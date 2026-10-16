// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
namespace mapgyver::network {
[[nodiscard]] std::string make_capability();
[[nodiscard]] bool capability_matches(std::string_view expected,std::string_view supplied) noexcept;
}
