// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/render/plan.hpp>
#include <memory>

namespace mapgyver::windows {
class PreparationDevice;
class PreparedGeneration;
struct PreparedTarget;
class PreparedGraph;
class PreparedWindows;
struct GraphLimits;
struct ImageHandoffState {std::uint64_t generation, gpu_copy_submissions, copied_payload_bytes;};
struct RenderedFrame {std::uint64_t sequence, revision; domain::MediaTime show_time;};
struct OutputControls {bool enabled = true, blackout = false, freeze = false;};
struct OutputWindow {
    domain::Id id;
    std::string name;
    std::uint32_t width, height, node;
    bool hidden = false; // Explicit API-only qualification; never physical evidence.
    OutputControls controls;
    std::optional<std::array<std::int32_t,2>> desktop_position{};
};
// Provisional measured Windows renderer. SDK objects remain adapter-private.
// Construct/prewarm outside deadline operation; frame() allocates no app resources.
class CompositionRenderer {
public:
    explicit CompositionRenderer(std::shared_ptr<const render::Plan> plan,std::vector<OutputWindow> outputs = {},
        std::shared_ptr<PreparationDevice> device = {});
    ~CompositionRenderer();
    CompositionRenderer(const CompositionRenderer&) = delete;
    CompositionRenderer& operator=(const CompositionRenderer&) = delete;
    [[nodiscard]] RenderedFrame frame();
    // Offline validation only: synchronous GPU completion and CPU readback.
    // Production previews must use a separate bounded asynchronous transfer path.
    [[nodiscard]] std::vector<std::uint8_t> capture_for_test(std::uint32_t node);
    [[nodiscard]] std::array<std::uint32_t,4> adapter_identity() const;
    [[nodiscard]] std::uint64_t submitted_draws() const noexcept;
    // Same GPU owner thread, outside frame execution. No graph/calibration edits.
    void output_controls(const domain::Id& output,OutputControls controls);
    void apply_output_controls(std::uint64_t revision,std::span<const OutputControls> controls);
    [[nodiscard]] std::uint64_t held_frame(const domain::Id& output) const;
    [[nodiscard]] std::vector<std::uint8_t> capture_output_for_test(const domain::Id& output);
    // Fixed-layout image generations only. No resources are created here. A
    // busy copy keeps candidate ownership with the caller (bounded backpressure).
    [[nodiscard]] bool apply_images(std::unique_ptr<PreparedGeneration>& candidate);
    // One nonblocking GPU-completion poll. Return retired ownership to preparation
    // for destruction; do not destroy a large retired plan in the frame callback.
    [[nodiscard]] std::unique_ptr<PreparedGeneration> retire_images();
    [[nodiscard]] ImageHandoffState image_handoff_state() const;
    [[nodiscard]] std::uint64_t resource_payload_bytes() const;
    [[nodiscard]] std::uint64_t constant_upload_bytes() const noexcept;
    [[nodiscard]] GraphLimits graph_limits() const;
    // At most one placeholder/import/FB step per submitted frame. True transfers
    // candidate ownership and applies the whole graph at a frame boundary.
    [[nodiscard]] bool stage_graph(std::unique_ptr<PreparedGraph>& candidate);
    [[nodiscard]] bool stage_scene(std::unique_ptr<PreparedGraph>& graph,std::unique_ptr<PreparedWindows>& windows);
    void cancel_graph(std::unique_ptr<PreparedGraph>& candidate);
    void cancel_scene(std::unique_ptr<PreparedGraph>& graph,std::unique_ptr<PreparedWindows>& windows);
    // Actual old-GPU completion, then bounded owner handle destruction. Retired
    // native textures/CPU containers return for background disposal afterwards.
    [[nodiscard]] std::unique_ptr<PreparedGraph> retire_graph();
    [[nodiscard]] std::unique_ptr<PreparedWindows> retire_windows();
    // Offline adapter experiment. Keeps real old-scene frames running through
    // bounded handle realization/import/FB creation; captures only afterwards.
    [[nodiscard]] std::vector<std::uint8_t> qualify_borrowed_target(PreparedTarget target);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
