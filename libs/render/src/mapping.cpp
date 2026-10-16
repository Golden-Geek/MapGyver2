// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/render/plan.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace mapgyver::render {
namespace {
using domain::Vec2;
void require(bool value,const char* reason) {if (!value) throw Error(reason);}
double cross(Vec2 a,Vec2 b,Vec2 c) {return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
void convex(const std::array<Vec2,4>& points) {
    double winding = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        require(std::isfinite(points[i].x) && std::isfinite(points[i].y),"Homography controls must be finite");
        const auto turn = cross(points[i],points[(i+1)%4],points[(i+2)%4]);
        require(std::isfinite(turn) && turn != 0 && (winding == 0 || std::signbit(turn) == std::signbit(winding)),"Homography boundary must be convex and ordered");winding = turn;
    }
}
Matrix3 square(const std::array<Vec2,4>& p) {
    convex(p);
    const auto dx1 = p[1].x-p[2].x, dx2 = p[3].x-p[2].x, dx3 = p[0].x-p[1].x+p[2].x-p[3].x;
    const auto dy1 = p[1].y-p[2].y, dy2 = p[3].y-p[2].y, dy3 = p[0].y-p[1].y+p[2].y-p[3].y;
    double g = 0, h = 0;
    if (dx3 != 0 || dy3 != 0) {
        const auto determinant = dx1*dy2-dx2*dy1;
        require(std::isfinite(determinant) && determinant != 0,"Homography is singular");
        g = (dx3*dy2-dx2*dy3)/determinant;h = (dx1*dy3-dx3*dy1)/determinant;
    }
    return {p[1].x-p[0].x+g*p[1].x,p[3].x-p[0].x+h*p[3].x,p[0].x,
        p[1].y-p[0].y+g*p[1].y,p[3].y-p[0].y+h*p[3].y,p[0].y,g,h,1};
}
Matrix3 inverse(const Matrix3& m) {
    Matrix3 result{m[4]*m[8]-m[5]*m[7],m[2]*m[7]-m[1]*m[8],m[1]*m[5]-m[2]*m[4],
        m[5]*m[6]-m[3]*m[8],m[0]*m[8]-m[2]*m[6],m[2]*m[3]-m[0]*m[5],
        m[3]*m[7]-m[4]*m[6],m[1]*m[6]-m[0]*m[7],m[0]*m[4]-m[1]*m[3]};
    const auto determinant = m[0]*result[0]+m[1]*result[3]+m[2]*result[6];
    require(std::isfinite(determinant) && determinant != 0,"Homography is singular");
    for (auto& value : result) value /= determinant;
    return result;
}
Matrix3 multiply(const Matrix3& a,const Matrix3& b) {
    Matrix3 result{};
    for (std::size_t r = 0; r < 3; ++r) for (std::size_t c = 0; c < 3; ++c)
        for (std::size_t k = 0; k < 3; ++k) result[r*3+c] += a[r*3+k]*b[k*3+c];
    return result;
}
constexpr std::array<Vec2,4> unit{{{0,0},{1,0},{1,1},{0,1}}};
template<class V,class F> bool unit_quad_topology(const std::vector<V>& vertices,const std::vector<domain::Triangle>& triangles,F position) {
    if (vertices.size() != 4 || triangles.size() != 2) return false;
    std::map<domain::Id,std::size_t> indices;std::array<unsigned,4> uses{};
    for (const auto& vertex : vertices) {
        const auto found = std::find(unit.begin(),unit.end(),position(vertex));if (found == unit.end()) return false;
        const auto index = static_cast<std::size_t>(found-unit.begin());if (++uses[index] != 1) return false;
        indices.emplace(vertex.id,index);
    }
    std::map<std::pair<std::size_t,std::size_t>,unsigned> edges;
    for (const auto& triangle : triangles) for (std::size_t i = 0; i < 3; ++i) {
        const auto a = indices.find(triangle.vertices[i]),b = indices.find(triangle.vertices[(i+1)%3]);
        if (a == indices.end() || b == indices.end() || a == b) return false;
        ++edges[{std::min(a->second,b->second),std::max(a->second,b->second)}];
    }
    if (edges.size() != 5) return false;
    for (std::size_t i = 0; i < 4; ++i) {const auto j = (i+1)%4;const auto found = edges.find({std::min(i,j),std::max(i,j)});if (found == edges.end() || found->second != 1) return false;}
    return true;
}
Matrix3 chart(const domain::Stage& stage,const domain::Id& surface) {
    const domain::SurfaceChart* selected = nullptr;
    for (const auto& [id,layout] : stage.layouts) for (const auto& item : layout.charts) {
        static_cast<void>(id);if (item.surface != surface) continue;
        require(!selected,"surface_layout_ambiguous");selected = &item;
    }
    std::array<Vec2,4> target = unit;
    if (selected) {
        require(unit_quad_topology(selected->vertices,selected->triangles,[](const auto& vertex){return vertex.surface_uv;}),"surface_chart_quad_unavailable");
        for (std::size_t i = 0; i < 4; ++i) {
            const auto found = std::find_if(selected->vertices.begin(),selected->vertices.end(),[&](const auto& vertex){return vertex.surface_uv == unit[i];});
            require(found != selected->vertices.end(),"surface_chart_quad_unavailable");target[i] = found->composition_uv;
        }
    }
    return homography(unit,target);
}
PatchMapping prepare(const domain::ProjectionPatch& patch,const domain::Stage& stage,const Plan& plan) {
    require(patch.calibration.mode == domain::WarpMode::homography,"projection_mesh_unavailable");
    const auto& geometry = stage.surfaces.at(patch.surface).geometry;
    require(unit_quad_topology(geometry.vertices,geometry.triangles,[](const auto& vertex){return vertex.uv;}),"surface_geometry_quad_unavailable");
    const auto source = std::find_if(plan.surfaces.begin(),plan.surfaces.end(),[&](const auto& item){return item.id == patch.surface;});
    require(source != plan.surfaces.end(),"surface_content_unavailable");
    std::array<Vec2,4> pixels{}, uv{};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto found = std::find_if(patch.calibration.vertices.begin(),patch.calibration.vertices.end(),[&](const auto& vertex){return vertex.id == patch.calibration.boundary.at(i);});
        require(found != patch.calibration.vertices.end(),"projection_control_unavailable");
        pixels[i] = {found->projector_pixels.x/patch.calibration.reference_width,found->projector_pixels.y/patch.calibration.reference_height};uv[i] = found->surface_uv;
        require(uv[i].x >= 0 && uv[i].x <= 1 && uv[i].y >= 0 && uv[i].y <= 1,"projection_surface_uv_unavailable");
    }
    const auto chart_matrix = chart(stage,patch.surface);
    const auto matrix = multiply(chart_matrix,homography(pixels,uv));
    PatchMapping result{patch.id,source->node};
    double scale = 0;for (const auto value : matrix) scale = std::max(scale,std::abs(value));
    require(std::isfinite(scale) && scale > 0,"Homography is not finite");
    for (std::size_t r = 0; r < 3; ++r) for (std::size_t c = 0; c < 3; ++c) {
        result.rows[r][c] = static_cast<float>(matrix[r*3+c]/scale);
        require(std::isfinite(result.rows[r][c]),"Homography exceeds native float capacity");
    }
    const double winding = cross(pixels[0],pixels[1],pixels[2]) > 0 ? 1 : -1;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto a = pixels[i], b = pixels[(i+1)%4];
        auto& edge = result.edges[i];edge = {static_cast<float>((a.y-b.y)*winding),static_cast<float>((b.x-a.x)*winding),static_cast<float>((a.x*b.y-a.y*b.x)*winding),0};
        require(edge[0] != 0 || edge[1] != 0,"projection_precision_unavailable");
        // Top/left boundaries include a pixel centre; bottom/right exclude it.
        // Opposite shared edges therefore never blend a seam pixel twice.
        edge[3] = edge[0] > 0 || (edge[0] == 0 && edge[1] > 0) ? 1.F : 0.F;
        for (const auto value : result.edges[i]) require(std::isfinite(value),"Projection boundary exceeds native float capacity");
    }
    float previous_w = 0;
    for (const auto point : pixels) {
        const auto w = result.rows[2][0]*static_cast<float>(point.x)+result.rows[2][1]*static_cast<float>(point.y)+result.rows[2][2];
        require(std::isfinite(w) && w != 0 && (previous_w == 0 || std::signbit(w) == std::signbit(previous_w)),"projection_precision_unavailable");previous_w = w;
    }
    result.identity = pixels == unit && uv == unit && chart_matrix == Matrix3{1,0,0,0,1,0,0,0,1};
    return result;
}
}
Matrix3 homography(const std::array<Vec2,4>& from,const std::array<Vec2,4>& to) {
    auto result = multiply(square(to),inverse(square(from)));
    for (const auto value : result) require(std::isfinite(value),"Homography coefficients overflow");
    return result;
}
Vec2 transform(const Matrix3& matrix,Vec2 point) {
    const auto w = matrix[6]*point.x+matrix[7]*point.y+matrix[8];
    require(std::isfinite(w) && w != 0,"Homography point lies on its horizon");
    const Vec2 result{(matrix[0]*point.x+matrix[1]*point.y+matrix[2])/w,(matrix[3]*point.x+matrix[4]*point.y+matrix[5])/w};
    require(std::isfinite(result.x) && std::isfinite(result.y),"Homography point overflows");return result;
}
std::vector<OutputMapping> compile_mappings(const control::ProjectState& project,const Plan& plan,std::size_t patch_limit) {
    const auto& stage = project.stage.data();require(stage.patches.size() <= patch_limit,"Projection patches exceed render capacity");
    std::vector<OutputMapping> result;result.reserve(stage.outputs.size());std::map<domain::Id,std::size_t> indices;
    for (const auto& [id,output] : stage.outputs) {static_cast<void>(output);indices.emplace(id,result.size());result.push_back({id,{},{}});}
    for (const auto& [id,patch] : stage.patches) {
        static_cast<void>(id);auto& output = result.at(indices.at(patch.output));
        try {output.patches.push_back(prepare(patch,stage,plan));}
        catch (const Error& error) {if (output.reason.empty()) output.reason = error.what();}
    }
    return result;
}
std::optional<std::array<std::uint32_t,2>> mapped_pixel(const PatchMapping& patch,std::uint32_t x,std::uint32_t y,
    std::uint32_t width,std::uint32_t height,std::uint32_t source_width,std::uint32_t source_height) {
    require(width && height && source_width && source_height && x < width && y < height,"Mapped pixel dimensions/index invalid");
    if (patch.identity) return std::array{static_cast<std::uint32_t>((static_cast<std::uint64_t>(x)*2+1)*source_width/(static_cast<std::uint64_t>(width)*2)),
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(y)*2+1)*source_height/(static_cast<std::uint64_t>(height)*2))};
    const auto px = (static_cast<float>(x)+.5F)/static_cast<float>(width), py = (static_cast<float>(y)+.5F)/static_cast<float>(height);
    for (const auto& edge : patch.edges) {
        const auto coverage = edge[0]*px+edge[1]*py+edge[2];
        if (coverage < 0 || (coverage == 0 && edge[3] == 0)) return {};
    }
    const auto dot = [&](const auto& row){return row[0]*px+row[1]*py+row[2];};
    const auto w = dot(patch.rows[2]);if (!std::isfinite(w) || w == 0) return {};
    const auto u = dot(patch.rows[0])/w, v = dot(patch.rows[1])/w;
    if (!(u >= 0 && u < 1 && v >= 0 && v < 1)) return {};
    return std::array{std::min(source_width-1,static_cast<std::uint32_t>(u*static_cast<float>(source_width))),
        std::min(source_height-1,static_cast<std::uint32_t>(v*static_cast<float>(source_height)))};
}
}
