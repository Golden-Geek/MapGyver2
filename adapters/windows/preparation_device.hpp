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
    // Startup copy targets use DEFAULT usage; producer generations are immutable.
    // Callers bound aggregate retained bytes and concurrent jobs separately.
    [[nodiscard]] PreparedImage prepare(const render::Image& image,bool copy_target = false) const;
private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    DXGI_ADAPTER_DESC1 identity_{};
    D3D11_FEATURE_DATA_THREADING threading_{};
};
// A fixed-layout generation. Preparation owns all CPU/native allocations;
// application transfers ownership to the render owner until its copy completes.
class PreparedGeneration {
public:
    [[nodiscard]] std::uint64_t generation_id() const noexcept {return generation;}
    [[nodiscard]] std::size_t image_count() const noexcept {return images.size();}
private:
    friend class CompositionRenderer;
    friend std::unique_ptr<PreparedGeneration> prepare_generation(const PreparationDevice&,
        const render::Plan&,std::shared_ptr<const render::Plan>,std::uint64_t,std::uint64_t);
    PreparedGeneration(std::uint64_t id,std::shared_ptr<const render::Plan> next)
        : generation(id),plan(std::move(next)) {}
    std::uint64_t generation;
    std::shared_ptr<const render::Plan> plan;
    std::vector<PreparedImage> images;
    std::shared_ptr<const render::Plan> retired_plan;
};
// Background preparation only. Validates a compiled plan against the allocated
// layout; does not call bgfx or use an immediate context. Aggregate candidate
// payload is capped at 128 MiB and must fit beside the active resource payload.
[[nodiscard]] std::unique_ptr<PreparedGeneration> prepare_generation(const PreparationDevice& device,
    const render::Plan& layout,std::shared_ptr<const render::Plan> next,std::uint64_t generation,
    std::uint64_t active_payload_bytes);
}
