// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Adapter-private SDK surface. Never include this from domain/control/protocol.
#include <mapgyver/render/plan.hpp>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace mapgyver::windows {
struct PreparedImage {
    domain::Id resource;
    std::uint64_t generation;
    std::uint32_t width, height;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
};
// Device resource creation is thread safe. Its immediate context and every bgfx
// call still belong exclusively to the render owner. Destroy preparation jobs
// before releasing their device; PreparedImage owns its texture reference.
class PreparationDevice {
public:
    PreparationDevice();
    [[nodiscard]] ID3D11Device* native() const noexcept {return device_.Get();}
    [[nodiscard]] const DXGI_ADAPTER_DESC1& identity() const noexcept {return identity_;}
    [[nodiscard]] D3D11_FEATURE_DATA_THREADING threading() const noexcept {return threading_;}
    // Preparation only. Initial data is a CPU upload, not a reported GPU copy.
    // Callers bound aggregate retained bytes and concurrent jobs separately.
    [[nodiscard]] PreparedImage prepare(const render::Image& image) const;
private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    DXGI_ADAPTER_DESC1 identity_{};
    D3D11_FEATURE_DATA_THREADING threading_{};
};
}
