// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/runtime/outputs.hpp>
namespace mapgyver::windows {
// Control/preparation only. Bounded topology-race retries; never a frame callback.
[[nodiscard]] std::vector<runtime::Display> detected_displays(std::size_t capacity = 256);
}
