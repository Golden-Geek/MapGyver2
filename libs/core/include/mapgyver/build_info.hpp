// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <optional>
#include <string_view>
namespace mapgyver {
struct BuildInfo {
    std::string_view product;
    std::string_view version;
    std::string_view revision;
    std::optional<bool> dirty;
    std::string_view compiler_id;
    std::string_view compiler_version;
    std::string_view configuration;
    std::string_view cmake_version;
    std::string_view target_os;
    std::string_view architecture;
};
[[nodiscard]] BuildInfo build_info() noexcept;
struct FeatureInfo {
    std::string_view name;
    bool configured;
    bool available;
    std::string_view reason;
};
[[nodiscard]] std::array<FeatureInfo, 5> feature_info() noexcept;
} // namespace mapgyver
