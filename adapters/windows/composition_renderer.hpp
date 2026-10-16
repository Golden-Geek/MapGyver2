// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/render/plan.hpp>
#include <memory>

namespace mapgyver::windows {
struct RenderedFrame {std::uint64_t sequence, revision; domain::MediaTime show_time;};
struct OutputControls {bool enabled = true, blackout = false, freeze = false;};
struct OutputWindow {
    domain::Id id;
    std::string name;
    std::uint32_t width, height, node;
    bool hidden = false; // Explicit API-only qualification; never physical evidence.
    OutputControls controls;
};
// Provisional measured Windows renderer. SDK objects remain adapter-private.
// Construct/prewarm outside deadline operation; frame() allocates no app resources.
class CompositionRenderer {
public:
    explicit CompositionRenderer(std::shared_ptr<const render::Plan> plan,std::vector<OutputWindow> outputs = {});
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
    [[nodiscard]] std::uint64_t held_frame(const domain::Id& output) const;
    [[nodiscard]] std::vector<std::uint8_t> capture_output_for_test(const domain::Id& output);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
