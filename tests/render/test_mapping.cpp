// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/render/plan.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace mapgyver;
using namespace domain;
namespace {
void check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
template<class F> void rejects(F action) {try {action();} catch (const render::Error&) {return;}throw std::runtime_error("Invalid mapping accepted");}
constexpr std::array<Vec2,4> unit{{{0,0},{1,0},{1,1},{0,1}}};
void solve() {
    const std::array<Vec2,4> quad{{{.1,.2},{.85,.05},{.95,.8},{.2,.95}}};
    const auto forward = render::homography(unit,quad), backward = render::homography(quad,unit);
    for (std::size_t i = 0; i < 4; ++i) {
        const auto p = render::transform(forward,unit[i]);check(std::hypot(p.x-quad[i].x,p.y-quad[i].y) < 1e-12,"Homography missed a control");
    }
    for (const auto p : {Vec2{.2,.7},Vec2{.5,.5},Vec2{.9,.1}}) {
        const auto q = render::transform(backward,render::transform(forward,p));check(std::hypot(q.x-p.x,q.y-p.y) < 1e-12,"Perspective inverse lost an interior point");
    }
    // A valid projectivity can have h22 == 0 outside its calibrated polygon.
    const std::array<Vec2,4> offset{{{2,1},{4,1},{4,3},{2,3}}};
    const render::Matrix3 horizon{1,0,1,0,1,0,1,0,0};std::array<Vec2,4> projected{};
    for (std::size_t i = 0; i < 4; ++i) projected[i] = render::transform(horizon,offset[i]);
    const auto solved = render::homography(offset,projected);const auto p = render::transform(solved,{3,2});
    check(std::abs(p.x-4.0/3) < 1e-12 && std::abs(p.y-2.0/3) < 1e-12,"Solver incorrectly fixes a projective coefficient");
    auto invalid = quad;invalid[2] = invalid[1];rejects([&]{static_cast<void>(render::homography(unit,invalid));});
    invalid = quad;std::swap(invalid[1],invalid[2]);rejects([&]{static_cast<void>(render::homography(unit,invalid));});
    invalid = quad;invalid[0].x = std::numeric_limits<double>::infinity();rejects([&]{static_cast<void>(render::homography(unit,invalid));});
}
struct Fixture {control::ProjectState state;render::Images images;Id output,surface;};
Fixture fixture() {
    Definitions defs;const Id image{"pattern"};defs.assets.emplace(image,Asset{image,"Pattern",ExternalSource{"test","pattern",{{Id{"picture"},"Picture",StreamKind::video}}}});
    auto project = make_default_project(std::move(defs));auto definitions = project.instances.definitions().data();
    for (auto& [id,c] : definitions.compositions) {static_cast<void>(id);for (auto& [port,canvas] : c.canvases) {static_cast<void>(port);canvas.width = 8;canvas.height = 8;}}
    static_cast<void>(project.instances.reconcile_definitions(DefinitionSnapshot{std::move(definitions)}));
    static_cast<void>(project.instances.bind_input(project.root_instance,project.content_input,{image,Id{"picture"}},false));
    auto stage = project.stage.data();stage.outputs.at(project.output).desired = {8,8,{60,1}};
    const auto surface = project.surface, output = project.output;
    stage.patches.begin()->second.calibration = quad_calibration(8,8);project.stage = StageSnapshot{stage,project.instances};
    control::ProjectController controller{std::move(project)};render::Images images;
    for (const auto& [id,source] : controller.snapshot()->instances.instances()) if (source.asset == image)
        images.emplace(render::PortKey{id,Id{"picture"}},render::pattern(image,1,8,8,"gradient"));
    return {*controller.snapshot(),images,output,surface};
}
void mappings() {
    auto f = fixture();const auto original = render::compile(f.state,0,f.images);
    check(original->output_mappings.size() == 1 && original->output_mappings[0].patches[0].identity,"Default map is not exact pixel-centre identity");
    check(render::reference_output(*original,f.output,8,8) == render::reference(*original,original->preview,true),"Default mapping changed colour/alpha encoding");
    auto stage = f.state.stage.data();const auto calibration = stage.patches.begin()->second.calibration;
    // One shared composition over two surfaces, with left/right UV charts.
    const Id second{"second-surface"}, group{"group"}, layout{"continuous"};
    stage.surfaces.emplace(second,Surface{second,"Second",std::nullopt,unit_quad()});
    stage.groups.emplace(group,SurfaceGroup{group,"Group",{f.surface,second},{}});
    stage.layouts.emplace(layout,Layout{layout,"Continuous",group,LayoutMode::continuous,
        {quad_chart(f.surface,{0,0},{.5,1}),quad_chart(second,{.5,0},{1,1})}});
    auto& left = stage.patches.begin()->second;for (auto& v : left.calibration.vertices) v.projector_pixels.x *= .5;
    const Id right_id{"right-patch"};auto right = ProjectionPatch{right_id,"Right",second,f.output,calibration};
    for (auto& v : right.calibration.vertices) v.projector_pixels.x = 4+v.projector_pixels.x*.5;
    stage.patches.emplace(right_id,right);
    const Id output2{"second-output"}, patch2{"second-output-patch"};
    stage.outputs.emplace(output2,OutputTarget{output2,"Second output","windows.window","",{8,8,{60,1}}});
    stage.patches.emplace(patch2,ProjectionPatch{patch2,"Same left surface",f.surface,output2,calibration});
    // Assign the second surface before the continuous-layout validator runs.
    auto without_layout = stage;without_layout.layouts.clear();
    control::ProjectController controller{f.state};
    const auto root = f.state.instances.placements().at(f.state.root_placement).source;
    static_cast<void>(controller.transact(0,{control::ReplaceStage{without_layout},control::AssignSurfaceContent{second,root}}));
    stage.surfaces.at(second).content_placement = controller.snapshot()->stage.data().surfaces.at(second).content_placement;
    static_cast<void>(controller.transact(1,{control::ReplaceStage{stage}}));
    const auto plan = render::compile(*controller.snapshot(),2,f.images);
    check(plan->nodes.size() == original->nodes.size() && plan->surfaces[0].node == plan->surfaces[1].node,"Patch fan-out duplicated composition/source ownership");
    check(render::reference_output(*plan,f.output,8,8) == render::reference_output(*original,f.output,8,8),"Continuous chart seam changed content or introduced a gap");
    check(render::reference_output(*plan,f.output,9,9) == render::reference_output(*original,f.output,9,9),"A pixel-centre seam blended translucent content twice");
    check(render::reference_output(*plan,output2,8,8) != render::reference_output(*plan,f.output,8,8),"One surface across outputs ignored its chart");
    check(f.state.stage.data().patches.begin()->second.calibration == calibration,"Compilation rewrote authored calibration");
    auto reordered = controller.snapshot()->stage.data();
    for (auto& [id,patch] : reordered.patches) {static_cast<void>(id);std::reverse(patch.calibration.vertices.begin(),patch.calibration.vertices.end());std::reverse(patch.calibration.triangles.begin(),patch.calibration.triangles.end());}
    static_cast<void>(controller.transact(2,{control::ReplaceStage{reordered}}));
    const auto next = render::compile(*controller.snapshot(),3,f.images);
    check(next->output_mappings == plan->output_mappings,"Storage order changed stable-ID homography controls");
    auto limits = render::Limits{};limits.mapping_patches = 2;rejects([&]{static_cast<void>(render::compile(*controller.snapshot(),3,f.images,limits));});
    auto unsupported = reordered;unsupported.patches.begin()->second.calibration.mode = WarpMode::mesh;
    static_cast<void>(controller.transact(3,{control::ReplaceStage{unsupported}}));
    const auto unavailable = render::compile(*controller.snapshot(),4,f.images);
    check(std::any_of(unavailable->output_mappings.begin(),unavailable->output_mappings.end(),[](const auto& map){return map.reason == "projection_mesh_unavailable";}),"Unsupported mesh lost configuration/status reason");
    auto topology = reordered;topology.layouts.begin()->second.charts[0].triangles[1] = {{Id{"p00"},Id{"p10"},Id{"p01"}}};
    static_cast<void>(controller.transact(4,{control::ReplaceStage{topology}}));
    const auto bad_chart = render::compile(*controller.snapshot(),5,f.images);
    check(std::any_of(bad_chart->output_mappings.begin(),bad_chart->output_mappings.end(),[](const auto& map){return map.reason == "surface_chart_quad_unavailable";}),"Overlapping chart faces were silently replaced by a full quad");
    topology = reordered;topology.surfaces.at(f.surface).geometry.triangles[1] = {{Id{"p00"},Id{"p10"},Id{"p01"}}};
    static_cast<void>(controller.transact(5,{control::ReplaceStage{topology}}));
    const auto bad_surface = render::compile(*controller.snapshot(),6,f.images);
    check(std::any_of(bad_surface->output_mappings.begin(),bad_surface->output_mappings.end(),[](const auto& map){return map.reason == "surface_geometry_quad_unavailable";}),"Overlapping surface faces were silently replaced by a full quad");
}
}
int main() {try {solve();mappings();std::cout << "Projective mapping, shared charts, colour and stable calibration contracts passed\n";return 0;}
    catch (const std::exception& error) {std::cerr << error.what() << '\n';return 1;}}
