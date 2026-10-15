// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/domain/stage.hpp>
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

using namespace mapgyver::domain;
namespace {
Id id(const char* value) { return Id{value}; }
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void rejects(ErrorCode code, const std::function<void()>& operation) {
    try { operation(); }
    catch (const ModelError& error) { check(error.code() == code,"Stage rejected a candidate for the wrong contract"); return; }
    throw std::runtime_error("Invalid stage unexpectedly passed validation");
}
DefaultProject project() {
    Definitions library;
    library.assets.emplace(id("media"),Asset{id("media"),"Media",ExternalSource{"media.file","relative/movie.mp4",
        {{id("picture"),"Picture",StreamKind::video}}}});
    // An owned stateful allocator exercises shared context through candidate copies.
    return make_default_project(std::move(library),[sequence=0]() mutable {return Id{"project-"+std::to_string(++sequence)};});
}
void direct_default_flow() {
    auto show = project();
    const auto& stage = show.stage.data();
    check(stage.surfaces.size() == 1 && stage.outputs.size() == 1 && stage.patches.size() == 1,
          "Default project did not create the direct one-surface/output/patch path");
    check(stage.groups.empty() && stage.layouts.empty(),"Default show requires group/graph setup");
    check(show.instances.instance(show.root_instance).owner_references == 2,"Show root ownership depends on its one surface");
    const auto calibration = stage.patches;
    static_cast<void>(show.instances.bind_input(show.root_instance,show.content_input,{id("media"),id("picture")}));
    const auto media = show.instances.instance(show.root_instance).inputs.at(show.content_input).instance;
    show.instances.set_transport(media,{{24,{1,24}},{1,1},true,true});
    const StageSnapshot content_changed{stage,show.instances};
    check(content_changed.data().patches == calibration,"Content/transport assignment rewrote physical calibration");
    const auto& root = show.instances.instance(show.root_instance);
    const auto& composition = show.instances.definitions().composition(std::get<CompositionSource>(show.instances.definitions().asset(root.asset).source).composition);
    const auto port = show.instances.placements().at(show.root_placement).source.port;
    check(composition.canvases.at(port).width == 1920 && composition.canvases.at(port).height == 1080,
          "Root composition does not own its logical canvas format");
    auto resized = stage;
    resized.outputs.at(show.output).desired.width = 1280;
    resized.outputs.at(show.output).desired.height = 720;
    const StageSnapshot desired_mode{resized,show.instances};
    check(desired_mode.data().patches == calibration && composition.canvases.at(port).width == 1920,
          "Desired projector mode silently rescaled calibration or composition canvas");
    const auto surface_lease = *stage.surfaces.at(show.surface).content_placement;
    check(show.instances.remove_placement(surface_lease).empty(),"Removing the last output consumer retired the show root");
    check(show.instances.instance(media).transport.playing,"No-surface state lost native-show logical transport intent");
    auto without_surfaces = stage;
    without_surfaces.surfaces.clear(); without_surfaces.patches.clear();
    const StageSnapshot no_consumers{without_surfaces,show.instances};
    check(no_consumers.data().surfaces.empty(),"A native show cannot retain logical transport without surface consumers");
}
void arbitrary_sets_and_layouts(std::size_t count) {
    auto show = project();
    auto stage = show.stage.data();
    const auto root = show.instances.placements().at(show.root_placement).source;
    std::vector<Id> surfaces{show.surface};
    for (std::size_t index = 1; index < count; ++index) {
        const Id surface{"surface-"+std::to_string(index)};
        const auto lease = show.instances.place_shared(root);
        stage.surfaces.emplace(surface,Surface{surface,"Surface "+std::to_string(index+1),lease.id,unit_quad()});
        surfaces.push_back(surface);
    }
    stage.groups.emplace(id("group"),SurfaceGroup{id("group"),"Connected set",surfaces,{}});
    if (count > 1) stage.groups.at(id("group")).seams.push_back(
        {{surfaces[0],id("p10"),id("p11")},{surfaces[1],id("p00"),id("p01")},false});
    std::vector<SurfaceChart> charts;
    for (std::size_t index = 0; index < count; ++index)
        charts.push_back(quad_chart(surfaces[index],{static_cast<double>(index)/static_cast<double>(count),0},
            {static_cast<double>(index+1)/static_cast<double>(count),1}));
    stage.layouts.emplace(id("layout"),Layout{id("layout"),"Continuous canvas",id("group"),LayoutMode::continuous,charts});
    const StageSnapshot continuous{stage,show.instances};
    check(continuous.data().surfaces.size() == count && continuous.data().layouts.at(id("layout")).charts.size() == count,
          "Surface/layout count did not use the same arbitrary collection model");
    check(show.instances.instances().size() == 1 && show.instances.instance(root.instance).owner_references == count+1,
          "Shared composition content was duplicated per surface or references were lost");
    stage.outputs.emplace(id("second-output"),OutputTarget{id("second-output"),"Second output","future.output.adapter","configured-binding",{}});
    stage.outputs.emplace(id("third-output"),OutputTarget{id("third-output"),"Third output","transport.ndi","configured-network-target",{}});
    auto left = quad_calibration(1920,1080), right = left;
    for (auto& vertex : left.vertices) vertex.surface_uv.x *= .5;
    for (auto& vertex : right.vertices) vertex.surface_uv.x = .5 + vertex.surface_uv.x*.5;
    stage.patches.emplace(id("left-patch"),ProjectionPatch{id("left-patch"),"Left portion",surfaces[0],id("second-output"),left});
    stage.patches.emplace(id("right-patch"),ProjectionPatch{id("right-patch"),"Right portion",surfaces[0],id("third-output"),right});
    if (count > 1) stage.patches.emplace(id("shared-output-patch"),ProjectionPatch{id("shared-output-patch"),"Shared projector",surfaces[1],show.output,quad_calibration(1920,1080)});
    const StageSnapshot routed{stage,show.instances};
    check(routed.data() == stage,"Unavailable output adapter/configuration was deleted or changed");
    check(routed.data().outputs.size() == 3 && routed.data().patches.size() == (count > 1 ? 4U : 3U),
          "Many-to-many surface/projector patches were flattened to one-to-one routing");
    if (count > 1) {
        const auto calibration = stage.patches;
        const auto independent = show.instances.place({show.instances.instance(root.instance).asset,root.port});
        const auto old_lease = *stage.surfaces.at(surfaces[1]).content_placement;
        stage.surfaces.at(surfaces[1]).content_placement = independent.id;
        static_cast<void>(show.instances.remove_placement(old_lease));
        rejects(ErrorCode::invalid_layout,[&]{static_cast<void>(StageSnapshot{stage,show.instances});});
        stage.layouts.at(id("layout")).mode = LayoutMode::independent;
        const StageSnapshot independent_layout{stage,show.instances};
        check(independent_layout.data().patches == calibration,"Independent content routing changed calibration");
        check(independent_layout.data().surfaces.at(surfaces[0]).content_placement == routed.data().surfaces.at(surfaces[0]).content_placement,
              "Changing one destination changed a peer's content binding");
    }
}
void rejection_and_immutability() {
    auto show = project();
    const auto original = show.stage.data();
    auto invalid = original;
    const auto patch_id = invalid.patches.begin()->first;
    invalid.surfaces.at(show.surface).geometry.vertices[0].local.x = std::numeric_limits<double>::quiet_NaN();
    rejects(ErrorCode::invalid_geometry,[&]{static_cast<void>(StageSnapshot{invalid,show.instances});});
    invalid = original;
    invalid.surfaces.at(show.surface).geometry.triangles.push_back(invalid.surfaces.at(show.surface).geometry.triangles.front());
    rejects(ErrorCode::invalid_geometry,[&]{static_cast<void>(StageSnapshot{invalid,show.instances});});
    invalid = original;
    invalid.surfaces.at(show.surface).geometry.triangles.front().vertices[0] = id("missing-point");
    rejects(ErrorCode::missing_reference,[&]{static_cast<void>(StageSnapshot{invalid,show.instances});});
    invalid = original;
    invalid.patches.at(patch_id).calibration.vertices[2].projector_pixels = {10,10};
    rejects(ErrorCode::invalid_geometry,[&]{static_cast<void>(StageSnapshot{invalid,show.instances});});
    invalid = original;
    invalid.patches.at(patch_id).calibration.triangles.back() = invalid.patches.at(patch_id).calibration.triangles.front();
    rejects(ErrorCode::invalid_geometry,[&]{static_cast<void>(StageSnapshot{invalid,show.instances});});
    invalid = original;
    invalid.patches.at(patch_id).output = id("missing-output");
    rejects(ErrorCode::missing_reference,[&]{static_cast<void>(StageSnapshot{invalid,show.instances});});
    invalid = original;
    const auto media = show.instances.place({id("media"),id("picture")});
    invalid.surfaces.at(show.surface).content_placement = media.id;
    rejects(ErrorCode::type_mismatch,[&]{static_cast<void>(StageSnapshot{invalid,show.instances});});
    invalid = original;
    invalid.surfaces.emplace(id("duplicate-lease"),Surface{id("duplicate-lease"),"Bad shared lease",original.surfaces.at(show.surface).content_placement,unit_quad()});
    rejects(ErrorCode::invalid_definition,[&]{static_cast<void>(StageSnapshot{invalid,show.instances});});
    invalid = original;
    invalid.groups.emplace(id("group"),SurfaceGroup{id("group"),"Group",{show.surface},
        {{{show.surface,id("p00"),id("p11")},{show.surface,id("p10"),id("p11")},false}}});
    rejects(ErrorCode::invalid_geometry,[&]{static_cast<void>(StageSnapshot{invalid,show.instances});});
    StageLimits limits; limits.surfaces = 0;
    rejects(ErrorCode::capacity_exceeded,[&]{static_cast<void>(StageSnapshot{original,show.instances,limits});});
    limits = {}; limits.vertices = 3;
    rejects(ErrorCode::capacity_exceeded,[&]{static_cast<void>(StageSnapshot{original,show.instances,limits});});
    check(show.stage.data() == original,"Rejected stage candidate mutated its published snapshot");
}
void mesh_and_configured_formats() {
    auto show = project();
    auto stage = show.stage.data();
    auto& calibration = stage.patches.begin()->second.calibration;
    calibration.mode = WarpMode::mesh;
    calibration.vertices.push_back({id("centre"),{.5,.5},{960,540}});
    calibration.triangles = {{{id("p00"),id("p10"),id("centre")}},{{id("p10"),id("p11"),id("centre")}},
        {{id("p11"),id("p01"),id("centre")}},{{id("p01"),id("p00"),id("centre")}}};
    std::reverse(calibration.vertices.begin(),calibration.vertices.end());
    const StageSnapshot mesh{stage,show.instances};
    check(mesh.data().patches.begin()->second.calibration.vertices.size() == 5,"Mesh calibration was reduced to a four-corner patch");
    calibration.mode = WarpMode::homography;
    rejects(ErrorCode::invalid_geometry,[&]{static_cast<void>(StageSnapshot{stage,show.instances});});
    auto definitions = show.instances.definitions().data();
    auto& composition = definitions.compositions.begin()->second;
    auto& canvas = composition.canvases.begin()->second;
    canvas.pixel_format = "future-pixel-format"; canvas.colour_space = "future-colour-space"; canvas.alpha = "future-alpha";
    const DefinitionSnapshot configured{definitions};
    check(configured.data() == definitions,"Unavailable canvas format declarations were changed or deleted");
    composition.outputs.push_back({id("audio"),"Audio",StreamKind::audio});
    composition.canvases.emplace(id("audio"),Canvas{});
    rejects(ErrorCode::type_mismatch,[&]{static_cast<void>(DefinitionSnapshot{definitions});});
}
}
int main() {
    try {
        direct_default_flow(); arbitrary_sets_and_layouts(1); arbitrary_sets_and_layouts(3); arbitrary_sets_and_layouts(37);
        rejection_and_immutability(); mesh_and_configured_formats();
        std::cout << "Passed portable stage, layout and calibration ownership contracts\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Stage contract failed: " << error.what() << '\n'; return 1;
    }
}
