// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace mapgyver::domain {
class Id;
struct Vec2 {double x = 0, y = 0;bool operator==(const Vec2&) const = default;};
enum class ContentFit { stretch, fit, fill };
enum class BlendMode { over, add, screen };
enum class MaskShape { none, rectangle, ellipse };
struct LayerVisual {
    double opacity = 1;
    BlendMode blend = BlendMode::over;
    bool operator==(const LayerVisual&) const = default;
};
struct SurfaceContent {
    Vec2 crop_minimum{0,0}, crop_maximum{1,1};
    ContentFit fit = ContentFit::stretch;
    double opacity = 1;
    bool operator==(const SurfaceContent&) const = default;
};
struct Mask {
    MaskShape shape = MaskShape::none;
    Vec2 minimum{0,0}, maximum{1,1}; // Surface UV, independent of projector pixels.
    double feather = 0; // Normalized inward fraction of mask bounds, 0..0.5.
    bool inverted = false;
    bool operator==(const Mask&) const = default;
};
struct PatchVisual {
    double opacity = 1;
    BlendMode blend = BlendMode::over;
    Mask mask{};
    bool operator==(const PatchVisual&) const = default;
};
void validate_visual(const LayerVisual& value,const Id& owner);
void validate_visual(const SurfaceContent& value,const Id& owner);
void validate_visual(const PatchVisual& value,const Id& owner);
}
