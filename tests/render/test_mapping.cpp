// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/render/plan.hpp>
#include <mapgyver/control/wire.hpp>
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
    auto fitted_stage = controller.snapshot()->stage.data();fitted_stage.surfaces.at(f.surface).content = SurfaceContent{{0,0},{1,1},ContentFit::fit,1};
    control::ProjectController fitted{*controller.snapshot()};static_cast<void>(fitted.transact(0,{control::ReplaceStage{fitted_stage}}));
    const auto fitted_plan = render::compile(*fitted.snapshot(),1,f.images);
    const auto& fitted_map = std::find_if(fitted_plan->output_mappings.begin(),fitted_plan->output_mappings.end(),[&](const auto& map){return map.id == output2;})->patches[0];
    check(!render::mapped_pixel(fitted_map,0,4,8,8,8,8) && render::mapped_pixel(fitted_map,2,4,8,8,8,8),"Fit used the whole canvas aspect instead of the charted surface region");
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
void visual_rules() {
    check(!render::layer_pixel(0,0,8,8,8,4,ContentFit::fit) && render::layer_pixel(0,2,8,8,8,4,ContentFit::fit) == std::array<std::uint32_t,2>{0,0},"Layer fit failed letterbox coverage");
    check(render::layer_pixel(0,0,8,8,8,4,ContentFit::fill) == std::array<std::uint32_t,2>{2,0} && render::layer_pixel(7,7,8,8,8,4,ContentFit::fill) == std::array<std::uint32_t,2>{5,3},"Layer fill failed centred cropping");
    check(!render::layer_pixel(0,0,8,8,4,8,ContentFit::fit) && render::layer_pixel(2,0,8,8,4,8,ContentFit::fit) == std::array<std::uint32_t,2>{0,0},"Portrait layer fit failed coverage");
    check(render::layer_pixel(16383,16383,16384,16384,16384,16384,ContentFit::fill) == std::array<std::uint32_t,2>{16383,16383},"Integer layer sampling overflowed its qualified dimension profile");
    auto f = fixture();auto legacy = control::wire::encode_project(f.state);
    for (auto& [id,surface] : legacy["stage"]["surfaces"].items()) {static_cast<void>(id);surface.erase("content");}
    for (auto& [id,patch] : legacy["stage"]["patches"].items()) {static_cast<void>(id);patch.erase("visual");}
    for (auto& [id,c] : legacy["definitions"]["compositions"].items()) {static_cast<void>(id);for (auto& layer : c["layers"]) layer.erase("visual");}
    check(control::wire::encode_project(control::wire::decode_project(legacy)) == control::wire::encode_project(f.state),"Legacy visual defaults changed authored state");
    auto malformed = legacy;malformed["stage"]["surfaces"].begin().value()["contnet"] = nullptr;
    bool rejected = false;try {static_cast<void>(control::wire::decode_project(malformed));} catch (const control::wire::Error&) {rejected = true;}
    check(rejected,"Legacy defaults silently accepted an unknown field");
    malformed = legacy;malformed["stage"]["surfaces"].begin().value().erase("geometry");rejected = false;
    try {static_cast<void>(control::wire::decode_project(malformed));} catch (const control::wire::Error&) {rejected = true;}
    check(rejected,"Legacy defaults weakened required geometry fields");
    control::ProjectController controller{f.state};auto definitions = f.state.instances.definitions().data();
    for (auto& [id,c] : definitions.compositions) {static_cast<void>(id);for (auto& [port,canvas] : c.canvases) {static_cast<void>(port);canvas.width = 8;canvas.height = 4;}}
    static_cast<void>(controller.transact(0,{control::ReplaceDefinitions{definitions}}));
    const auto original = controller.snapshot();const auto surface_path = "/stage/surfaces/"+f.surface.str()+"/content";
    const auto patch_id = original->stage.data().patches.begin()->first;
    const auto patch_path = "/stage/patches/"+patch_id.str()+"/visual";
    auto write = [&](const std::vector<control::ParameterWrite>& values) {
        return control::ParameterRegistry{controller}.write(controller,controller.revision(),values);
    };
    write({{surface_path+"/fit",ContentFit::fit}});
    auto plan = render::compile(*controller.snapshot(),controller.revision(),f.images);const auto& fit = plan->output_mappings[0].patches[0];
    check(!render::mapped_pixel(fit,0,0,8,8,8,4) && render::mapped_pixel(fit,0,2,8,8,8,4) == std::array<std::uint32_t,2>{0,0},"Fit failed centred letterboxing on the logical aspect");
    write({{surface_path+"/fit",ContentFit::fill}});
    plan = render::compile(*controller.snapshot(),controller.revision(),f.images);const auto& fill = plan->output_mappings[0].patches[0];
    check(render::mapped_pixel(fill,0,0,8,8,8,4) == std::array<std::uint32_t,2>{2,0} &&
        render::mapped_pixel(fill,7,7,8,8,8,4) == std::array<std::uint32_t,2>{5,3},"Fill failed centred source cropping");
    write({{surface_path+"/fit",ContentFit::stretch},{surface_path+"/cropMinimum",Vec2{.25,.25}},{surface_path+"/cropMaximum",Vec2{.75,.75}},
        {surface_path+"/opacity",.5},{patch_path+"/opacity",.8},{patch_path+"/mask/shape",MaskShape::rectangle},
        {patch_path+"/mask/minimum",Vec2{.25,.25}},{patch_path+"/mask/maximum",Vec2{.75,.75}}});
    plan = render::compile(*controller.snapshot(),controller.revision(),f.images);const auto& rectangle = plan->output_mappings[0].patches[0];
    check(render::mapped_pixel(rectangle,0,0,8,8,8,4) == std::array<std::uint32_t,2>{2,1},"Normalized crop was not applied after fitting");
    check(render::patch_coverage(rectangle,0,0,8,8) == 0 && std::abs(render::patch_coverage(rectangle,4,4,8,8)-.4F) < 1e-6F,"Rectangle/opacity coverage changed premultiplied scaling");
    write({{patch_path+"/mask/inverted",true}});plan = render::compile(*controller.snapshot(),controller.revision(),f.images);
    const auto& inverted = plan->output_mappings[0].patches[0];
    check(std::abs(render::patch_coverage(inverted,0,0,8,8)-.4F) < 1e-6F && render::patch_coverage(inverted,4,4,8,8) == 0,"Mask inversion changed only its interior");
    write({{patch_path+"/mask/inverted",false},{patch_path+"/mask/shape",MaskShape::ellipse},{patch_path+"/mask/minimum",Vec2{0,0}},
        {patch_path+"/mask/maximum",Vec2{1,1}},{patch_path+"/mask/feather",.25},{patch_path+"/blend",BlendMode::screen}});
    plan = render::compile(*controller.snapshot(),controller.revision(),f.images);const auto& ellipse = plan->output_mappings[0].patches[0];
    check(render::patch_coverage(ellipse,0,0,8,8) == 0 && std::abs(render::patch_coverage(ellipse,4,4,8,8)-.4F) < 1e-6F &&
        render::patch_coverage(ellipse,1,3,8,8) > .28F && render::patch_coverage(ellipse,1,3,8,8) < .30F,"Ellipse feather did not produce analytic inward coverage");
    check(controller.snapshot()->stage.data().patches.at(patch_id).calibration == original->stage.data().patches.at(patch_id).calibration,"Visual edits rewrote physical calibration");
    const auto encoded = control::wire::encode_project(*controller.snapshot());
    check(control::wire::encode_project(control::wire::decode_project(encoded)) == encoded,"Visual fields did not survive logical persistence");
    const auto before = controller.snapshot();const auto revision = controller.revision();
    rejected = false;try {write({{surface_path+"/opacity",.25},{surface_path+"/cropMinimum",Vec2{.9,.9}}});} catch (const ModelError&) {rejected = true;}
    check(rejected && controller.snapshot() == before && controller.revision() == revision,"Invalid crop partially applied its opacity transaction");
    static_cast<void>(controller.undo(revision));check(controller.snapshot()->stage.data().patches.at(patch_id).visual->mask.shape == MaskShape::rectangle,"Visual settings bypassed shared undo");
    const auto stable = controller.snapshot();const auto stable_revision = controller.revision();
    auto invalid_definitions = stable->instances.definitions().data();invalid_definitions.compositions.begin()->second.layers[0].visual = LayerVisual{1.1,BlendMode::over};
    rejected = false;try {static_cast<void>(controller.transact(stable_revision,{control::ReplaceDefinitions{invalid_definitions}}));} catch (const ModelError&) {rejected = true;}
    check(rejected && controller.snapshot() == stable && controller.revision() == stable_revision,"Structural layer edits bypassed visual validation");
    auto invalid_stage = stable->stage.data();invalid_stage.patches.at(patch_id).visual->mask.feather = .75;
    rejected = false;try {static_cast<void>(controller.transact(stable_revision,{control::ReplaceStage{invalid_stage}}));} catch (const ModelError&) {rejected = true;}
    check(rejected && controller.snapshot() == stable && controller.revision() == stable_revision,"Structural patch edits bypassed mask validation");
    auto inactive = stable->instances.definitions().data();inactive.compositions.begin()->second.layers[0].visual = LayerVisual{0,BlendMode::over};
    static_cast<void>(controller.transact(stable_revision,{control::ReplaceDefinitions{inactive}}));
    const auto culled = render::compile(*controller.snapshot(),controller.revision(),{});
    check(culled->nodes.size() == 1 && culled->issues.empty() && culled->image_upload_bytes == 0 &&
        controller.snapshot()->instances.instances() == stable->instances.instances(),"Zero opacity failed to cull unavailable visual work or changed source ownership");
    const render::Pixel a{.2F,.1F,0,.5F},b{.1F,.2F,.3F,.5F};
    const auto over = render::blend(a,b,BlendMode::over),add = render::blend(a,b,BlendMode::add),screen = render::blend(a,b,BlendMode::screen);
    check(std::abs(over.r-.25F) < 1e-6F && std::abs(add.r-.3F) < 1e-6F && std::abs(screen.r-.28F) < 1e-6F &&
        over.a == .75F && add.a == .75F && screen.a == .75F,"Blend RGB/associated alpha equations changed");
    check(render::blend(a,b,BlendMode::over,0) == b && render::blend(a,b,BlendMode::over,1) == over,"Opacity endpoints changed background or blend mode");
}
}
int main() {try {solve();mappings();visual_rules();std::cout << "Projective mapping, visual rules, legacy defaults and shared control contracts passed\n";return 0;}
    catch (const std::exception& error) {std::cerr << error.what() << '\n';return 1;}}
