// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/domain/stage.hpp>
#include <span>

namespace mapgyver::runtime {
struct Display {
    std::string endpoint, name; // Stable OS path, not an enumeration index.
    domain::DesiredOutputMode mode;
    std::int32_t x = 0, y = 0;
    std::string reason{}; // Detected capability constraint, never authored intent.
};
struct OutputCapability {
    std::string adapter;
    bool requires_display;
    std::uint32_t max_width, max_height;
};
struct OutputBinding {
    domain::OutputTarget desired;
    std::optional<Display> display;
    // Empty means eligible for native preparation, not already presented.
    std::string reason;
};
struct OutputLimits {std::size_t targets = 256, displays = 256, capabilities = 16;};
class OutputError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
// Preparation/control only. Exact desired identity; never auto-select a monitor.
// Retains every configured target, including unsupported/absent/ambiguous ones.
[[nodiscard]] std::vector<OutputBinding> bind_outputs(const domain::StageSnapshot& stage,
    std::span<const Display> displays,std::span<const OutputCapability> capabilities,OutputLimits limits = {});
} // namespace mapgyver::runtime
