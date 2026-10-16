// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/domain/definitions.hpp>
#include <cmath>

namespace mapgyver::domain {
namespace {
void require(bool value,const Id& owner,const char* message) {if (!value) throw ModelError(ErrorCode::invalid_definition,message,{owner});}
void opacity(double value,const Id& owner) {require(std::isfinite(value) && value >= 0 && value <= 1,owner,"Visual opacity must be finite and within 0..1");}
void blend(BlendMode value,const Id& owner) {require(value == BlendMode::over || value == BlendMode::add || value == BlendMode::screen,owner,"Unknown visual blend mode");}
void bounds(Vec2 minimum,Vec2 maximum,const Id& owner) {
    require(std::isfinite(minimum.x) && std::isfinite(minimum.y) && std::isfinite(maximum.x) && std::isfinite(maximum.y) &&
        minimum.x >= 0 && minimum.y >= 0 && maximum.x <= 1 && maximum.y <= 1 && minimum.x < maximum.x && minimum.y < maximum.y,
        owner,"Visual UV bounds must be finite, nonempty and within 0..1");
}
}
void validate_visual(const LayerVisual& value,const Id& owner) {
    opacity(value.opacity,owner);blend(value.blend,owner);
    require(value.fit == ContentFit::stretch || value.fit == ContentFit::fit || value.fit == ContentFit::fill,owner,"Unknown layer fit mode");
}
void validate_visual(const SurfaceContent& value,const Id& owner) {
    bounds(value.crop_minimum,value.crop_maximum,owner);opacity(value.opacity,owner);
    require(value.fit == ContentFit::stretch || value.fit == ContentFit::fit || value.fit == ContentFit::fill,owner,"Unknown content fit mode");
}
void validate_visual(const PatchVisual& value,const Id& owner) {
    opacity(value.opacity,owner);blend(value.blend,owner);bounds(value.mask.minimum,value.mask.maximum,owner);
    require(value.mask.shape == MaskShape::none || value.mask.shape == MaskShape::rectangle || value.mask.shape == MaskShape::ellipse,owner,"Unknown mask shape");
    require(std::isfinite(value.mask.feather) && value.mask.feather >= 0 && value.mask.feather <= .5,owner,"Mask feather must be finite and within 0..0.5");
}
}
