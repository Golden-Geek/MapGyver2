// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/domain/stage.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace mapgyver::domain {
namespace {
void require(bool valid, ErrorCode code, const char* message, std::vector<Id> involved = {}) {
    if (!valid) throw ModelError(code,message,std::move(involved));
}
void finite(Vec2 point, const Id& owner) {
    require(std::isfinite(point.x) && std::isfinite(point.y),ErrorCode::invalid_geometry,"Geometry coordinates must be finite",{owner});
}
double area(Vec2 a, Vec2 b, Vec2 c) { return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); }
template<class T> const T& find(const std::map<Id,T>& values, const Id& id) {
    const auto found = values.find(id);
    if (found == values.end()) throw ModelError(ErrorCode::missing_reference,"Stage reference is missing",{id});
    return found->second;
}
template<class Vertex, class Position, class Other>
std::map<Id,Vec2> mesh(const std::vector<Vertex>& vertices, const std::vector<Triangle>& triangles,
                       const Id& owner, Position position, Other other) {
    require(vertices.size() >= 3 && !triangles.empty(),ErrorCode::invalid_geometry,"Geometry needs vertices and topology",{owner});
    std::map<Id,Vec2> points;
    std::set<std::array<Id,3>> faces;
    for (const auto& vertex : vertices) {
        const auto point = position(vertex);
        finite(point,owner); finite(other(vertex),owner);
        require(points.emplace(vertex.id,point).second,ErrorCode::duplicate_id,"Duplicate geometry point ID",{owner,vertex.id});
    }
    for (const auto& triangle : triangles) {
        auto face = triangle.vertices;
        std::sort(face.begin(),face.end());
        require(faces.insert(face).second,ErrorCode::invalid_geometry,"Geometry repeats a triangle face",{owner});
        std::array<Vec2,3> p{};
        require(triangle.vertices[0] != triangle.vertices[1] && triangle.vertices[0] != triangle.vertices[2] &&
                triangle.vertices[1] != triangle.vertices[2],ErrorCode::invalid_geometry,"Triangle repeats a point",{owner});
        for (std::size_t index = 0; index < 3; ++index) {
            const auto found = points.find(triangle.vertices[index]);
            require(found != points.end(),ErrorCode::missing_reference,"Triangle point ID is missing",{owner,triangle.vertices[index]});
            p[index] = found->second;
        }
        const auto signed_area = area(p[0],p[1],p[2]);
        require(std::isfinite(signed_area) && signed_area != 0,ErrorCode::invalid_geometry,"Triangle is degenerate or overflows",{owner});
    }
    return points;
}
void convex_boundary(const std::vector<Id>& boundary, const std::map<Id,Vec2>& points, const Id& owner) {
    require(boundary.size() == 4 && std::set<Id>(boundary.begin(),boundary.end()).size() == 4,
            ErrorCode::invalid_geometry,"Homography boundary needs four distinct stable point IDs",{owner});
    int winding = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        for (const auto& id : {boundary[index],boundary[(index+1)%4],boundary[(index+2)%4]})
            require(points.contains(id),ErrorCode::missing_reference,"Homography boundary point is missing",{owner,id});
        const auto turn = area(points.at(boundary[index]),points.at(boundary[(index+1)%4]),points.at(boundary[(index+2)%4]));
        require(std::isfinite(turn) && turn != 0,ErrorCode::invalid_geometry,"Homography boundary is degenerate",{owner});
        const int sign = turn > 0 ? 1 : -1;
        require(winding == 0 || winding == sign,ErrorCode::invalid_geometry,"Homography boundary must be convex and ordered",{owner});
        winding = sign;
    }
}
std::pair<Id,Id> ordered(Id first, Id second) { return first < second ? std::pair{first,second} : std::pair{second,first}; }
void edge(const Stage& stage, const SurfaceGroup& group, const SurfaceEdge& value) {
    require(std::find(group.surfaces.begin(),group.surfaces.end(),value.surface) != group.surfaces.end(),
            ErrorCode::missing_reference,"Seam surface is not in its operational group",{group.id,value.surface});
    require(value.first != value.second,ErrorCode::invalid_geometry,"Seam edge repeats a point",{group.id,value.surface});
    const auto& geometry = find(stage.surfaces,value.surface).geometry;
    std::size_t uses = 0;
    for (const auto& triangle : geometry.triangles)
        for (std::size_t index = 0; index < 3; ++index)
            if (ordered(triangle.vertices[index],triangle.vertices[(index+1)%3]) == ordered(value.first,value.second)) ++uses;
    require(uses == 1,ErrorCode::invalid_geometry,"Seam must identify a boundary edge",{group.id,value.surface,value.first,value.second});
}
void validate(const Stage& stage, const InstancePool& instances, StageLimits limits) {
    require(stage.surfaces.size() <= limits.surfaces && stage.groups.size() <= limits.groups && stage.layouts.size() <= limits.layouts &&
            stage.outputs.size() <= limits.outputs && stage.patches.size() <= limits.patches,ErrorCode::capacity_exceeded,"Configured stage entity capacity exceeded");
    std::size_t vertices = 0, triangles = 0;
    const auto capacity = [&](std::size_t points, std::size_t faces) {
        require(points <= limits.vertices-vertices && faces <= limits.triangles-triangles,ErrorCode::capacity_exceeded,"Configured stage geometry capacity exceeded");
        vertices += points; triangles += faces;
    };
    std::set<Id> content_leases;
    const auto content = [&](const Surface& surface) -> std::optional<InstancePort> {
        if (!surface.content_placement) return std::nullopt;
        const auto& placement = find(instances.placements(),*surface.content_placement);
        const auto& source = instances.instance(placement.source.instance);
        require(std::holds_alternative<CompositionSource>(instances.definitions().asset(source.asset).source),
                ErrorCode::type_mismatch,"Surfaces consume composition content, not direct media/assets",{surface.id,source.id});
        require(instances.definitions().output({source.asset,placement.source.port}).kind == StreamKind::video,
                ErrorCode::type_mismatch,"Surface content must be a video composition port",{surface.id,source.id,placement.source.port});
        return placement.source;
    };
    for (const auto& [id,surface] : stage.surfaces) {
        require(id == surface.id,ErrorCode::invalid_definition,"Surface identity differs from map key",{id,surface.id});
        validate_display_name(surface.name,id);
        if (surface.content) validate_visual(*surface.content,id);
        capacity(surface.geometry.vertices.size(),surface.geometry.triangles.size());
        static_cast<void>(mesh(surface.geometry.vertices,surface.geometry.triangles,id,
            [](const SurfaceVertex& p){return p.local;},[](const SurfaceVertex& p){return p.uv;}));
        static_cast<void>(content(surface));
        if (surface.content_placement)
            require(content_leases.insert(*surface.content_placement).second,ErrorCode::invalid_definition,
                    "Each surface must own a distinct content placement lease; share the live instance explicitly",{id,*surface.content_placement});
    }
    for (const auto& [id,group] : stage.groups) {
        require(id == group.id,ErrorCode::invalid_definition,"Group identity differs from map key",{id,group.id});
        validate_display_name(group.name,id);
        std::set<Id> members;
        for (const auto& surface : group.surfaces) {
            static_cast<void>(find(stage.surfaces,surface));
            require(members.insert(surface).second,ErrorCode::duplicate_id,"Operational group repeats a surface",{id,surface});
        }
        for (const auto& seam : group.seams) {
            edge(stage,group,seam.first); edge(stage,group,seam.second);
            require(seam.first.surface != seam.second.surface || ordered(seam.first.first,seam.first.second) != ordered(seam.second.first,seam.second.second),
                    ErrorCode::invalid_geometry,"A seam cannot join an edge to itself",{id});
        }
    }
    for (const auto& [id,layout] : stage.layouts) {
        require(id == layout.id,ErrorCode::invalid_definition,"Layout identity differs from map key",{id,layout.id});
        validate_display_name(layout.name,id);
        const auto& group = find(stage.groups,layout.group);
        require(layout.mode == LayoutMode::independent || layout.mode == LayoutMode::continuous,ErrorCode::invalid_layout,"Unknown content layout mode",{id});
        std::set<Id> surfaces;
        std::optional<InstancePort> common;
        for (const auto& chart : layout.charts) {
            require(std::find(group.surfaces.begin(),group.surfaces.end(),chart.surface) != group.surfaces.end(),ErrorCode::missing_reference,"UV chart surface is not in its group",{id,chart.surface});
            require(surfaces.insert(chart.surface).second,ErrorCode::duplicate_id,"Layout repeats a surface UV chart",{id,chart.surface});
            capacity(chart.vertices.size(),chart.triangles.size());
            static_cast<void>(mesh(chart.vertices,chart.triangles,id,
                [](const LayoutVertex& p){return p.surface_uv;},[](const LayoutVertex& p){return p.composition_uv;}));
            if (layout.mode == LayoutMode::continuous) {
                const auto source = content(find(stage.surfaces,chart.surface));
                require(source.has_value(),ErrorCode::invalid_layout,"Continuous layout has an unbound surface",{id,chart.surface});
                require(!common || *common == *source,ErrorCode::invalid_layout,"Continuous charts must consume the same composition instance/output",{id,chart.surface});
                common = source;
            }
        }
        require(surfaces.size() == group.surfaces.size(),ErrorCode::invalid_layout,"Layout must explicitly chart every group member",{id,group.id});
    }
    for (const auto& [id,output] : stage.outputs) {
        require(id == output.id,ErrorCode::invalid_definition,"Output identity differs from map key",{id,output.id});
        validate_display_name(output.name,id); validate_display_name(output.adapter,id);
        require(output.desired.width > 0 && output.desired.height > 0 && output.desired.refresh_hz.numerator > 0 && output.desired.refresh_hz.denominator > 0,
                ErrorCode::invalid_definition,"Desired output mode needs positive dimensions/rational refresh",{id});
    }
    for (const auto& [id,patch] : stage.patches) {
        require(id == patch.id,ErrorCode::invalid_definition,"Patch identity differs from map key",{id,patch.id});
        validate_display_name(patch.name,id);
        if (patch.visual) validate_visual(*patch.visual,id);
        static_cast<void>(find(stage.surfaces,patch.surface)); static_cast<void>(find(stage.outputs,patch.output));
        const auto& calibration = patch.calibration;
        require(calibration.reference_width > 0 && calibration.reference_height > 0,ErrorCode::invalid_geometry,"Calibration reference mode must be retained and positive",{id});
        capacity(calibration.vertices.size(),calibration.triangles.size());
        const auto uv = mesh(calibration.vertices,calibration.triangles,id,
            [](const WarpVertex& p){return p.surface_uv;},[](const WarpVertex& p){return p.projector_pixels;});
        const auto pixels = mesh(calibration.vertices,calibration.triangles,id,
            [](const WarpVertex& p){return p.projector_pixels;},[](const WarpVertex& p){return p.surface_uv;});
        require(calibration.mode == WarpMode::homography || calibration.mode == WarpMode::mesh,ErrorCode::invalid_geometry,"Unknown warp mode",{id});
        if (calibration.mode == WarpMode::homography) {
            require(calibration.vertices.size() == 4 && calibration.triangles.size() == 2,ErrorCode::invalid_geometry,"Homography calibration needs four controls/two quad faces",{id});
            convex_boundary(calibration.boundary,uv,id); convex_boundary(calibration.boundary,pixels,id);
            std::map<std::pair<Id,Id>,std::size_t> edges;
            for (const auto& triangle : calibration.triangles)
                for (std::size_t index = 0; index < 3; ++index) ++edges[ordered(triangle.vertices[index],triangle.vertices[(index+1)%3])];
            require(edges.size() == 5,ErrorCode::invalid_geometry,"Homography faces must triangulate one quad",{id});
            for (std::size_t index = 0; index < 4; ++index) {
                const auto found = edges.find(ordered(calibration.boundary[index],calibration.boundary[(index+1)%4]));
                require(found != edges.end() && found->second == 1,ErrorCode::invalid_geometry,"Homography topology differs from its stable boundary",{id});
            }
        }
    }
}
std::vector<Triangle> quad_faces() {
    return {{{Id{"p00"},Id{"p10"},Id{"p11"}}},{{Id{"p00"},Id{"p11"},Id{"p01"}}}};
}
}
StageSnapshot::StageSnapshot(Stage stage, const InstancePool& instances, StageLimits limits) {
    validate(stage,instances,limits);
    stage_ = std::make_shared<const Stage>(std::move(stage));
}
SurfaceGeometry unit_quad() {
    return {{{Id{"p00"},{0,0},{0,0}},{Id{"p10"},{1,0},{1,0}},
             {Id{"p11"},{1,1},{1,1}},{Id{"p01"},{0,1},{0,1}}},quad_faces()};
}
Calibration quad_calibration(std::uint32_t width, std::uint32_t height) {
    return {WarpMode::homography,width,height,
        {{Id{"p00"},{0,0},{0,0}},{Id{"p10"},{1,0},{static_cast<double>(width),0}},
         {Id{"p11"},{1,1},{static_cast<double>(width),static_cast<double>(height)}},{Id{"p01"},{0,1},{0,static_cast<double>(height)}}},
        quad_faces(),{Id{"p00"},Id{"p10"},Id{"p11"},Id{"p01"}}};
}
SurfaceChart quad_chart(const Id& surface, Vec2 min, Vec2 max) {
    return {surface,{{Id{"p00"},{0,0},min},{Id{"p10"},{1,0},{max.x,min.y}},
                    {Id{"p11"},{1,1},max},{Id{"p01"},{0,1},{min.x,max.y}}},quad_faces()};
}
DefaultProject make_default_project(Definitions library, InstancePool::IdGenerator ids) {
    require(static_cast<bool>(ids),ErrorCode::invalid_definition,"Project ID allocator is missing");
    // Share allocation context across definition/stage/instance creation and
    // candidate copies; a caller's stateful allocator must not be rewound.
    auto allocator = std::make_shared<InstancePool::IdGenerator>(std::move(ids));
    const auto next = [allocator]{return (*allocator)();};
    const auto asset = next(), composition = next(), input = next(), output_port = next(), slot = next();
    require(library.assets.emplace(asset,Asset{asset,"Root composition",CompositionSource{composition}}).second,
            ErrorCode::duplicate_id,"Root asset ID collided",{asset});
    require(library.compositions.emplace(composition,Composition{composition,"Root composition",
        {{input,"Content",StreamKind::video}},{{output_port,"Program",StreamKind::video}},
        {{slot,"Default content",StreamKind::video,InputPort{input},std::nullopt}},{{output_port,Canvas{}}}}).second,
        ErrorCode::duplicate_id,"Root composition ID collided",{composition});
    InstancePool pool{DefinitionSnapshot{std::move(library)},{},next};
    const auto root = pool.place({asset,output_port});
    const auto content = pool.place_shared(root.source);
    const auto surface = next(), output = next(), patch = next();
    Stage stage;
    stage.surfaces.emplace(surface,Surface{surface,"Surface 1",content.id,unit_quad()});
    stage.outputs.emplace(output,OutputTarget{output,"Output 1","windows.display","",{}});
    stage.patches.emplace(patch,ProjectionPatch{patch,"Patch 1",surface,output,quad_calibration(1920,1080)});
    StageSnapshot snapshot{std::move(stage),pool};
    return {std::move(pool),std::move(snapshot),root.id,root.source.instance,input,surface,output};
}
} // namespace mapgyver::domain
