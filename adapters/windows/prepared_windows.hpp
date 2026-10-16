// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "composition_renderer.hpp"
#include "prepared_graph.hpp"
#include <SDL3/SDL.h>

namespace mapgyver::windows {
struct NativeOutput {
    OutputWindow configured;
    SDL_Window* window = nullptr;
    bgfx::FrameBufferHandle swapchain = BGFX_INVALID_HANDLE;
    GraphTexture mapped, held, presented;
    std::uint64_t held_frame = 0;
    bgfx::TextureHandle freeze_source = BGFX_INVALID_HANDLE;
    std::array<std::uint32_t,2> freeze_source_size{};
    bool show_pending = false;
};
class PreparedWindows {
public:
    [[nodiscard]] std::uint64_t generation_id() const noexcept {return generation;}
    [[nodiscard]] GraphLimits graph_limits() const noexcept {return combined_limits;}
    void controls(std::span<const OutputControls> values) {
        if (values.size() != outputs.size()) throw render::Error("Prepared window control count invalid");
        for (std::size_t i = 0; i < values.size(); ++i) outputs[i].configured.controls = values[i];
    }
private:
    friend class CompositionRenderer;
    friend std::unique_ptr<PreparedWindows> prepare_windows(const PreparationDevice&,const std::vector<OutputWindow>&,
        std::vector<OutputWindow>,std::uint64_t,GraphLimits);
    PreparedWindows(std::uint64_t id) : generation(id) {}
    std::vector<NativeOutput> outputs;
    std::vector<std::size_t> reuse;
    Microsoft::WRL::ComPtr<ID3D11Device> owner_device;
    Microsoft::WRL::ComPtr<ID3D11Query> completion;
    GraphLimits combined_limits{};
    std::uint64_t generation, last_step_frame = UINT64_MAX, destroyed_frame = UINT64_MAX;
    std::size_t cursor = 0;
    unsigned step = 0;
    bool retiring = false, fence_submitted = false, completion_observed = false, hide_pending = false;
};
[[nodiscard]] bool same_windows(const std::vector<OutputWindow>& first,const std::vector<OutputWindow>& second);
[[nodiscard]] std::unique_ptr<PreparedWindows> prepare_windows(const PreparationDevice& device,
    const std::vector<OutputWindow>& current,std::vector<OutputWindow> desired,std::uint64_t generation,GraphLimits limits);
}
