// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/render/plan.hpp>
#include <iostream>
#include <functional>
#include <limits>
#include <algorithm>

using namespace mapgyver;
using namespace domain;
namespace {
void check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
void rejects(const std::function<void()>& action) {
    try {action();} catch (const render::Error&) {return;}
    throw std::runtime_error("Invalid render preparation was accepted");
}
Asset image(std::string id) {return {Id{id},id,ExternalSource{"test.image",id,{{Id{"picture"},"Picture",StreamKind::video}}}};}
Asset composition(std::string id) {return {Id{id},id,CompositionSource{Id{id}}};}
Composition definition(std::string id,std::vector<Layer> layers,std::uint32_t size = 8) {
    return {Id{id},id,{},{{Id{"picture"},"Picture",StreamKind::video}},std::move(layers),{{Id{"picture"},Canvas{size,size}}}};
}
Layer layer(std::string id,std::string asset) {return {Id{id},id,StreamKind::video,AssetPort{Id{asset},Id{"picture"}},std::nullopt};}
control::ProjectController fixture() {
    Definitions defs;
    defs.assets.emplace(Id{"red"},image("red")); defs.assets.emplace(Id{"blue"},image("blue"));
    defs.assets.emplace(Id{"unused"},image("unused"));
    defs.assets.emplace(Id{"nested"},composition("nested"));
    defs.compositions.emplace(Id{"nested"},definition("nested",{layer("back","red"),layer("front","blue")}));
    auto initial = make_default_project(std::move(defs));
    auto updated = initial.instances.definitions().data();
    for (auto& [id,c] : updated.compositions) {static_cast<void>(id); for (auto& [port,canvas] : c.canvases) {static_cast<void>(port);canvas.width = 8;canvas.height = 8;}}
    static_cast<void>(initial.instances.reconcile_definitions(DefinitionSnapshot{std::move(updated)}));
    static_cast<void>(initial.instances.bind_input(initial.root_instance,initial.content_input,{Id{"nested"},Id{"picture"}},false));
    return control::ProjectController{std::move(initial)};
}
render::Images prepare(const control::ProjectState& state) {
    render::Images images;
    const auto red = render::rgba8(Id{"red-resource"},1,1,1,std::array<std::uint8_t,4>{255,0,0,255});
    const auto blue = render::rgba8(Id{"blue-resource"},1,1,1,std::array<std::uint8_t,4>{0,0,255,128});
    for (const auto& [id,source] : state.instances.instances()) {
        if (source.asset == Id{"red"}) images.emplace(render::PortKey{id,Id{"picture"}},red);
        if (source.asset == Id{"blue"}) images.emplace(render::PortKey{id,Id{"picture"}},blue);
    }
    return images;
}
void colour_and_generations() {
    const auto a = render::rgba8(Id{"a"},1,1,1,std::array<std::uint8_t,4>{128,0,0,128});
    check(a->pixels->at(0).r > .108F && a->pixels->at(0).r < .109F,"sRGB was not decoded before alpha premultiplication");
    const auto b = render::rgba8(Id{"b"},2,1,1,std::array<std::uint8_t,4>{64,0,0,128},{render::Transfer::srgb,render::Alpha::premultiplied});
    check(b->pixels->at(0).r > .107F && b->pixels->at(0).r < .109F,"Encoded premultiplied sRGB was decoded in the wrong order");
    rejects([]{static_cast<void>(render::rgba8(Id{"bad"},0,1,1,std::array<std::uint8_t,4>{0,0,0,0}));});
    rejects([]{static_cast<void>(render::rgba8(Id{"bad"},1,1,1,std::array<std::uint8_t,4>{255,0,0,128},{render::Transfer::srgb,render::Alpha::premultiplied}));});
    rejects([]{static_cast<void>(render::pattern(Id{"bad"},1,100,100,"bars",100));});
}
void active_graph_and_linear_blend() {
    auto controller = fixture(); const auto state = controller.snapshot();
    const auto plan = render::compile(*state,controller.revision(),prepare(*state));
    check(plan->nodes.size() == 4 && plan->targets.size() == 2 && plan->surfaces.size() == 1 && plan->issues.empty(),"Nested/shared active graph was not compiled once");
    check(plan->surfaces[0].node == plan->preview,"Shared surface/preview was duplicated");
    check(plan->image_upload_bytes == 32 && plan->target_bytes == 1024 && plan->export_bytes == 256,"Resource upload/pool/export budgets are not based on actual compatible slots");
    const auto pixels = render::reference(*plan,plan->preview);
    check(pixels.size() == 8*8*4 && pixels[0] == 187 && pixels[1] == 0 && pixels[2] == 188 && pixels[3] == 255,"Back-to-front compositing is not linear-light premultiplied over");
    auto defs = state->instances.definitions().data(); std::reverse(defs.compositions.at(Id{"nested"}).layers.begin(),defs.compositions.at(Id{"nested"}).layers.end());
    static_cast<void>(controller.transact(0,{control::ReplaceDefinitions{std::move(defs)}}));
    const auto next = render::compile(*controller.snapshot(),1,prepare(*controller.snapshot()));
    const auto changed = render::reference(*next,next->preview);
    check(changed[0] == 255 && changed[2] == 0 && render::reference(*plan,plan->preview) == pixels,"Layer reorder or immutable prior plan failed");
    auto limits = render::Limits{}; limits.targets = 1;
    rejects([&]{static_cast<void>(render::compile(*state,0,prepare(*state),limits));});
    limits = {}; limits.total_payload_bytes = 1311;
    rejects([&]{static_cast<void>(render::compile(*state,0,prepare(*state),limits));});
    check(controller.revision() == 1,"Failed preparation mutated authored state");
}
void unsupported_and_independent() {
    auto controller = fixture(); auto defs = controller.snapshot()->instances.definitions().data();
    defs.compositions.at(Id{"nested"}).canvases.at(Id{"picture"}).pixel_format = "future-format";
    static_cast<void>(controller.transact(0,{control::ReplaceDefinitions{defs}}));
    const auto plan = render::compile(*controller.snapshot(),1,prepare(*controller.snapshot()));
    check(plan->nodes.size() == 2 && plan->issues.size() == 1 && plan->image_upload_bytes == 0,"Unsupported canvas failed to cull unused visual branches or lost its reason");
    const auto blank = render::reference(*plan,plan->preview);
    check(std::all_of(blank.begin(),blank.end(),[](auto value){return value == 0;}) && controller.snapshot()->instances.definitions().data().compositions.at(Id{"nested"}).canvases.at(Id{"picture"}).pixel_format == "future-format","Unavailable output invented content or deleted authored configuration");
    auto pristine = fixture(); const auto state = pristine.snapshot();
    auto stage = state->stage.data();
    for (unsigned index = 1; index < 37; ++index) {
        const auto id = Id{"surface-"+std::to_string(index)};
        stage.surfaces.emplace(id,Surface{id,"Surface",std::nullopt,unit_quad()});
    }
    static_cast<void>(pristine.transact(0,{control::ReplaceStage{stage}}));
    std::vector<control::Command> assignments;
    const auto root = state->instances.placements().at(state->root_placement).source;
    for (const auto& [id,surface] : stage.surfaces) {static_cast<void>(surface);assignments.push_back(control::AssignSurfaceContent{id,root});}
    static_cast<void>(pristine.transact(1,assignments));
    const auto shared = render::compile(*pristine.snapshot(),2,prepare(*pristine.snapshot()));
    check(shared->surfaces.size() == 37 && shared->nodes.size() == 4,"Arbitrary shared surfaces duplicated composition work");
    const auto selected = state->stage.data().surfaces.begin()->first;
    static_cast<void>(pristine.transact(2,{control::AssignSurfaceContent{selected,AssetPort{state->instances.instance(root.instance).asset,root.port}}}));
    const auto independent = render::compile(*pristine.snapshot(),3,prepare(*pristine.snapshot()));
    const auto found = std::find_if(independent->surfaces.begin(),independent->surfaces.end(),[&](const auto& value){return value.id == selected;});
    check(independent->nodes.size() == 5 && found->node != independent->preview && render::reference(*independent,found->node)[3] == 0 &&
          render::reference(*independent,independent->preview)[3] == 255,"Independent placement incorrectly shared composition input/playback scope");
}
void deep_graph_and_pool_reuse() {
    Definitions defs; defs.assets.emplace(Id{"red"},image("red")); std::string prior = "red";
    for (unsigned index = 0; index < 2048; ++index) {
        const auto id = "chain-"+std::to_string(index); defs.assets.emplace(Id{id},composition(id));
        defs.compositions.emplace(Id{id},definition(id,{layer("child",prior)})); prior = id;
    }
    auto initial = make_default_project(std::move(defs)); auto library = initial.instances.definitions().data();
    for (auto& [id,c] : library.compositions) {static_cast<void>(id);for (auto& [p,canvas] : c.canvases) {static_cast<void>(p);canvas.width = 8;canvas.height = 8;}}
    static_cast<void>(initial.instances.reconcile_definitions(DefinitionSnapshot{std::move(library)}));
    static_cast<void>(initial.instances.bind_input(initial.root_instance,initial.content_input,{Id{prior},Id{"picture"}},false));
    const control::ProjectController controller{std::move(initial)};
    const auto plan = render::compile(*controller.snapshot(),0,prepare(*controller.snapshot()));
    check(plan->nodes.size() == 2050 && plan->targets.size() == 2 && plan->target_bytes == 1024,"Deep graph uses recursion or fails compatible target lifetime reuse");
    check(render::reference(*plan,plan->preview)[0] == 255,"Aliased target plan overwrote a live dependency");
}
void prepared_resource_contracts() {
    const auto controller = fixture();const auto state = controller.snapshot();
    const auto unavailable = render::compile(*state,0,{});
    check(unavailable->issues.size() == 2 && unavailable->image_upload_bytes == 0 && unavailable->targets.empty() && unavailable->fallback_bytes == 16 &&
        std::all_of(unavailable->issues.begin(),unavailable->issues.end(),[](const auto& value){return value.reason == "source_frame_unavailable";}),"Unavailable sources lost their configured nodes/reasons");
    const auto blank = render::reference(*unavailable,unavailable->preview);
    check(std::all_of(blank.begin(),blank.end(),[](auto value){return value == 0;}),"Unavailable source invented video content");
    auto images = prepare(*state);auto first = images.begin(), second = std::next(first);
    auto conflicting = std::make_shared<render::Image>(*second->second);
    conflicting->resource = first->second->resource;conflicting->generation = first->second->generation;
    second->second = conflicting;
    rejects([&]{static_cast<void>(render::compile(*state,0,images));});
    images = prepare(*state);auto invalid = std::make_shared<render::Image>(*images.begin()->second);
    auto pixels = std::make_shared<std::vector<render::Pixel>>(*invalid->pixels);pixels->at(0).r = std::numeric_limits<float>::quiet_NaN();invalid->pixels = pixels;images.begin()->second = invalid;
    rejects([&]{static_cast<void>(render::compile(*state,0,images));});
    check(controller.revision() == 0 && state->instances.definitions().data().assets.contains(Id{"unused"}),"Preparation failure deleted/modified authored assets");
}
}
int main() {
    try {colour_and_generations();active_graph_and_linear_blend();unsupported_and_independent();deep_graph_and_pool_reuse();prepared_resource_contracts();std::cout << "Render graph, colour, culling and lifetime contracts passed\n";return 0;}
    catch (const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}
