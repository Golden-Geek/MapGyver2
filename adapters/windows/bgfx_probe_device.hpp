// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>
struct ID3D11Device;
struct ID3D11Texture2D;

namespace mapgyver::windows {
// Private Windows-adapter boundary justified by the measured Spout path.
// This offline qualification utility is not the production render interface.
class BgfxProbeDevice {
public:
    struct Sample { ID3D11Texture2D* texture; std::uint32_t applied_frame; };
    BgfxProbeDevice();
    ~BgfxProbeDevice();
    BgfxProbeDevice(const BgfxProbeDevice&) = delete;
    BgfxProbeDevice& operator=(const BgfxProbeDevice&) = delete;
    [[nodiscard]] ID3D11Device* device() const noexcept;
    // The source is retained; returned native target is borrowed until retire().
    [[nodiscard]] Sample sample(ID3D11Texture2D* source);
    // Call only after downstream use has completed. Destruction remains safe on failure.
    void retire();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
