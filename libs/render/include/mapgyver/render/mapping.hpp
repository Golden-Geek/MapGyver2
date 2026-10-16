// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/control/project.hpp>
#include <array>
#include <span>

namespace mapgyver::render {
using Matrix3 = std::array<double,9>;
// Preparation-only projective solve. Points are corresponding ordered convex
// boundaries. Coordinates and matrices never contain platform/GPU types.
[[nodiscard]] Matrix3 homography(const std::array<domain::Vec2,4>& from,const std::array<domain::Vec2,4>& to);
[[nodiscard]] domain::Vec2 transform(const Matrix3& matrix,domain::Vec2 point);
struct PatchMapping {
    domain::Id id;
    std::uint32_t node;
    // Canonical float rows supplied unchanged to the native shader/reference.
    std::array<std::array<float,4>,3> rows{};
    // Inward coverage equations, with w selecting inclusive top/left edges.
    std::array<std::array<float,4>,4> edges{};
    bool identity = false;
    bool operator==(const PatchMapping&) const = default;
};
struct OutputMapping {
    domain::Id id;
    std::vector<PatchMapping> patches; // Back-to-front stable patch-ID order.
    std::string reason;
    bool operator==(const OutputMapping&) const = default;
};
struct Plan;
[[nodiscard]] std::vector<OutputMapping> compile_mappings(const control::ProjectState& project,const Plan& plan,std::size_t patch_limit);
// Nonallocating canonical pixel-centre lookup. Outside coverage returns nullopt.
[[nodiscard]] std::optional<std::array<std::uint32_t,2>> mapped_pixel(const PatchMapping& patch,
    std::uint32_t x,std::uint32_t y,std::uint32_t width,std::uint32_t height,std::uint32_t source_width,std::uint32_t source_height);
}
