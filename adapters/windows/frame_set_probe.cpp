// SPDX-License-Identifier: GPL-3.0-or-later
// Offline cooperative frame-set experiment; native resources remain adapter-private.
#include "bgfx_probe_device.hpp"
#include <mapgyver/build_info.hpp>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
using nlohmann::json;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void require(HRESULT status, const char* operation) {
    if (FAILED(status)) {
        std::ostringstream error; error << operation << " HRESULT=0x" << std::hex << static_cast<unsigned long>(status);
        throw std::runtime_error(error.str());
    }
}
void complete(ID3D11Device* device, ID3D11DeviceContext* context) {
    D3D11_QUERY_DESC description{D3D11_QUERY_EVENT, 0};
    ComPtr<ID3D11Query> query;
    require(device->CreateQuery(&description, &query), "Create common completion query");
    context->End(query.Get()); context->Flush();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;) {
        BOOL done = FALSE;
        const auto status = context->GetData(query.Get(), &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        require(status, "Common completion query");
        if (status == S_OK && done) break;
        check(std::chrono::steady_clock::now() < deadline, "Common frame-set GPU completion timed out");
        std::this_thread::yield();
    }
    require(device->GetDeviceRemovedReason(), "Frame-set device health");
}
std::uint32_t pixel(UINT x, UINT y, std::uint32_t generation, std::uint32_t frame, std::uint32_t port) {
    return (generation * 100000U + frame * 256U + port) ^ (x * 0x9e3779b9U) ^ (y * 0x85ebca6bU);
}
json run(bool stale_member, bool missing_member) {
    mapgyver::windows::BgfxProbeDevice renderer;
    auto* device = renderer.device();
    ComPtr<ID3D11DeviceContext> context; device->GetImmediateContext(&context);
    ComPtr<IDXGIDevice> dxgi; require(device->QueryInterface(IID_PPV_ARGS(&dxgi)), "Frame-set DXGI device");
    ComPtr<IDXGIAdapter> base; require(dxgi->GetAdapter(&base), "Frame-set adapter");
    ComPtr<IDXGIAdapter3> adapter; require(base.As(&adapter), "Frame-set memory adapter");
    DXGI_ADAPTER_DESC identity{}; require(base->GetDesc(&identity), "Frame-set adapter identity");
    json sets = json::array();
    std::uint32_t generation = 0, sequence = 0;
    constexpr std::array counts{2U,5U,11U,2U};
    constexpr std::array sizes{64U,128U,256U,64U};
    for (std::size_t phase = 0; phase < counts.size(); ++phase) {
        ++generation;
        const auto count = counts[phase], side = sizes[phase];
        // Fixed test capacity only; no wall/floor/two-output assumption in the model.
        std::array<ComPtr<ID3D11Texture2D>, 12> sources, staging;
        std::array<ID3D11Texture2D*, 12> inputs{};
        for (UINT port = 0; port < count; ++port) {
            D3D11_TEXTURE2D_DESC description{};
            description.Width = side; description.Height = side; description.MipLevels = 1; description.ArraySize = 1;
            description.Format = port % 2 == 0 ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
            description.SampleDesc.Count = 1; description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            require(device->CreateTexture2D(&description, nullptr, &sources[port]), "Create independent source port texture");
            inputs[port] = sources[port].Get();
            description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0; description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            require(device->CreateTexture2D(&description, nullptr, &staging[port]), "Create port validation staging texture");
        }
        for (int repeat = 0; repeat < 2; ++repeat) {
            ++sequence;
            const auto started = std::chrono::steady_clock::now();
            std::vector<std::uint32_t> pixels(static_cast<std::size_t>(side)*side);
            for (UINT port = 0; port < count; ++port) {
                const auto source_sequence = stale_member && phase == 2 && repeat == 0 && port == count-1 ? sequence-1 : sequence;
                for (UINT y = 0; y < side; ++y)
                    for (UINT x = 0; x < side; ++x) pixels[static_cast<std::size_t>(y)*side+x] = pixel(x,y,generation,source_sequence,port);
                context->UpdateSubresource(sources[port].Get(), 0, nullptr, pixels.data(), side*4U, 0);
            }
            complete(device, context.Get()); // Cooperating producer finishes every member before publication.
            if (missing_member && phase == 0 && repeat == 0) inputs[1] = nullptr;
            const auto sampled = renderer.sample_set(std::span{inputs.data(), count});
            check(sampled.count == count && sampled.applied_frame > 0, "Frame set is incomplete");
            for (UINT port = 0; port < count; ++port) {
                check(sampled.members[port].applied_frame == sampled.applied_frame, "Frame set used different application frames");
                context->CopyResource(staging[port].Get(), sampled.members[port].texture);
            }
            complete(device, context.Get()); // This one completion covers every member's sampling/copy.
            json ports = json::array();
            bool exact = true;
            for (UINT port = 0; port < count; ++port) {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                require(context->Map(staging[port].Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map frame-set validation pixels");
                bool member_exact = true;
                std::uint32_t hash = 2166136261U;
                for (UINT y = 0; y < side; ++y) {
                    const auto* bytes = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y)*mapped.RowPitch;
                    const auto* row = reinterpret_cast<const std::uint32_t*>(bytes);
                    for (UINT x = 0; x < side; ++x) member_exact = member_exact && row[x] == pixel(x,y,generation,sequence,port);
                    for (UINT x = 0; x < side*4U; ++x) hash = (hash ^ bytes[x])*16777619U;
                }
                context->Unmap(staging[port].Get(), 0);
                exact = exact && member_exact;
                ports.push_back({{"portId","port-"+std::to_string(port)}, {"resourceGeneration",generation},
                    {"frameSequence",sequence}, {"appliedBgfxFrame",sampled.members[port].applied_frame},
                    {"format",port % 2 == 0 ? "RGBA8" : "BGRA8"}, {"side",side},
                    {"pixelHash",hash}, {"pixelsAndAlphaMatchExactly",member_exact}});
            }
            renderer.retire(); // Every member retires together after the common completion.
            check(exact, "Frame-set member pixel identity mismatch");
            DXGI_QUERY_VIDEO_MEMORY_INFO local{};
            require(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local), "Frame-set video memory query");
            sets.push_back({{"sourceInstanceId","cooperating-probe"}, {"generation",generation},
                {"frameSequence",sequence}, {"pts",sequence-1}, {"timeBase",{1,30}}, {"requiredPorts",count},
                {"appliedBgfxFrame",sampled.applied_frame}, {"completeAfterGpuCompletion",true},
                {"sourceCpuUploadCalls",count}, {"samplingDrawSubmissions",count},
                {"validationGpuCopySubmissions",count}, {"validationCpuReadbacks",count},
                {"localVideoUsageBytes",local.CurrentUsage}, {"localVideoBudgetBytes",local.Budget},
                {"offlineMicroseconds",std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count()},
                {"ports",ports}});
        }
    }
    const auto build = mapgyver::build_info();
    return {{"schemaVersion",1}, {"evidenceClass","native-gpu-cooperating-bgfx-frame-sets"}, {"result","passed"},
        {"sourceRevision",build.revision}, {"sourceDirty",build.dirty.has_value() ? json(*build.dirty) : json(nullptr)},
        {"buildConfiguration",build.configuration},
        {"adapter",{{"vendorId",identity.VendorId},{"deviceId",identity.DeviceId},
                    {"luidHigh",identity.AdapterLuid.HighPart},{"luidLow",identity.AdapterLuid.LowPart}}},
        {"ownership","All source/native targets retained through common GPU completion and group retirement"},
        {"counterMeaning","Application API submissions; validation readbacks separate; not hardware transfer counters"},
        {"physicalOutputs","not_tested"}, {"physicalScanoutSynchronization","not_claimed"},
        {"timingMeaning","Offline wall time includes upload/allocation/readback; not a show deadline budget"}, {"sets",sets}};
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view{argv[1]} == "--version") {
            std::cout << "MapGyver M01 cooperative native frame-set probe\n"; return 0;
        }
        check(argc >= 2 && std::string_view{argv[1]} == "--json", "Usage: frame-set-probe --json [--stale-member|--missing-member]");
        bool stale = false, missing = false;
        for (int index = 2; index < argc; ++index) {
            if (std::string_view{argv[index]} == "--stale-member") stale = true;
            else if (std::string_view{argv[index]} == "--missing-member") missing = true;
            else throw std::runtime_error("Unknown frame-set probe argument");
        }
        std::cout << run(stale,missing).dump(2) << '\n'; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Frame-set probe failed: " << error.what() << '\n'; return 1;
    }
}
