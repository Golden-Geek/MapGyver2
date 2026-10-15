// SPDX-License-Identifier: GPL-3.0-or-later
// All SDK types stay in this adapter. This diagnostic has no render deadline.
#include "probe.hpp"
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if !defined(_M_X64)
#error The M01 baseline currently requires a native x64 MSVC target.
#endif

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
struct CloseHandleDeleter {
    void operator()(void* handle) const noexcept { if (handle != nullptr) CloseHandle(handle); }
};
using SharedHandle = std::unique_ptr<void, CloseHandleDeleter>;
struct Device {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL feature_level{};
};
Device create_device(IDXGIAdapter* adapter) {
    Device result;
    constexpr std::array levels{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    require(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                             levels.data(), static_cast<UINT>(levels.size()), D3D11_SDK_VERSION,
                             &result.device, &result.feature_level, &result.context), "D3D11CreateDevice");
    return result;
}
std::string utf8(const wchar_t* value) {
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("Adapter name conversion failed");
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), size, nullptr, nullptr) == 0)
        throw std::runtime_error("Adapter name conversion failed");
    result.pop_back();
    return result;
}
void wait_for_gpu(const Device& device) {
    const D3D11_QUERY_DESC description{D3D11_QUERY_EVENT, 0};
    ComPtr<ID3D11Query> completion;
    require(device.device->CreateQuery(&description, &completion), "CreateQuery");
    device.context->End(completion.Get());
    device.context->Flush();
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;) {
        BOOL done = FALSE;
        const auto status = device.context->GetData(completion.Get(), &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        require(status, "GPU completion query");
        if (status == S_OK && done) return;
        if (std::chrono::steady_clock::now() >= limit) throw std::runtime_error("GPU completion exceeded two seconds");
        std::this_thread::yield();
    }
}
class KeyLease {
public:
    KeyLease(IDXGIKeyedMutex* mutex, UINT64 acquire, UINT64 release) : mutex_(mutex), release_(release) {
        // WAIT_TIMEOUT and WAIT_ABANDONED are nonnegative; SUCCEEDED alone is wrong.
        const auto status = mutex_->AcquireSync(acquire, 1000);
        if (status != S_OK) throw std::runtime_error("Keyed mutex acquire timed out, was abandoned, or failed");
    }
    KeyLease(const KeyLease&) = delete;
    KeyLease& operator=(const KeyLease&) = delete;
    ~KeyLease() { if (active_) mutex_->ReleaseSync(release_); }
    void release() {
        require(mutex_->ReleaseSync(release_), "Keyed mutex release");
        active_ = false;
    }
private:
    IDXGIKeyedMutex* mutex_;
    UINT64 release_;
    bool active_ = true;
};

nlohmann::json run_case(const Device& producer, const Device& consumer, UINT side, DXGI_FORMAT format, UINT generation) {
    UINT cpu_uploads = 0, gpu_copies = 0, cpu_readbacks = 0;
    D3D11_TEXTURE2D_DESC description{};
    description.Width = side;
    description.Height = side;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    description.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    ComPtr<ID3D11Texture2D> source;
    require(producer.device->CreateTexture2D(&description, nullptr, &source), "Create shared texture");
    ComPtr<IDXGIResource1> resource;
    require(source.As(&resource), "Shared resource interface");
    HANDLE raw_handle = nullptr;
    require(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                        nullptr, &raw_handle), "CreateSharedHandle");
    SharedHandle handle{raw_handle};
    ComPtr<ID3D11Device1> consumer1;
    require(consumer.device.As(&consumer1), "D3D11.1 consumer interface");
    ComPtr<ID3D11Texture2D> imported;
    require(consumer1->OpenSharedResource1(handle.get(), IID_PPV_ARGS(&imported)), "OpenSharedResource1");
    handle.reset(); // Opened COM objects, not the temporary NT handle, own subsequent lifetime.
    ComPtr<IDXGIKeyedMutex> producer_mutex, consumer_mutex;
    require(source.As(&producer_mutex), "Producer keyed mutex");
    require(imported.As(&consumer_mutex), "Consumer keyed mutex");
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(side) * side);
    for (UINT y = 0; y < side; ++y)
        for (UINT x = 0; x < side; ++x)
            pixels[static_cast<std::size_t>(y) * side + x] = 0xff000000U | (generation << 16U) | (y << 8U) | x;
    {
        KeyLease lease{producer_mutex.Get(), 0, 1};
        producer.context->UpdateSubresource(source.Get(), 0, nullptr, pixels.data(), side * 4, 0);
        ++cpu_uploads;
        producer.context->Flush();
        lease.release();
    }
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.MiscFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    require(consumer.device->CreateTexture2D(&description, nullptr, &staging), "Create validation staging texture");
    KeyLease lease{consumer_mutex.Get(), 1, 0};
    consumer.context->CopyResource(staging.Get(), imported.Get());
    ++gpu_copies;
    wait_for_gpu(consumer); // Validation only: never a production render/audio deadline path.
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require(consumer.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map validation readback");
    ++cpu_readbacks;
    bool match = true;
    for (UINT y = 0; y < side; ++y) {
        const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
        for (UINT x = 0; x < side; ++x) match = match && row[x] == pixels[static_cast<std::size_t>(y) * side + x];
    }
    consumer.context->Unmap(staging.Get(), 0);
    lease.release();
    require(consumer.device->GetDeviceRemovedReason(), "Consumer device health");
    require(producer.device->GetDeviceRemovedReason(), "Producer device health");
    if (!match) throw std::runtime_error("Shared texture generation/pixel validation failed");
    return {{"generation", generation}, {"width", side}, {"height", side},
            {"format", format == DXGI_FORMAT_R8G8B8A8_UNORM ? "RGBA8" : "BGRA8"}, {"pixelsMatch", true},
            {"submittedCpuUploads", cpu_uploads}, {"submittedGpuCopies", gpu_copies}, {"validationCpuReadbacks", cpu_readbacks}};
}
} // namespace

