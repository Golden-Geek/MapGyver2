// SPDX-License-Identifier: GPL-3.0-or-later
#include "preparation_device.hpp"
#include <cmath>
#include <map>

namespace mapgyver::windows {
namespace {
using Microsoft::WRL::ComPtr;
void check(bool value,const char* message) {if (!value) throw render::Error(message);}
void require(HRESULT value,const char* message) {check(SUCCEEDED(value),message);}
}
PreparationDevice::PreparationDevice() {
    ComPtr<IDXGIFactory1> factory;
    require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Create preparation device factory failed");
    ComPtr<IDXGIAdapter1> selected;
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;const auto result = factory->EnumAdapters1(index,&candidate);
        if (result == DXGI_ERROR_NOT_FOUND) break;
        require(result,"Enumerate preparation device adapters failed");
        DXGI_ADAPTER_DESC1 description{};require(candidate->GetDesc1(&description),"Read preparation device adapter failed");
        if (!(description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) && (!selected || description.DedicatedVideoMemory > identity_.DedicatedVideoMemory)) {
            selected = std::move(candidate);identity_ = description;
        }
    }
    check(selected != nullptr,"Native preparation requires an actual hardware GPU");
    constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
    // Deliberately omit SINGLETHREADED. No software fallback or debug-layer
    // availability fallback can turn a failed device into hardware evidence.
    D3D_FEATURE_LEVEL actual{};
    require(D3D11CreateDevice(selected.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels,2,D3D11_SDK_VERSION,&device_,&actual,nullptr),"Create owned thread-safe D3D11 hardware device failed");
    check(actual >= D3D_FEATURE_LEVEL_11_0 && !(device_->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED),
        "Native preparation device lacks required feature level/thread safety");
    require(device_->CheckFeatureSupport(D3D11_FEATURE_THREADING,&threading_,sizeof(threading_)),"Read actual device threading support failed");
    UINT support = 0;
    require(device_->CheckFormatSupport(DXGI_FORMAT_R32G32B32A32_FLOAT,&support),"Read prepared image format support failed");
    check((support&(D3D11_FORMAT_SUPPORT_TEXTURE2D|D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)) ==
        (D3D11_FORMAT_SUPPORT_TEXTURE2D|D3D11_FORMAT_SUPPORT_SHADER_SAMPLE),"Prepared linear image format unavailable");
}
PreparedImage PreparationDevice::prepare(const render::Image& image,bool copy_target) const {
    check(image.generation && image.width && image.height && image.width <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION &&
        image.height <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION,"Prepared native image generation/dimensions invalid");
    const auto count = static_cast<std::uint64_t>(image.width)*image.height;
    check(image.pixels && count <= render::Limits{}.image_bytes/sizeof(render::Pixel) && image.pixels->size() == count,
        "Prepared native image storage exceeds exact bounded payload");
    for (const auto& pixel : *image.pixels) check(std::isfinite(pixel.r) && std::isfinite(pixel.g) && std::isfinite(pixel.b) &&
        std::isfinite(pixel.a) && pixel.r >= 0 && pixel.r <= pixel.a && pixel.g >= 0 && pixel.g <= pixel.a &&
        pixel.b >= 0 && pixel.b <= pixel.a && pixel.a >= 0 && pixel.a <= 1,"Prepared native image is not finite premultiplied linear colour");
    D3D11_TEXTURE2D_DESC description{};
    description.Width = image.width;description.Height = image.height;description.MipLevels = 1;description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;description.SampleDesc.Count = 1;
    description.Usage = copy_target ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{image.pixels->data(),static_cast<UINT>(image.width*sizeof(render::Pixel)),0};
    PreparedImage result{image.resource,image.generation,image.width,image.height,{}};
    require(device_->CreateTexture2D(&description,&data,&result.texture),"Prepare immutable native image failed");
    return result;
}
std::unique_ptr<PreparedGeneration> prepare_generation(const PreparationDevice& device,const render::Plan& layout,
    std::shared_ptr<const render::Plan> next,std::uint64_t generation,std::uint64_t active_payload_bytes) {
    check(next && generation && next->nodes.size() == layout.nodes.size() && next->targets.size() == layout.targets.size() &&
        next->surfaces.size() == layout.surfaces.size() && next->preview == layout.preview,"Image handoff requires the allocated graph layout");
    for (std::size_t i = 0; i < layout.targets.size(); ++i) check(next->targets[i].width == layout.targets[i].width &&
        next->targets[i].height == layout.targets[i].height,"Image handoff cannot resize allocated targets");
    for (std::size_t i = 0; i < layout.surfaces.size(); ++i) check(next->surfaces[i].id == layout.surfaces[i].id &&
        next->surfaces[i].node == layout.surfaces[i].node,"Image handoff cannot reroute allocated surface exports");
    std::map<domain::Id,std::shared_ptr<const render::Image>> resources;
    std::uint64_t bytes = 0;std::size_t layers = 0;
    for (std::size_t i = 0; i < layout.nodes.size(); ++i) {
        const auto& old = layout.nodes[i];const auto& node = next->nodes[i];
        check(node.source == old.source && node.width == old.width && node.height == old.height && node.target == old.target &&
            static_cast<bool>(node.image) == static_cast<bool>(old.image),"Image handoff requires stable node identities/dimensions/storage");
        check(node.layers == old.layers,"Image handoff cannot rewire allocated dependency lifetimes/draw capacity");
        check(node.layers.size() <= render::Limits{}.layers-layers,"Image handoff exceeds bounded layer capacity");layers += node.layers.size();
        for (const auto child : node.layers) check(child < i,"Image handoff dependencies must precede their consumers");
        if (!node.image) continue;
        check(node.image->resource == old.image->resource && node.image->width == node.width && node.image->height == node.height,
            "Image handoff requires stable native image identities/dimensions");
        const auto [found,inserted] = resources.emplace(node.image->resource,node.image);
        check(inserted || found->second == node.image,"Image handoff requires one immutable generation per resource");
        if (inserted) {
            const auto size = static_cast<std::uint64_t>(node.width)*node.height*sizeof(render::Pixel);
            check(size <= 134217728-bytes,"Image handoff candidate exceeds 128 MiB payload capacity");bytes += size;
        }
    }
    check(resources.size() <= 256 && active_payload_bytes <= 536870912 && bytes <= 536870912-active_payload_bytes,
        "Image handoff exceeds resource count/combined payload capacity");
    auto result = std::unique_ptr<PreparedGeneration>{new PreparedGeneration{generation,std::move(next)}};
    result->images.reserve(resources.size());
    for (const auto& [id,image] : resources) {static_cast<void>(id);result->images.push_back(device.prepare(*image));}
    return result;
}
}
