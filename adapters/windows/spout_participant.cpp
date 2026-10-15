// SPDX-License-Identifier: GPL-3.0-or-later
// Headless M01 qualification participant. Console I/O/readback are control/validation,
// never a production render deadline. This file alone owns platform and SDK types.
#ifdef MAPGYVER_REFERENCE_SPOUT
#include <SpoutDX.h>
#else
#include <SpoutDX/SpoutDX.h>
#endif
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#ifdef MAPGYVER_REFERENCE_SPOUT
#include <mapgyver/source_revision.hpp>
#else
#include <mapgyver/build_info.hpp>
#include "bgfx_probe_device.hpp"
#endif

namespace {
using Microsoft::WRL::ComPtr;
using nlohmann::json;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void require(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << " HRESULT=0x" << std::hex << static_cast<unsigned long>(result);
        throw std::runtime_error(message.str());
    }
}
std::string utf8(const wchar_t* value) {
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    check(size > 0, "UTF-8 conversion failed");
    std::string result(static_cast<std::size_t>(size), '\0');
    check(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), size, nullptr, nullptr) != 0,
          "UTF-8 conversion failed");
    result.pop_back();
    return result;
}
struct CloseHandle {
    void operator()(void* value) const noexcept { if (value != nullptr) ::CloseHandle(value); }
};
class MutexLease {
public:
    static std::unique_ptr<MutexLease> acquire(const std::string& sender, bool create) {
        const auto name = sender + "_SpoutAccessMutex";
        HANDLE handle = create ? CreateMutexA(nullptr, FALSE, name.c_str()) :
            OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name.c_str());
        if (handle == nullptr) {
            if (!create && GetLastError() == ERROR_FILE_NOT_FOUND) return nullptr;
            throw std::runtime_error("Spout named-mutex open/create failed");
        }
        std::unique_ptr<void, CloseHandle> owner{handle};
        const DWORD status = WaitForSingleObject(handle, 0);
        if (status == WAIT_TIMEOUT) return nullptr;
        if (status == WAIT_ABANDONED) {
            ReleaseMutex(handle);
            throw std::runtime_error("Spout named mutex was abandoned");
        }
        check(status == WAIT_OBJECT_0, "Spout named-mutex acquire failed");
        return std::unique_ptr<MutexLease>(new MutexLease(std::move(owner)));
    }
    ~MutexLease() {
        if (!ReleaseMutex(handle_.get())) std::cerr << "Spout named-mutex release failed\n";
    }
    MutexLease(const MutexLease&) = delete;
    MutexLease& operator=(const MutexLease&) = delete;
private:
    explicit MutexLease(std::unique_ptr<void, CloseHandle> handle) : handle_(std::move(handle)) {}
    std::unique_ptr<void, CloseHandle> handle_;
};
void wait_gpu(ID3D11Device* device, ID3D11DeviceContext* context) {
    const D3D11_QUERY_DESC description{D3D11_QUERY_EVENT, 0};
    ComPtr<ID3D11Query> completion;
    require(device->CreateQuery(&description, &completion), "Create completion query");
    context->End(completion.Get()); context->Flush();
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;) {
        BOOL done = FALSE;
        const auto status = context->GetData(completion.Get(), &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        require(status, "GPU completion query");
        if (status == S_OK && done) break;
        check(std::chrono::steady_clock::now() < limit, "GPU completion timed out");
        std::this_thread::yield();
    }
    require(device->GetDeviceRemovedReason(), "D3D11 device health");
}
std::uint32_t pixel(UINT x, UINT y, std::uint32_t marker) {
    return marker ^ (x * 0x9e3779b9U) ^ (y * 0x85ebca6bU);
}
struct Stats {
    std::uint64_t uploads = 0, sdk_copies = 0, validation_copies = 0, readbacks = 0, blocked = 0, draws = 0;
    json report() const {
        return {{"sourceCpuUploadCalls", uploads}, {"sdkGpuCopyPathCalls", sdk_copies},
                {"validationGpuCopySubmissions", validation_copies}, {"validationCpuReadbacks", readbacks},
                {"nonblockingMutexMisses", blocked}, {"candidateDrawSubmissions", draws}};
    }
};
class Participant {
public:
    Participant(std::string role, std::string name) : role_(std::move(role)), name_(std::move(name)) {
        ComPtr<IDXGIFactory1> factory;
        require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
        ComPtr<IDXGIAdapter1> selected;
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> candidate;
            const auto status = factory->EnumAdapters1(index, &candidate);
            if (status == DXGI_ERROR_NOT_FOUND) break;
            require(status, "EnumAdapters1");
            DXGI_ADAPTER_DESC1 description{};
            require(candidate->GetDesc1(&description), "Adapter description");
            if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                (!selected || description.DedicatedVideoMemory > adapter_.DedicatedVideoMemory)) {
                selected = candidate; adapter_ = description;
            }
        }
        check(selected != nullptr, "No hardware GPU; WARP is not this profile");
