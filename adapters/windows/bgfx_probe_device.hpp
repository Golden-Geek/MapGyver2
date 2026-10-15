// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <span>
struct ID3D11Device;
struct ID3D11Texture2D;

namespace mapgyver::windows {
// Private Windows-adapter boundary justified by the measured Spout path.
// This offline qualification utility is not the production render interface.
class BgfxProbeDevice {
public:
    struct Sample { ID3D11Texture2D* texture; std::uint32_t applied_frame; };
    // Bounded offline fixture capacity, not a product surface/output limit.
    struct SampleSet { std::array<Sample, 12> members{}; std::size_t count = 0; std::uint32_t applied_frame = 0; };
    BgfxProbeDevice();
    ~BgfxProbeDevice();
    BgfxProbeDevice(const BgfxProbeDevice&) = delete;
    BgfxProbeDevice& operator=(const BgfxProbeDevice&) = delete;
    [[nodiscard]] ID3D11Device* device() const noexcept;
    // The source is retained; returned native target is borrowed until retire().
    [[nodiscard]] Sample sample(ID3D11Texture2D* source);
    // All members are sampled in one applied bgfx frame and retire together.
    [[nodiscard]] SampleSet sample_set(std::span<ID3D11Texture2D* const> sources);
    // Call only after downstream use has completed. Destruction remains safe on failure.
    void retire();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