nlohmann::json d3d11_shared_texture_baseline() {
    ComPtr<IDXGIFactory1> factory;
    require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
    ComPtr<IDXGIAdapter1> selected;
    DXGI_ADAPTER_DESC1 selected_desc{};
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        const auto status = factory->EnumAdapters1(index, &candidate);
        if (status == DXGI_ERROR_NOT_FOUND) break;
        require(status, "EnumAdapters1");
        DXGI_ADAPTER_DESC1 description{};
        require(candidate->GetDesc1(&description), "Adapter description");
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
            (!selected || description.DedicatedVideoMemory > selected_desc.DedicatedVideoMemory)) {
            selected = candidate;
            selected_desc = description;
        }
    }
    if (!selected) throw std::runtime_error("No hardware adapter; WARP cannot qualify this probe");
    const auto producer = create_device(selected.Get());
    const auto consumer = create_device(selected.Get());
    nlohmann::json cases = nlohmann::json::array();
    UINT generation = 0;
    for (const auto format : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM}) {
        UINT support = 0;
        require(producer.device->CheckFormatSupport(format, &support), "CheckFormatSupport");
        const UINT needed = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE | D3D11_FORMAT_SUPPORT_RENDER_TARGET;
        if ((support & needed) != needed) throw std::runtime_error("Required texture format capability missing");
        for (const UINT side : {64U, 128U, 256U}) cases.push_back(run_case(producer, consumer, side, format, ++generation));
    }
    ComPtr<IDXGIAdapter3> budget_adapter;
    require(selected.As(&budget_adapter), "Video memory budget interface");
    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
    require(budget_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory), "Video memory budget query");
    return {{"schemaVersion", 1}, {"evidenceClass", "native-gpu-shared-resource-baseline"},
            {"result", "passed"}, {"adapter", {{"name", utf8(selected_desc.Description)},
             {"vendorId", selected_desc.VendorId}, {"deviceId", selected_desc.DeviceId},
             {"luidHigh", selected_desc.AdapterLuid.HighPart}, {"luidLow", selected_desc.AdapterLuid.LowPart},
             {"software", false}, {"dedicatedVideoMemoryBytes", selected_desc.DedicatedVideoMemory}}},
            {"featureLevel", static_cast<UINT>(producer.feature_level)},
            {"videoMemory", {{"budgetBytes", memory.Budget}, {"currentUsageBytes", memory.CurrentUsage}}},
            {"counterMeaning", "API submissions in this probe, not hardware bus/copy-engine counters"},
            {"bgfxInterop", "not_run"}, {"spoutInterop", "not_run"}, {"physicalOutputs", "not_tested"},
            {"cases", std::move(cases)}};
}
} // namespace mapgyver::experiments
