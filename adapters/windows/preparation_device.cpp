// SPDX-License-Identifier: GPL-3.0-or-later
#include "preparation_device.hpp"
#include <cmath>

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
PreparedImage PreparationDevice::prepare(const render::Image& image) const {
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
    description.Usage = D3D11_USAGE_IMMUTABLE;description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{image.pixels->data(),static_cast<UINT>(image.width*sizeof(render::Pixel)),0};
    PreparedImage result{image.resource,image.generation,image.width,image.height,{}};
    require(device_->CreateTexture2D(&description,&data,&result.texture),"Prepare immutable native image failed");
    return result;
}
}