#ifdef MAPGYVER_REFERENCE_SPOUT
        constexpr std::array levels{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL actual{};
        require(D3D11CreateDevice(selected.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                 levels.data(), static_cast<UINT>(levels.size()), D3D11_SDK_VERSION,
                                 &device_, &actual, &context_), "Create participant device");
#else
        renderer_ = std::make_unique<mapgyver::windows::BgfxProbeDevice>();
        device_ = renderer_->device();
        device_->GetImmediateContext(&context_);
        ComPtr<IDXGIDevice> actual_device;
        require(device_.As(&actual_device), "Candidate DXGI identity");
        ComPtr<IDXGIAdapter> actual_adapter;
        require(actual_device->GetAdapter(&actual_adapter), "Candidate adapter");
        ComPtr<IDXGIAdapter1> actual_adapter1;
        require(actual_adapter.As(&actual_adapter1), "Candidate adapter identity");
        DXGI_ADAPTER_DESC1 actual{};
        require(actual_adapter1->GetDesc1(&actual), "Candidate adapter description");
        check(actual.AdapterLuid.HighPart == adapter_.AdapterLuid.HighPart && actual.AdapterLuid.LowPart == adapter_.AdapterLuid.LowPart,
              "Candidate and participant selected different GPUs");
#endif
        sdk_ = std::make_unique<spoutDX>();
        spoututils::DisableSpoutLog(); spoututils::DisableSpoutLogFile();
        check(sdk_->OpenDirectX11(device_.Get()), "SDK OpenDirectX11 failed");
        sdk_->SetAdapterAuto(false);
        sdk_->DisableFrameCount(); // Instance only. Never call global registry SetFrameCount.
        check(!sdk_->IsFrameCountEnabled(), "SDK instance frame count did not disable");
        if (role_ == "sender") {
            unsigned int width = 0, height = 0; HANDLE handle = nullptr; DWORD format = 0;
            check(!sdk_->GetSenderInfo(name_.c_str(), width, height, handle, format), "Sender name already belongs to another process");
#ifndef MAPGYVER_REFERENCE_SPOUT
            sdk_->SetKeyed(false);
#endif
            check(sdk_->SetSenderName(name_.c_str()), "SDK sender name rejected");
        } else {
            sdk_->SetReceiverName(name_.c_str());
        }
    }
    ~Participant() {
        held_.reset();
        if (sdk_) {
            if (role_ == "sender") sdk_->ReleaseSender(); else sdk_->ReleaseReceiver();
            sdk_->CloseDirectX11();
            sdk_.reset();
        }
        texture_.Reset(); staging_.Reset(); context_.Reset(); device_.Reset();
#ifndef MAPGYVER_REFERENCE_SPOUT
        renderer_.reset();
#endif
    }
    json info() const {
        std::array<wchar_t, 32768> module{};
        const auto loaded = GetModuleHandleW(L"SpoutDX.dll");
        check(loaded != nullptr && GetModuleFileNameW(loaded, module.data(), static_cast<DWORD>(module.size())) != 0,
              "SDK DLL module identity unavailable");
        return {{"event", "ready"}, {"role", role_}, {"name", name_}, {"sdkVersion", MAPGYVER_SPOUT_SDK_VERSION},
                {"sdkModulePath", utf8(module.data())}, {"pid", GetCurrentProcessId()},
                {"adapter", {{"name", utf8(adapter_.Description)}, {"vendorId", adapter_.VendorId},
                             {"deviceId", adapter_.DeviceId}, {"luidHigh", adapter_.AdapterLuid.HighPart},
                             {"luidLow", adapter_.AdapterLuid.LowPart}, {"software", false}}},
                {"sdkFrameCountEnabled", sdk_->IsFrameCountEnabled()},
                {"counterMeaning", "API-path counts under owned non-keyed mutex and disabled instance frame count; not hardware counters"},
#ifdef MAPGYVER_REFERENCE_SPOUT
                {"renderer", "native-reference"}
#else
                {"renderer", "bgfx-D3D11"}
#endif
                };
    }
    json command(const json& request) {
        const auto op = request.at("op").get<std::string>();
        if (op == "publish") return publish(request);
        if (op == "poll") return poll();
        if (op == "lock") {
            check(!held_, "Mutex already held by this participant");
            held_ = MutexLease::acquire(name_, role_ == "sender");
            check(held_ != nullptr, "Could not acquire fixture stall lock");
            return {{"locked", true}};
        }
        if (op == "unlock") { held_.reset(); return {{"locked", false}}; }
        if (op == "stats") return stats_.report();
        throw std::runtime_error("Unknown participant operation");
    }
