// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "preparation_device.hpp"
#include <bgfx/bgfx.h>

namespace mapgyver::windows {
// Adapter-private state. Native allocations happen on preparation; bgfx handles
// and views are initialized/destroyed only by their owner in bounded steps.
struct GraphTexture {
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> native;
    bool borrowed = false;
};
struct GraphInput {bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;PreparedImage image;};
struct GraphResources {
    std::shared_ptr<const render::Plan> plan;
    std::vector<GraphTexture> targets, exports;
    std::vector<bgfx::TextureHandle> inputs;
    std::map<std::pair<domain::Id,std::uint64_t>,GraphInput> images;
    std::vector<std::uint32_t> exported_nodes;
    std::vector<bgfx::ViewId> views;
    std::uint32_t render_views = 0;
    std::uint64_t owned_payload_bytes = 0;
    void swap(GraphResources& other) noexcept;
};
struct GraphLimits {
    std::uint32_t dimension, views, textures, framebuffers, draws;
    std::size_t active_textures, active_framebuffers, windows;
    std::uint64_t active_payload, output_payload;
};
class PreparedGraph {
public:
    [[nodiscard]] std::uint64_t generation_id() const noexcept {return generation;}
private:
    friend class CompositionRenderer;
    friend std::unique_ptr<PreparedGraph> prepare_graph(const PreparationDevice&,std::shared_ptr<const render::Plan>,
        std::uint64_t,GraphLimits,std::vector<std::uint32_t>);
    PreparedGraph(std::uint64_t id) : generation(id) {}
    struct Binding {
        bgfx::TextureHandle* handle = nullptr;
        bgfx::FrameBufferHandle* framebuffer = nullptr;
        ID3D11Texture2D* native = nullptr;
        bgfx::TextureFormat::Enum format = bgfx::TextureFormat::Unknown;
        std::uint16_t width = 0, height = 0;
    };
    GraphResources resources;
    std::vector<Binding> bindings;
    std::vector<std::uint32_t> output_nodes;
    Microsoft::WRL::ComPtr<ID3D11Query> completion;
    Microsoft::WRL::ComPtr<ID3D11Device> owner_device;
    std::uint64_t generation, last_step_frame = UINT64_MAX, destroyed_frame = UINT64_MAX;
    std::size_t binding_count = 0, cursor = 0;
    bool imported = false, retiring = false, completion_observed = false;
    void list_bindings(); // Uses existing allocated slots; no allocation on owner.
};
[[nodiscard]] bool same_graph_layout(const render::Plan& first,const render::Plan& second);
[[nodiscard]] std::unique_ptr<PreparedGraph> prepare_graph(const PreparationDevice& device,
    std::shared_ptr<const render::Plan> plan,std::uint64_t generation,GraphLimits limits,std::vector<std::uint32_t> output_nodes);
}
