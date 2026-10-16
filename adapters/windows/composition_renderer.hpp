// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/render/plan.hpp>
#include <memory>

namespace mapgyver::windows {
struct RenderedFrame {std::uint64_t sequence, revision; domain::MediaTime show_time;};
// Provisional measured Windows renderer. SDK objects remain adapter-private.
// Construct/prewarm outside deadline operation; frame() allocates no app resources.
class CompositionRenderer {
public:
    explicit CompositionRenderer(std::shared_ptr<const render::Plan> plan);
    ~CompositionRenderer();
    CompositionRenderer(const CompositionRenderer&) = delete;
    CompositionRenderer& operator=(const CompositionRenderer&) = delete;
    [[nodiscard]] RenderedFrame frame();
    // Offline validation only: synchronous GPU completion and CPU readback.
    // Production previews must use a separate bounded asynchronous transfer path.
    [[nodiscard]] std::vector<std::uint8_t> capture_for_test(std::uint32_t node);
    [[nodiscard]] std::array<std::uint32_t,4> adapter_identity() const;
    [[nodiscard]] std::uint64_t submitted_draws() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