private:
    json publish(const json& request) {
        check(role_ == "sender", "Only senders publish");
        check(request.at("side").is_number_unsigned() && request.at("marker").is_number_unsigned(),
              "Fixture dimensions and marker must be unsigned integers");
        check(request.at("side").get<std::uint64_t>() <= 512 &&
              request.at("marker").get<std::uint64_t>() <= UINT32_MAX, "Fixture integer exceeds capacity");
        const auto side = request.at("side").get<unsigned int>();
        const auto marker = request.at("marker").get<std::uint32_t>();
        const auto format_name = request.at("format").get<std::string>();
        check(side > 0 && side <= 512 && marker > 0, "Fixture dimensions/marker out of bounds");
        check(format_name == "RGBA8" || format_name == "BGRA8", "Unsupported fixture texture format");
        auto lease = MutexLease::acquire(name_, true);
        if (!lease) { ++stats_.blocked; return {{"copySubmitted", false}, {"busy", true}, {"stats", stats_.report()}}; }
        const auto format = format_name == "RGBA8" ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
        if (!texture_ || width_ != side || format_ != format) {
            texture_.Reset();
            D3D11_TEXTURE2D_DESC description{};
            description.Width = side; description.Height = side; description.MipLevels = 1; description.ArraySize = 1;
            description.Format = format; description.SampleDesc.Count = 1; description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            require(device_->CreateTexture2D(&description, nullptr, &texture_), "Create publication texture");
            width_ = side; format_ = format; ++generation_;
        }
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(side) * side);
        for (UINT y = 0; y < side; ++y)
            for (UINT x = 0; x < side; ++x) pixels[static_cast<std::size_t>(y) * side + x] = pixel(x, y, marker);
        context_->UpdateSubresource(texture_.Get(), 0, nullptr, pixels.data(), side * 4U, 0);
        ++stats_.uploads;
        wait_gpu(device_.Get(), context_.Get());
        ID3D11Texture2D* outgoing = texture_.Get();
        std::uint32_t applied_frame = 0;
