// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/runtime/outputs.hpp>
#include <numeric>
#include <set>

namespace mapgyver::runtime {
namespace {
bool refresh_matches(domain::Rational a,domain::Rational b) {
    if (a.numerator <= 0 || a.denominator <= 0 || b.numerator <= 0 || b.denominator <= 0) return false;
    const auto ag = std::gcd(a.numerator,a.denominator), bg = std::gcd(b.numerator,b.denominator);
    return a.numerator/ag == b.numerator/bg && a.denominator/ag == b.denominator/bg;
}
}
std::vector<OutputBinding> bind_outputs(const domain::StageSnapshot& stage,std::span<const Display> displays,
    std::span<const OutputCapability> capabilities,OutputLimits limits) {
    if (limits.targets == 0 || stage.data().outputs.size() > limits.targets || displays.size() > limits.displays || capabilities.size() > limits.capabilities)
        throw OutputError("Output binding inventory exceeds configured capacity");
    std::set<std::string> adapters;
    for (const auto& capability : capabilities) if (capability.adapter.empty() || !adapters.insert(capability.adapter).second || !capability.max_width || !capability.max_height)
        throw OutputError("Output capability inventory is invalid or ambiguous");
    std::vector<OutputBinding> result;result.reserve(stage.data().outputs.size());
    for (const auto& [id,desired] : stage.data().outputs) {
        static_cast<void>(id); OutputBinding binding{desired,std::nullopt,{}};
        const OutputCapability* capability = nullptr;
        for (const auto& item : capabilities) if (item.adapter == desired.adapter) {capability = &item;break;}
        if (!capability) binding.reason = "output_adapter_unavailable";
        else if (!desired.enabled) binding.reason = "output_disabled";
        else if (desired.desired.width > capability->max_width || desired.desired.height > capability->max_height) binding.reason = "output_mode_exceeds_capacity";
        else if (capability->requires_display) {
            if (desired.endpoint.empty()) binding.reason = "output_unbound";
            else {
                const Display* selected = nullptr; std::size_t matches = 0;
                for (const auto& display : displays) if (display.endpoint == desired.endpoint) {selected = &display;++matches;}
                if (!matches) binding.reason = "output_display_absent";
                else if (matches != 1) binding.reason = "output_display_ambiguous";
                else {
                    binding.display = *selected;
                    if (selected->mode.width != desired.desired.width || selected->mode.height != desired.desired.height ||
                        !refresh_matches(selected->mode.refresh_hz,desired.desired.refresh_hz)) binding.reason = "output_display_mode_mismatch";
                }
            }
        }
        result.push_back(std::move(binding));
    }
    return result;
}
}
