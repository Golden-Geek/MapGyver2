// SPDX-License-Identifier: GPL-3.0-or-later
// Functional experiment, not a production deadline path. SDK types stay here.
#include "probe.hpp"
#include "bgfx_probe_device.hpp"
#include "probe_shaders.hpp"
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <SDL3/SDL.h>
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace mapgyver::experiments {
namespace {
using Microsoft::WRL::ComPtr;
void require(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << " HRESULT=0x" << std::hex << static_cast<unsigned long>(result);
        throw std::runtime_error(message.str());
    }
}
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void wait_gpu(ID3D11Device* device, ID3D11DeviceContext* context) {
    const D3D11_QUERY_DESC description{D3D11_QUERY_EVENT, 0};
    ComPtr<ID3D11Query> completion;
    require(device->CreateQuery(&description, &completion), "Create GPU completion query");
    context->End(completion.Get());
    context->Flush();
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;) {
        BOOL done = FALSE;
        const auto status = context->GetData(completion.Get(), &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        require(status, "GPU completion query");
        if (status == S_OK && done) return;
        if (std::chrono::steady_clock::now() >= limit) throw std::runtime_error("GPU completion exceeded two seconds");
        std::this_thread::yield();
    }
}
std::string utf8(const wchar_t* value) {
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    check(size > 0, "Adapter name conversion failed");
    std::string result(static_cast<std::size_t>(size), '\0');
    check(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), size, nullptr, nullptr) != 0,
          "Adapter name conversion failed");
    result.pop_back();
    return result;
}
struct CloseNtHandle {
    void operator()(void* handle) const noexcept { if (handle != nullptr) CloseHandle(handle); }
};
class ProbeCallbacks final : public bgfx::CallbackI {
public:
    std::array<char, 65536> trace{};
    std::size_t used = 0;
    std::size_t dropped = 0;
    void fatal(const char* file, std::uint16_t line, bgfx::Fatal::Enum code, const char* text) override {
        std::fwrite(trace.data(), 1, used, stderr);
        std::fprintf(stderr, "bgfx fatal %u at %s:%u: %s\n", static_cast<unsigned>(code), file,
                     static_cast<unsigned>(line), text);
        std::fflush(stderr);
        std::_Exit(1); // Fail even DebugCheck; never enter an interactive debugger dialog.
    }
    void traceVargs(const char*, std::uint16_t, const char* format, va_list arguments) override {
        const auto available = trace.size() - used;
        const int required = std::vsnprintf(trace.data() + used, available, format, arguments);
        if (required <= 0) return;
        const auto count = static_cast<std::size_t>(required);
        const auto retained = (count < available) ? count : available - 1;
        used += retained;
        dropped += count - retained;
    }
    // No profiler or persistent shader cache is configured for this experiment.
    void profilerBegin(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerBeginLiteral(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerEnd() override {}
    std::uint32_t cacheReadSize(std::uint64_t) override { return 0; }
    bool cacheRead(std::uint64_t, void*, std::uint32_t) override { return false; }
    void cacheWrite(std::uint64_t, const void*, std::uint32_t) override {}
    void screenShot(const char*, std::uint32_t, std::uint32_t, std::uint32_t, const void*, std::uint32_t, bool) override {
        fatal(__FILE__, __LINE__, bgfx::Fatal::DebugCheck, "Unrequested screenshot callback");
    }
    void captureBegin(std::uint32_t, std::uint32_t, std::uint32_t, bgfx::TextureFormat::Enum, bool) override {
        fatal(__FILE__, __LINE__, bgfx::Fatal::DebugCheck, "Unrequested capture callback");
    }
    void captureEnd() override { fatal(__FILE__, __LINE__, bgfx::Fatal::DebugCheck, "Unrequested capture callback"); }
    void captureFrame(const void*, std::uint32_t) override { fatal(__FILE__, __LINE__, bgfx::Fatal::DebugCheck, "Unrequested capture callback"); }
};
struct CaseResources {
    bgfx::TextureHandle source = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle target = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
    ComPtr<ID3D11Texture2D> producer_texture;
    ComPtr<ID3D11Texture2D> imported;
    ComPtr<IDXGIKeyedMutex> producer_mutex, consumer_mutex;
    bool producer_locked = false, consumer_locked = false;
};
struct Session {
    ProbeCallbacks callbacks;
    SDL_Window* window = nullptr;
    std::array<SDL_Window*, 3> output_windows{}; // Bounded offline multi-window fixture.
    bool initialized = false;
    ID3D11Device* device = nullptr; // Borrowed from bgfx, lifetime bounded by initialized.
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Device> producer;
    ComPtr<ID3D11DeviceContext> producer_context;
    DXGI_ADAPTER_DESC1 adapter{};
    std::array<CaseResources, 12> cases;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::ShaderHandle vertex_shader = BGFX_INVALID_HANDLE, fragment_shader = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    void retire(CaseResources& resources) {
        if (bgfx::isValid(resources.framebuffer)) bgfx::destroy(resources.framebuffer);
        if (bgfx::isValid(resources.source)) bgfx::destroy(resources.source);
        if (bgfx::isValid(resources.target)) bgfx::destroy(resources.target);
        resources.framebuffer = BGFX_INVALID_HANDLE;
        resources.source = BGFX_INVALID_HANDLE;
        resources.target = BGFX_INVALID_HANDLE;
    }
    ~Session() {
        if (initialized) {
            for (auto& resources : cases) retire(resources);
            if (bgfx::isValid(program)) bgfx::destroy(program);
            if (bgfx::isValid(vertex_shader)) bgfx::destroy(vertex_shader);
            if (bgfx::isValid(fragment_shader)) bgfx::destroy(fragment_shader);
            if (bgfx::isValid(sampler)) bgfx::destroy(sampler);
            if (bgfx::isValid(vertices)) bgfx::destroy(vertices);
            context.Reset();
            // Native imported COM resources remain held in cases until shutdown completes,
            // including exception unwinding. No borrowed resource is freed prematurely.
            bgfx::shutdown();
        }
        for (auto& resources : cases) {
            // This path also runs after an injected timeout. Normal releases above
            // are checked; cleanup failures remain visible while unwinding an error.
            if (resources.consumer_locked) {
                const auto status = resources.consumer_mutex->ReleaseSync(0);
                if (FAILED(status)) std::cerr << "Consumer mutex cleanup failed: " << status << '\n';
            }
            if (resources.producer_locked) {
                const auto status = resources.producer_mutex->ReleaseSync(0);
                if (FAILED(status)) std::cerr << "Producer mutex cleanup failed: " << status << '\n';
            }
        }
        for (auto* output : output_windows) if (output != nullptr) SDL_DestroyWindow(output);
        if (window != nullptr) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

void initialize(Session& session, bool independent_producer = true) {
    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
    session.window = SDL_CreateWindow("MapGyver M01 texture experiment", 128, 128, SDL_WINDOW_HIDDEN);
    check(session.window != nullptr, SDL_GetError());
    auto* window = SDL_GetPointerProperty(SDL_GetWindowProperties(session.window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    check(window != nullptr, "SDL did not provide a native window");
    ComPtr<IDXGIFactory1> factory;
    require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
    DXGI_ADAPTER_DESC1 desired{};
    bool found = false;
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        const auto status = factory->EnumAdapters1(index, &candidate);
        if (status == DXGI_ERROR_NOT_FOUND) break;
        require(status, "Enumerate hardware adapters");
        DXGI_ADAPTER_DESC1 description{};
        require(candidate->GetDesc1(&description), "Adapter description");
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
            (!found || description.DedicatedVideoMemory > desired.DedicatedVideoMemory)) {
            desired = description;
            found = true;
        }
    }
    check(found, "No hardware GPU; software cannot qualify this experiment");
    // The pinned manifest disables bgfx's multithreaded feature. frame() renders
    // inline. renderFrame() is invalid in that build and asserts before init.
    bgfx::Init init;
    init.type = bgfx::RendererType::Direct3D11;
    init.vendorId = static_cast<std::uint16_t>(desired.VendorId);
    init.deviceId = static_cast<std::uint16_t>(desired.DeviceId);
    init.platformData.nwh = window;
    init.resolution.width = 128;
    init.resolution.height = 128;
    init.resolution.reset = BGFX_RESET_NONE;
    init.callback = &session.callbacks;
    check(bgfx::init(init), "bgfx D3D11 initialization failed");
    session.initialized = true;
    check(bgfx::getRendererType() == bgfx::RendererType::Direct3D11, "Unexpected renderer fallback");
    session.device = static_cast<ID3D11Device*>(bgfx::getInternalData()->context);
    check(session.device != nullptr, "bgfx did not expose its D3D11 device");
    session.device->GetImmediateContext(&session.context);
    ComPtr<IDXGIDevice> dxgi_device;
    require(session.device->QueryInterface(IID_PPV_ARGS(&dxgi_device)), "DXGI device interface");
    ComPtr<IDXGIAdapter> adapter;
    require(dxgi_device->GetAdapter(&adapter), "Actual bgfx adapter");
    ComPtr<IDXGIAdapter1> adapter1;
    require(adapter.As(&adapter1), "Adapter identity interface");
    require(adapter1->GetDesc1(&session.adapter), "Actual adapter description");
    check((session.adapter.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0, "bgfx selected software rendering");
    check(session.adapter.AdapterLuid.HighPart == desired.AdapterLuid.HighPart &&
          session.adapter.AdapterLuid.LowPart == desired.AdapterLuid.LowPart, "bgfx selected a different GPU");
    if (independent_producer) {
        constexpr std::array levels{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL actual{};
        require(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                 levels.data(), static_cast<UINT>(levels.size()), D3D11_SDK_VERSION,
                                 &session.producer, &actual, &session.producer_context), "Create independent producer device");
    }
    session.vertex_shader = bgfx::createShader(bgfx::copy(shaders::vertex.data(), static_cast<std::uint32_t>(shaders::vertex.size())));
    session.fragment_shader = bgfx::createShader(bgfx::copy(shaders::fragment.data(), static_cast<std::uint32_t>(shaders::fragment.size())));
    check(bgfx::isValid(session.vertex_shader) && bgfx::isValid(session.fragment_shader), "Pinned shader creation failed");
    session.program = bgfx::createProgram(session.vertex_shader, session.fragment_shader, false);
    session.sampler = bgfx::createUniform("s_color", bgfx::UniformType::Sampler);
    check(bgfx::isValid(session.program) && bgfx::isValid(session.sampler), "Texture sampling program creation failed");
    struct Vertex { float x, y, z, u, v; };
    constexpr std::array vertices{Vertex{-1, -1, 0, 0, 1}, Vertex{3, -1, 0, 2, 1}, Vertex{-1, 3, 0, 0, -1}};
    bgfx::VertexLayout layout;
    layout.begin(bgfx::RendererType::Direct3D11).add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float).end();
    session.vertices = bgfx::createVertexBuffer(bgfx::copy(vertices.data(), static_cast<std::uint32_t>(sizeof(vertices))), layout);
    check(bgfx::isValid(session.vertices), "Sampling geometry creation failed");
    bgfx::frame();
}

nlohmann::json run_case(Session& session, std::size_t index, bool external, std::uint16_t side, bool bgra, bool hold_producer_key) {
    auto& resources = session.cases.at(index);
    const auto generation = static_cast<std::uint32_t>(index + 1);
    const auto format = bgra ? bgfx::TextureFormat::BGRA8 : bgfx::TextureFormat::RGBA8;
    const auto dxgi_format = bgra ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr std::uint64_t sampling = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT |
                                       BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    check(bgfx::isTextureValid(0, false, 1, format, sampling), "Source texture format unsupported by bgfx");
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(side) * side);
    for (UINT y = 0; y < side; ++y)
        for (UINT x = 0; x < side; ++x)
            pixels[static_cast<std::size_t>(y) * side + x] = ((128U + (x + y) % 128U) << 24U) | (generation << 16U) | (y << 8U) | x;
    UINT source_uploads = 0, gpu_draws = 0, validation_copies = 0, validation_readbacks = 0;
    resources.source = bgfx::createTexture2D(side, side, false, 1, format, sampling,
        external ? nullptr : bgfx::copy(pixels.data(), static_cast<std::uint32_t>(pixels.size() * sizeof(pixels[0]))));
    if (!external) ++source_uploads;
    resources.target = bgfx::createTexture2D(side, side, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | sampling);
    check(bgfx::isValid(resources.source) && bgfx::isValid(resources.target), "Texture handle allocation failed");
    bgfx::frame(); // Realize both resources before render-thread-only override calls.
    if (external) {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = side; description.Height = side; description.MipLevels = 1; description.ArraySize = 1;
        description.Format = dxgi_format; description.SampleDesc.Count = 1; description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        description.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        require(session.producer->CreateTexture2D(&description, nullptr, &resources.producer_texture), "Create producer texture");
        ComPtr<IDXGIResource1> shared;
        require(resources.producer_texture.As(&shared), "Producer sharing interface");
        HANDLE raw = nullptr;
        require(shared->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &raw), "Create NT share handle");
        std::unique_ptr<void, CloseNtHandle> handle{raw};
        ComPtr<ID3D11Device1> consumer;
        require(session.device->QueryInterface(IID_PPV_ARGS(&consumer)), "Consumer D3D11.1 interface");
        require(consumer->OpenSharedResource1(handle.get(), IID_PPV_ARGS(&resources.imported)), "Import producer resource into bgfx device");
        handle.reset();
        require(resources.producer_texture.As(&resources.producer_mutex), "Producer keyed mutex");
        require(resources.imported.As(&resources.consumer_mutex), "Consumer keyed mutex");
        check(resources.producer_mutex->AcquireSync(0, 1000) == S_OK, "Producer key acquire failed/timed out/abandoned");
        resources.producer_locked = true;
        session.producer_context->UpdateSubresource(resources.producer_texture.Get(), 0, nullptr, pixels.data(), side * 4U, 0);
        ++source_uploads;
        wait_gpu(session.producer.Get(), session.producer_context.Get());
        if (!hold_producer_key) {
            require(resources.producer_mutex->ReleaseSync(1), "Producer key release");
            resources.producer_locked = false;
        }
        const auto acquire = resources.consumer_mutex->AcquireSync(1, 1000);
        if (acquire != S_OK) {
            std::ostringstream message;
            message << "Consumer keyed mutex acquire status=0x" << std::hex << static_cast<unsigned long>(acquire);
            throw std::runtime_error(message.str());
        }
        resources.consumer_locked = true;
        const auto imported = bgfx::overrideInternal(resources.source, reinterpret_cast<uintptr_t>(resources.imported.Get()));
        check(imported == reinterpret_cast<uintptr_t>(resources.imported.Get()), "bgfx did not import the requested native texture");
    }
    // The size/format overload creates a bgfx-owned native texture and returns it.
    // Passing a null pointer to the pointer overload is NOT a getter in this pin.
    const auto native_target = bgfx::overrideInternal(resources.target, side, side, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT | sampling);
    check(native_target != 0, "bgfx-owned render target was not realized");
    auto* target = reinterpret_cast<ID3D11Texture2D*>(native_target); // Borrowed until target handle retires.
    resources.framebuffer = bgfx::createFrameBuffer(1, &resources.target, false);
    check(bgfx::isValid(resources.framebuffer), "Framebuffer creation failed");
    constexpr std::array<float, 16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    SDL_PumpEvents();
    bgfx::setViewFrameBuffer(0, resources.framebuffer);
    bgfx::setViewRect(0, 0, 0, side, side);
    bgfx::setViewTransform(0, identity.data(), identity.data());
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR, 0x00000000);
    bgfx::setTransform(identity.data());
    bgfx::setVertexBuffer(0, session.vertices);
    bgfx::setTexture(0, session.sampler, resources.source, static_cast<std::uint32_t>(sampling));
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(0, session.program);
    ++gpu_draws;
    const auto rendered_frame = bgfx::frame();
    // Validation is outside the submitted rendering operation; never a show path.
    D3D11_TEXTURE2D_DESC staging_description{};
    target->GetDesc(&staging_description);
    staging_description.Usage = D3D11_USAGE_STAGING; staging_description.BindFlags = 0;
    staging_description.MiscFlags = 0; staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    require(session.device->CreateTexture2D(&staging_description, nullptr, &staging), "Create readback validation texture");
    session.context->CopyResource(staging.Get(), target);
    ++validation_copies;
    wait_gpu(session.device, session.context.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require(session.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map rendered pixels");
    ++validation_readbacks;
    std::string mismatch;
    for (UINT y = 0; y < side && mismatch.empty(); ++y) {
        const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
        for (UINT x = 0; x < side; ++x) {
            auto expected = pixels[static_cast<std::size_t>(y) * side + x];
            if (bgra) expected = (expected & 0xff00ff00U) | ((expected & 0xffU) << 16U) | ((expected >> 16U) & 0xffU);
            if (row[x] != expected) {
                std::ostringstream message;
                message << "Sampled pixel mismatch at " << x << ',' << y << " expected 0x" << std::hex << expected << " got 0x" << row[x];
                mismatch = message.str();
                break;
            }
        }
    }
    session.context->Unmap(staging.Get(), 0);
    if (external) {
        require(resources.consumer_mutex->ReleaseSync(0), "Consumer key release after GPU completion");
        resources.consumer_locked = false;
    }
    session.retire(resources);
    bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
    bgfx::frame(); bgfx::frame(); // Execute deferred destruction on this same owner thread.
    wait_gpu(session.device, session.context.Get());
    resources.consumer_mutex.Reset(); resources.producer_mutex.Reset();
    resources.imported.Reset(); resources.producer_texture.Reset();
    require(session.device->GetDeviceRemovedReason(), "bgfx device health");
    check(mismatch.empty(), mismatch.c_str());
    return {{"generation", generation}, {"mode", external ? "imported_shared" : "bgfx_owned"},
            {"format", bgra ? "BGRA8" : "RGBA8"}, {"width", side}, {"height", side},
            {"appliedBgfxFrame", rendered_frame}, {"pixelsAndAlphaMatchExactly", true},
            {"sourceUploadCalls", source_uploads}, {"renderDrawSubmissions", gpu_draws},
            {"validationGpuCopySubmissions", validation_copies}, {"validationCpuReadbacks", validation_readbacks},
            {"retiredAfterGpuCompletion", true}};
}
} // namespace

nlohmann::json bgfx_texture_interop(bool hold_producer_key) {
    Session session;
    initialize(session);
    nlohmann::json cases = nlohmann::json::array();
    std::size_t index = 0;
    for (const bool external : {false, true})
        for (const bool bgra : {false, true})
            for (const std::uint16_t side : {std::uint16_t{64}, std::uint16_t{128}, std::uint16_t{256}})
                cases.push_back(run_case(session, index++, external, side, bgra, hold_producer_key));
    return {{"schemaVersion", 1}, {"evidenceClass", "native-gpu-bgfx-d3d11-sampling"}, {"result", "passed"},
            {"bgfxVersion", "1.129.8940-496#1"}, {"sdlVersion", SDL_GetVersion()},
            {"renderer", bgfx::getRendererName(bgfx::getRendererType())},
            {"adapter", {{"name", utf8(session.adapter.Description)}, {"vendorId", session.adapter.VendorId},
             {"deviceId", session.adapter.DeviceId}, {"luidHigh", session.adapter.AdapterLuid.HighPart},
             {"luidLow", session.adapter.AdapterLuid.LowPart}, {"software", false}}},
            {"ownership", "single API/render owner; imported COM resources retained through bgfx destruction and GPU completion"},
            {"counterMeaning", "counted API submissions, not hardware bus/copy-engine counters; staging readback is validation only"},
            {"traceBytesDropped", session.callbacks.dropped},
            {"spoutInterop", "not_run"}, {"physicalOutputs", "not_tested"}, {"cases", std::move(cases)}};
}

nlohmann::json bgfx_window_submission() {
    Session session;
    initialize(session, false);
    check((bgfx::getCaps()->supported & BGFX_CAPS_SWAP_CHAIN) != 0, "Candidate does not support native multi-window swapchains");
    ComPtr<IDXGIDevice> dxgi;
    require(session.device->QueryInterface(IID_PPV_ARGS(&dxgi)), "Window probe DXGI device");
    ComPtr<IDXGIAdapter> base; require(dxgi->GetAdapter(&base), "Window probe adapter");
    ComPtr<IDXGIAdapter3> adapter; require(base.As(&adapter), "Window probe memory adapter");
    std::array<std::uint32_t, 3> generations{};
    std::array<int, 3> widths{}, heights{};
    const auto memory = [&] {
        DXGI_QUERY_VIDEO_MEMORY_INFO information{};
        require(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &information), "Window video memory query");
        return nlohmann::json{{"localVideoUsageBytes", information.CurrentUsage}, {"localVideoBudgetBytes", information.Budget},
                              {"bgfxFrameBufferHandles", bgfx::getStats()->numFrameBuffers}};
    };
    const auto before = memory();
    const auto release_gpu = [&](std::size_t index) {
        bgfx::setViewFrameBuffer(static_cast<bgfx::ViewId>(index), BGFX_INVALID_HANDLE);
        session.retire(session.cases[index]);
    };
    const auto flush_retirement = [&] {
        bgfx::frame(); bgfx::frame(); // Pinned upstream example requires this before HWND destruction/reuse.
        wait_gpu(session.device, session.context.Get());
        require(session.device->GetDeviceRemovedReason(), "Window retirement device health");
    };
    const auto attach = [&](std::size_t index, int width, int height) {
        auto* output = session.output_windows[index];
        check(output != nullptr && (SDL_GetWindowFlags(output) & SDL_WINDOW_HIDDEN) != 0, "Window fixture must stay hidden");
        int actual_width = 0, actual_height = 0;
        check(SDL_GetWindowSizeInPixels(output, &actual_width, &actual_height), SDL_GetError());
        check(actual_width == width && actual_height == height, "Native window pixel size did not settle");
        auto* native = SDL_GetPointerProperty(SDL_GetWindowProperties(output), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        check(native != nullptr, "Output window has no native HWND");
        auto& resources = session.cases[index];
        resources.framebuffer = bgfx::createFrameBuffer(native, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height));
        check(bgfx::isValid(resources.framebuffer), "Native swapchain framebuffer creation failed");
        resources.source = bgfx::createTexture2D(16, 16, false, 1, bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        check(bgfx::isValid(resources.source), "Window sample texture allocation failed");
        widths[index] = width; heights[index] = height; ++generations[index];
    };
    const auto create = [&](std::size_t index, int width, int height) {
        session.output_windows[index] = SDL_CreateWindow("MapGyver M01 hidden output API fixture", width, height,
            SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
        check(session.output_windows[index] != nullptr, SDL_GetError());
        attach(index, width, height);
    };
    for (std::size_t index = 0; index < 3; ++index) create(index, 64 + static_cast<int>(index)*16, 64 + static_cast<int>(index)*16);
    nlohmann::json stages = nlohmann::json::array();
    std::uint32_t source_sequence = 0;
    constexpr std::array<float,16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for (int stage = 0; stage < 4; ++stage) {
        if (stage == 1) {
            for (std::size_t index = 0; index < 3; ++index) release_gpu(index);
            flush_retirement(); // Prevent two swapchains being attached to one HWND.
            for (std::size_t index = 0; index < 3; ++index) {
                const int side = 128 + static_cast<int>(index)*16;
                check(SDL_SetWindowSize(session.output_windows[index], side, side), SDL_GetError());
                check(SDL_SyncWindow(session.output_windows[index]), SDL_GetError());
                attach(index, side, side);
            }
        } else if (stage == 2) {
            release_gpu(1); flush_retirement();
            SDL_DestroyWindow(session.output_windows[1]); session.output_windows[1] = nullptr;
        } else if (stage == 3) {
            create(1, 96, 112); // New native-window identity; peers continue on the same GPU owner.
        }
        SDL_PumpEvents();
        nlohmann::json windows = nlohmann::json::array();
        for (std::size_t index = 0; index < 3; ++index) {
            if (session.output_windows[index] != nullptr) {
                windows.push_back({{"outputId", "output-"+std::to_string(index)},
                    {"sdlWindowId", SDL_GetWindowID(session.output_windows[index])}, {"resourceGeneration", generations[index]},
                    {"width", widths[index]}, {"height", heights[index]}, {"hidden", true}});
            }
        }
        nlohmann::json frames = nlohmann::json::array();
        for (int repeat = 0; repeat < 3; ++repeat) {
            ++source_sequence;
            const auto started = std::chrono::steady_clock::now();
            std::uint32_t draws = 0;
            for (std::size_t index = 0; index < 3; ++index) {
                if (session.output_windows[index] == nullptr) continue; // Explicit removed test output only.
                auto& resources = session.cases[index];
                std::array<std::uint32_t,256> pixels{};
                pixels.fill(0xff000000U | source_sequence * 256U | static_cast<std::uint32_t>(index+1));
                bgfx::updateTexture2D(resources.source, 0, 0, 0, 0, 16, 16, bgfx::copy(pixels.data(), sizeof(pixels)));
                const auto view = static_cast<bgfx::ViewId>(index);
                bgfx::setViewFrameBuffer(view, resources.framebuffer);
                bgfx::setViewRect(view, 0, 0, static_cast<std::uint16_t>(widths[index]), static_cast<std::uint16_t>(heights[index]));
                bgfx::setViewTransform(view, identity.data(), identity.data());
                bgfx::setViewClear(view, BGFX_CLEAR_COLOR, 0);
                bgfx::setTransform(identity.data()); bgfx::setVertexBuffer(0, session.vertices);
                bgfx::setTexture(0, session.sampler, resources.source);
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                bgfx::submit(view, session.program); ++draws;
            }
            const auto applied = bgfx::frame();
            bgfx::frame(); // Execute the pinned backend's next flip/present cycle.
            wait_gpu(session.device, session.context.Get());
            require(session.device->GetDeviceRemovedReason(), "Multi-window submission device health");
            frames.push_back({{"sourceSequence", source_sequence}, {"appliedBgfxFrame", applied},
                {"windowDrawSubmissions", draws}, {"sourceCpuUploadCalls", draws}, {"cpuReadbacks",0},
                {"offlineSubmissionCompletionMicroseconds",std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count()}});
        }
        stages.push_back({{"stage",stage}, {"windows",windows}, {"frames",frames}, {"memory",memory()}});
    }
    for (std::size_t index = 0; index < 3; ++index) release_gpu(index);
    flush_retirement();
    const auto after = memory();
    for (auto*& output : session.output_windows) { SDL_DestroyWindow(output); output = nullptr; }
    int remaining_windows = 0;
    auto** windows = SDL_GetWindows(&remaining_windows);
    check(windows != nullptr, SDL_GetError());
    SDL_free(windows);
    return {{"schemaVersion",1}, {"evidenceClass","native-gpu-hidden-sdl-bgfx-multi-window-api"}, {"result","passed"},
        {"adapter",{{"name",utf8(session.adapter.Description)},{"vendorId",session.adapter.VendorId},{"deviceId",session.adapter.DeviceId},
                    {"luidHigh",session.adapter.AdapterLuid.HighPart},{"luidLow",session.adapter.AdapterLuid.LowPart}}},
        {"stages",stages}, {"memoryBefore",before}, {"memoryAfterRetirement",after},
        {"remainingSdlWindows",remaining_windows},
        {"ownership","Single native GPU/API owner; swapchain destruction flushed before native window resize/reuse/destruction"},
        {"visibility","All fixture windows hidden; no browser or preview subscriber"},
        {"counterMeaning","Explicit draw/upload API submissions; no Present/scanout/hardware transfer counter is claimed"},
        {"windowPixels","not_read_back"}, {"physicalTwoOutputQualification","not_run"},
        {"opticalSynchronization","not_claimed"}, {"productionDeadlineBudget","not_qualified"},
        {"traceBytesDropped",session.callbacks.dropped}};
}
} // namespace mapgyver::experiments

namespace mapgyver::windows {
using namespace mapgyver::experiments;
struct BgfxProbeDevice::Impl {
    Session session;
    std::thread::id owner = std::this_thread::get_id();
    std::size_t active_count = 0;
    Impl() { initialize(session, false); }
};
BgfxProbeDevice::BgfxProbeDevice() : impl_(std::make_unique<Impl>()) {}
BgfxProbeDevice::~BgfxProbeDevice() = default;
ID3D11Device* BgfxProbeDevice::device() const noexcept { return impl_->session.device; }
BgfxProbeDevice::Sample BgfxProbeDevice::sample(ID3D11Texture2D* source) {
    const std::array inputs{source};
    return sample_set(inputs).members[0];
}
BgfxProbeDevice::SampleSet BgfxProbeDevice::sample_set(std::span<ID3D11Texture2D* const> sources) {
    auto& state = *impl_;
    auto& session = state.session;
    check(std::this_thread::get_id() == state.owner, "Candidate sampling must run on its GPU owner thread");
    check(state.active_count == 0 && !sources.empty() && sources.size() <= session.cases.size(),
          "Only one bounded candidate sample set may be live");
    std::array<D3D11_TEXTURE2D_DESC, 12> descriptions{};
    for (std::size_t index = 0; index < sources.size(); ++index) {
        check(sources[index] != nullptr, "Candidate set contains a missing source");
        ComPtr<ID3D11Device> source_device;
        sources[index]->GetDevice(&source_device);
        check(source_device.Get() == session.device, "Candidate import belongs to another D3D11 device");
        auto& description = descriptions[index];
        sources[index]->GetDesc(&description);
        check(description.Width > 0 && description.Width <= 512 && description.Height > 0 && description.Height <= 512,
              "Candidate fixture size unsupported");
        check(description.Format == DXGI_FORMAT_R8G8B8A8_UNORM || description.Format == DXGI_FORMAT_B8G8R8A8_UNORM,
              "Candidate fixture format unsupported");
    }
    constexpr std::uint64_t sampling = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT |
                                       BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    state.active_count = sources.size();
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const auto& description = descriptions[index];
        const auto format = description.Format == DXGI_FORMAT_R8G8B8A8_UNORM ? bgfx::TextureFormat::RGBA8 : bgfx::TextureFormat::BGRA8;
        check(bgfx::isTextureValid(0, false, 1, format, BGFX_TEXTURE_RT | sampling), "Candidate format cannot be rendered");
        auto& resources = session.cases[index];
        resources.imported = sources[index]; // Held until every member's views and GPU use retire.
        resources.source = bgfx::createTexture2D(static_cast<std::uint16_t>(description.Width), static_cast<std::uint16_t>(description.Height), false, 1, format, sampling);
        resources.target = bgfx::createTexture2D(static_cast<std::uint16_t>(description.Width), static_cast<std::uint16_t>(description.Height), false, 1, format, BGFX_TEXTURE_RT | sampling);
        check(bgfx::isValid(resources.source) && bgfx::isValid(resources.target), "Candidate sample allocation failed");
    }
    bgfx::frame(); // Execute all bounded allocations before importing native resources.
    SampleSet result;
    result.count = sources.size();
    constexpr std::array<float, 16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    SDL_PumpEvents();
    for (std::size_t index = 0; index < sources.size(); ++index) {
        auto& resources = session.cases[index];
        const auto& description = descriptions[index];
        const auto width = static_cast<std::uint16_t>(description.Width);
        const auto height = static_cast<std::uint16_t>(description.Height);
        const auto format = description.Format == DXGI_FORMAT_R8G8B8A8_UNORM ? bgfx::TextureFormat::RGBA8 : bgfx::TextureFormat::BGRA8;
        check(bgfx::overrideInternal(resources.source, reinterpret_cast<uintptr_t>(sources[index])) == reinterpret_cast<uintptr_t>(sources[index]),
              "Candidate did not import the caller-owned texture");
        const auto native = bgfx::overrideInternal(resources.target, width, height, 1, format, BGFX_TEXTURE_RT | sampling);
        check(native != 0, "Candidate target is not ready");
        resources.framebuffer = bgfx::createFrameBuffer(1, &resources.target, false);
        check(bgfx::isValid(resources.framebuffer), "Candidate framebuffer allocation failed");
        const auto view = static_cast<bgfx::ViewId>(index);
        bgfx::setViewFrameBuffer(view, resources.framebuffer);
        bgfx::setViewRect(view, 0, 0, width, height);
        bgfx::setViewTransform(view, identity.data(), identity.data());
        bgfx::setViewClear(view, BGFX_CLEAR_COLOR, 0);
        bgfx::setTransform(identity.data()); bgfx::setVertexBuffer(0, session.vertices);
        bgfx::setTexture(0, session.sampler, resources.source, static_cast<std::uint32_t>(sampling));
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(view, session.program);
        result.members[index].texture = reinterpret_cast<ID3D11Texture2D*>(native);
    }
    result.applied_frame = bgfx::frame(); // One immutable software application frame for the whole set.
    for (std::size_t index = 0; index < result.count; ++index) result.members[index].applied_frame = result.applied_frame;
    return result;
}
void BgfxProbeDevice::retire() {
    auto& state = *impl_;
    check(std::this_thread::get_id() == state.owner, "Candidate retirement must run on its GPU owner thread");
    if (state.active_count == 0) return;
    for (std::size_t index = 0; index < state.active_count; ++index) {
        state.session.retire(state.session.cases[index]);
        bgfx::setViewFrameBuffer(static_cast<bgfx::ViewId>(index), BGFX_INVALID_HANDLE);
    }
    bgfx::frame(); bgfx::frame();
    wait_gpu(state.session.device, state.session.context.Get());
    for (std::size_t index = 0; index < state.active_count; ++index) state.session.cases[index].imported.Reset();
    state.active_count = 0;
}
} // namespace mapgyver::windows