#ifndef MAPGYVER_REFERENCE_SPOUT
        const auto sample = renderer_->sample(texture_.Get());
        outgoing = sample.texture; applied_frame = sample.applied_frame; ++stats_.draws;
#endif
        check(sdk_->SendTexture(outgoing), "SDK SendTexture failed");
        check(name_ == sdk_->GetName(), "SDK changed the requested sender identity");
        check(sdk_->GetDX11Device() == device_.Get(), "SDK changed GPU device unexpectedly");
        ++stats_.sdk_copies; // The recursive named mutex and disabled frame count force this pinned copy path.
        wait_gpu(device_.Get(), context_.Get());
#ifndef MAPGYVER_REFERENCE_SPOUT
        renderer_->retire();
#endif
        return {{"copySubmitted", true}, {"busy", false}, {"marker", marker}, {"generation", generation_},
                {"side", side}, {"format", format_name}, {"appliedBgfxFrame", applied_frame}, {"stats", stats_.report()}};
    }
    json poll() {
        check(role_ == "receiver", "Only receivers poll");
        unsigned int width = 0, height = 0; HANDLE handle = nullptr; DWORD format = 0;
        if (!sdk_->GetSenderInfo(name_.c_str(), width, height, handle, format)) {
            sdk_->ReleaseReceiver(); observed_handle_ = nullptr;
            return {{"connected", false}, {"ready", false}, {"lastValidatedMarker", last_marker_}, {"stats", stats_.report()}};
        }
        check(width > 0 && width <= 512 && height == width, "Fixture sender geometry unsupported");
        check(format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM, "Fixture sender format unsupported");
        auto lease = MutexLease::acquire(name_, false);
        if (!lease) { ++stats_.blocked; return {{"connected", true}, {"ready", false}, {"busy", true}, {"stats", stats_.report()}}; }
        if (handle != observed_handle_ || width != width_ || static_cast<DXGI_FORMAT>(format) != format_) {
            ComPtr<ID3D11Texture2D> shared;
            require(device_->OpenSharedResource(handle, IID_PPV_ARGS(&shared)), "Open discovered Spout shared texture");
            D3D11_TEXTURE2D_DESC description{};
            shared->GetDesc(&description);
            check((description.MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX) == 0,
                  "This controlled copy-count profile requires a non-keyed sender");
            description.Usage = D3D11_USAGE_DEFAULT; description.MiscFlags = 0; description.CPUAccessFlags = 0;
            texture_.Reset(); staging_.Reset();
            require(device_->CreateTexture2D(&description, nullptr, &texture_), "Create caller-owned receive texture");
            description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            require(device_->CreateTexture2D(&description, nullptr, &staging_), "Create validation staging texture");
            observed_handle_ = handle; width_ = width; format_ = static_cast<DXGI_FORMAT>(format); ++generation_;
        }
        auto* pointer = texture_.Get();
        sdk_->SetReceiverName(name_.c_str());
        const bool received = sdk_->ReceiveTexture(&pointer);
        check(pointer == texture_.Get(), "SDK replaced caller-owned receive texture");
        const bool updated = sdk_->IsUpdated();
        if (!received || updated)
            return {{"connected", sdk_->IsConnected()}, {"ready", false}, {"updated", updated}, {"generation", generation_}, {"stats", stats_.report()}};
        check(sdk_->GetDX11Device() == device_.Get(), "SDK switched GPU unexpectedly");
        ++stats_.sdk_copies;
        ID3D11Texture2D* received_texture = texture_.Get();
        std::uint32_t applied_frame = 0;
#ifndef MAPGYVER_REFERENCE_SPOUT
        const auto sample = renderer_->sample(texture_.Get());
        received_texture = sample.texture; applied_frame = sample.applied_frame; ++stats_.draws;
#endif
        context_->CopyResource(staging_.Get(), received_texture); ++stats_.validation_copies;
        wait_gpu(device_.Get(), context_.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        require(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map received validation pixels");
        ++stats_.readbacks;
        const auto marker = *static_cast<const std::uint32_t*>(mapped.pData);
        bool valid = marker != 0;
        std::uint32_t hash = 2166136261U;
        for (UINT y = 0; y < width_; ++y) {
            const auto* bytes = static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch;
            const auto* row = reinterpret_cast<const std::uint32_t*>(bytes);
            for (UINT x = 0; x < width_; ++x) valid = valid && row[x] == pixel(x, y, marker);
            for (UINT x = 0; x < width_ * 4U; ++x) hash = (hash ^ bytes[x]) * 16777619U;
        }
        context_->Unmap(staging_.Get(), 0);
#ifndef MAPGYVER_REFERENCE_SPOUT
        renderer_->retire();
#endif
        const bool advanced = valid && marker != last_marker_;
        if (valid) last_marker_ = marker;
        return {{"connected", true}, {"ready", valid}, {"contentAdvanced", advanced}, {"marker", marker}, {"pixelHash", hash},
                {"side", width_}, {"format", format_ == DXGI_FORMAT_R8G8B8A8_UNORM ? "RGBA8" : "BGRA8"},
                {"generation", generation_}, {"appliedBgfxFrame", applied_frame}, {"stats", stats_.report()}};
    }
    std::string role_, name_;
#ifndef MAPGYVER_REFERENCE_SPOUT
    std::unique_ptr<mapgyver::windows::BgfxProbeDevice> renderer_;
#endif
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    DXGI_ADAPTER_DESC1 adapter_{};
    std::unique_ptr<spoutDX> sdk_;
    std::unique_ptr<MutexLease> held_;
    ComPtr<ID3D11Texture2D> texture_, staging_;
    UINT width_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    HANDLE observed_handle_ = nullptr;
    std::uint32_t last_marker_ = 0;
    std::uint64_t generation_ = 0;
    Stats stats_;
};
json source_info() {
#ifdef MAPGYVER_REFERENCE_SPOUT
    return {{"sourceRevision", mapgyver::source_known ? json(mapgyver::source_revision) : json(nullptr)},
            {"sourceDirty", mapgyver::source_known ? json(mapgyver::source_dirty) : json(nullptr)}, {"buildConfiguration", "Release"}};
#else
    const auto build = mapgyver::build_info();
    return {{"sourceRevision", build.revision.empty() ? json(nullptr) : json(build.revision)},
            {"sourceDirty", build.dirty.has_value() ? json(*build.dirty) : json(nullptr)}, {"buildConfiguration", build.configuration}};
#endif
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view{argv[1]} == "--version") {
            std::cout << "MapGyver M01 Spout participant SDK " << MAPGYVER_SPOUT_SDK_VERSION << '\n';
            return 0;
        }
        if (argc != 5 || std::string_view{argv[1]} != "--role" || std::string_view{argv[3]} != "--name") {
            std::cerr << "Usage: spout-participant --role <sender|receiver> --name <isolated-name>\n";
            return 2;
        }
        const std::string role{argv[2]}, name{argv[4]};
        check(role == "sender" || role == "receiver", "Invalid participant role");
        check(!name.empty() && name.size() <= 120 && name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos,
              "Invalid isolated sender name");
        Participant participant{role, name};
        auto info = participant.info(); info.update(source_info());
        std::cout << info.dump() << std::endl;
        std::array<char, 4097> line{};
        while (std::cin.getline(line.data(), static_cast<std::streamsize>(line.size()))) {
            const auto request = json::parse(line.data());
            if (request.at("op") == "quit") { std::cout << "{\"stopped\":true}" << std::endl; return 0; }
            const auto started = std::chrono::steady_clock::now();
            auto response = participant.command(request);
            response["controlCallMicroseconds"] = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
            std::cout << response.dump() << std::endl;
        }
        check(std::cin.eof(), "Command exceeded the 4096-byte capacity or input failed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Spout participant failed: " << error.what() << '\n';
        return 1;
    }
}
