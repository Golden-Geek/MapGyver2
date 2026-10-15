// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/control/registry.hpp>
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

using namespace mapgyver::control;
namespace {
Id id(const char* value) {return Id{value};}
void check(bool valid,const char* message) {if (!valid) throw std::runtime_error(message);}
void rejects(RegistryErrorCode code,const std::function<void()>& operation) {
    try {operation();} catch (const RegistryError& error) {check(error.code() == code,"Wrong registry rejection"); return;}
    throw std::runtime_error("Invalid registry operation succeeded");
}
void model_rejects(ErrorCode code,const std::function<void()>& operation) {
    try {operation();} catch (const ModelError& error) {check(error.code() == code,"Wrong domain rejection"); return;}
    throw std::runtime_error("Invalid registered model edit succeeded");
}
DefaultProject fixture() {
    Definitions definitions;
    definitions.assets.emplace(id("media"),Asset{id("media"),"Movie",ExternalSource{"future.source","movie.mp4",
        {{id("picture"),"Picture",StreamKind::video},{id("sound"),"Sound",StreamKind::audio}}}});
    auto project = make_default_project(std::move(definitions),[next = 0]() mutable {return Id{"registry-"+std::to_string(++next)};});
    auto stage = project.stage.data();
    stage.groups.emplace(id("group"),SurfaceGroup{id("group"),"Group",{project.surface},{}});
    stage.layouts.emplace(id("layout"),Layout{id("layout"),"Layout",id("group"),LayoutMode::independent,{quad_chart(project.surface)}});
    project.stage = StageSnapshot{std::move(stage),project.instances};
    return project;
}
std::set<std::string> addresses(const ParameterRegistry& registry) {
    std::set<std::string> result;
    for (const auto& descriptor : registry.inventory()) result.insert(descriptor.address);
    return result;
}
void metadata_and_atomic_writes() {
    auto project = fixture(); const auto output = project.output, surface = project.surface, root = project.root_instance;
    const auto composition = std::get<CompositionSource>(project.instances.definitions().asset(project.instances.instance(root).asset).source).composition;
    const auto port = project.instances.placements().at(project.root_placement).source.port;
    ProjectController controller{std::move(project)};
    ParameterRegistry registry{controller};
    const auto output_path = "/outputs/"+output.str();
    const auto canvas_path = "/show/compositions/"+composition.str()+"/canvases/"+port.str();
    const auto& width = registry.parameter(output_path+"/desired/width");
    check(width.owner == output && width.parameter == id("width") && width.type == ParameterType::unsigned_integer &&
          width.unit == "pixels" && width.range && width.range->minimum == 1 && width.access == Access::read_write &&
          width.persistence == Persistence::authored && width.available && width.default_value &&
          std::get<std::uint64_t>(*width.default_value) == 1920,"Width descriptor lost type/range/unit/default/access metadata");
    check(registry.parameter("/show/sources/"+root.str()+"/transport/position").persistence == Persistence::resume_checkpoint,
          "Requested media position was classified as ordinary persisted authored data");
    const auto original = controller.snapshot(); const auto patches = original->stage.data().patches;
    const auto change = registry.write(controller,0,{
        {output_path+"/name",std::string{"Renamed output"}},
        {output_path+"/adapter",std::string{"unknown.output"}},
        {output_path+"/endpoint",std::string{"persistent-id"}},
        {output_path+"/desired/width",std::uint64_t{3840}},
        {output_path+"/desired/height",std::uint64_t{2160}},
        {canvas_path+"/width",std::uint64_t{1280}},
        {"/library/assets/media/name",std::string{"Renamed movie"}},
        {"/stage/surfaces/"+surface.str()+"/name",std::string{"Renamed surface"}},
        {"/show/sources/"+root.str()+"/transport/rate",Rational{-1,2}},
        {"/show/sources/"+root.str()+"/transport/playing",true}});
    const auto& changed_output = change.snapshot->stage.data().outputs.at(output);
    check(changed_output.desired.width == 3840 && changed_output.desired.height == 2160 && changed_output.name == "Renamed output" &&
          changed_output.adapter == "unknown.output" && changed_output.endpoint == "persistent-id","Multiple writes to the same object overwrote one another");
    check(change.snapshot->instances.definitions().composition(composition).canvases.at(port).width == 1280 &&
          change.snapshot->instances.instance(root).transport.rate == Rational{-1,2} &&
          change.snapshot->instances.instance(root).transport.playing && change.snapshot->stage.data().patches == patches,
          "Mixed definition/stage/transport writes lost state or rewrote calibration");
    check(addresses(ParameterRegistry{controller}) == addresses(registry),"Renaming changed canonical addresses");
    rejects(RegistryErrorCode::stale_snapshot,[&]{static_cast<void>(registry.write(controller,1,{{output_path+"/name",std::string{"Old inventory"}}}));});
    ParameterRegistry current{controller}; const auto undone = current.undo(controller,1);
    check(undone.snapshot == original && controller.revision() == 2,"Registered undo failed to restore the entire transaction");
    rejects(RegistryErrorCode::stale_snapshot,[&]{static_cast<void>(registry.write(controller,2,{{output_path+"/name",std::string{"Reused snapshot"}}}));});
    check(ParameterRegistry{controller}.redo(controller,2).snapshot == change.snapshot,"Registered redo did not restore exact state");
}
void rejection_and_derived_access() {
    auto project = fixture(); const auto output_path = "/outputs/"+project.output.str();
    const auto point_path = "/stage/surfaces/"+project.surface.str()+"/geometry/points/p00/position";
    const auto root_path = "/show/sources/"+project.root_instance.str();
    ProjectController controller{std::move(project)}; ParameterRegistry registry{controller};
    const auto original = controller.snapshot();
    std::size_t read_only = 0;
    for (const auto& descriptor : registry.inventory()) if (descriptor.access == Access::read_only) {
        ++read_only;
        rejects(RegistryErrorCode::read_only,[&]{static_cast<void>(registry.write(controller,0,
            {{output_path+"/name",std::string{"Must roll back"}},{descriptor.address,descriptor.value}}));});
    }
    check(read_only >= 6 && controller.snapshot() == original && controller.revision() == 0,"Derived writes bypassed read-only access or partially applied");
    rejects(RegistryErrorCode::type_mismatch,[&]{static_cast<void>(registry.write(controller,0,{{output_path+"/desired/width",1920.0}}));});
    rejects(RegistryErrorCode::out_of_range,[&]{static_cast<void>(registry.write(controller,0,{{output_path+"/desired/width",std::uint64_t{0}}}));});
    rejects(RegistryErrorCode::out_of_range,[&]{static_cast<void>(registry.write(controller,0,{{output_path+"/desired/width",std::numeric_limits<std::uint64_t>::max()}}));});
    rejects(RegistryErrorCode::out_of_range,[&]{static_cast<void>(registry.write(controller,0,{{point_path,Vec2{std::numeric_limits<double>::quiet_NaN(),0}}}));});
    rejects(RegistryErrorCode::missing_address,[&]{static_cast<void>(registry.write(controller,0,{{"/missing/parameter",true}}));});
    model_rejects(ErrorCode::invalid_transport,[&]{static_cast<void>(registry.write(controller,0,
        {{output_path+"/name",std::string{"Must roll back"}},{root_path+"/transport/rate",Rational{1,0}}}));});
    model_rejects(ErrorCode::invalid_geometry,[&]{static_cast<void>(registry.write(controller,0,
        {{output_path+"/name",std::string{"Must roll back"}},{point_path,Vec2{1,0}}}));});
    check(controller.snapshot() == original && controller.revision() == 0 && !controller.can_undo(),"Validation failure changed snapshot or journal");
    rejects(RegistryErrorCode::capacity_exceeded,[&]{static_cast<void>(ParameterRegistry{controller,RegistryLimits{1,1024}});});
    ParameterRegistry bounded{controller,RegistryLimits{262144,1}};
    rejects(RegistryErrorCode::capacity_exceeded,[&]{static_cast<void>(bounded.write(controller,0,
        {{output_path+"/name",std::string{"One"}},{output_path+"/name",std::string{"Two"}}}));});
}
void stable_order_points_and_action_inventory() {
    auto project = fixture(); const auto surface = project.surface;
    const auto composition = std::get<CompositionSource>(project.instances.definitions().asset(project.instances.instance(project.root_instance).asset).source).composition;
    ProjectController controller{std::move(project)}; ParameterRegistry registry{controller};
    const auto original_addresses = addresses(registry);
    std::set<std::string> actions;
    for (const auto& action : registry.actions()) {
        check(action.access == Access::trigger && !action.payload_type.empty() && !action.description.empty(),"Structural action lacks descriptor metadata");
        check(actions.insert(action.address).second,"Structural action address is duplicated");
    }
    check(actions.size() == std::variant_size_v<Command>+2 && actions.contains("/commands/surfaces/assign") &&
          actions.contains("/commands/definitions/replace") && actions.contains("/commands/history/undo"),"Typed command coverage is incomplete");
    const auto changed = registry.write(controller,0,{{"/library/assets/media/outputs/order",std::vector<Id>{id("sound"),id("picture")}}});
    check(changed.snapshot->instances.definitions().outputs(id("media")).front().id == id("sound") &&
          addresses(ParameterRegistry{controller}) == original_addresses,"Port reorder changed addressing or failed to apply");
    ParameterRegistry next{controller};
    model_rejects(ErrorCode::duplicate_id,[&]{static_cast<void>(next.write(controller,1,
        {{"/library/assets/media/outputs/order",std::vector<Id>{id("picture"),id("picture")}}}));});
    auto stage = controller.snapshot()->stage.data();
    std::reverse(stage.surfaces.at(surface).geometry.vertices.begin(),stage.surfaces.at(surface).geometry.vertices.end());
    static_cast<void>(next.execute(controller,1,{ReplaceStage{stage}}));
    ParameterRegistry reordered{controller};
    check(addresses(reordered) == original_addresses,"Point-vector reorder changed stable point addresses");
    const auto position_path = "/stage/surfaces/"+surface.str()+"/geometry/points/p00/position";
    const auto point_change = reordered.write(controller,2,{{position_path,Vec2{-0.25,0}}});
    const auto& points = point_change.snapshot->stage.data().surfaces.at(surface).geometry.vertices;
    check(std::find_if(points.begin(),points.end(),[](const auto& point){return point.id == id("p00");})->local == Vec2{-0.25,0},
          "Stable point setter changed an index rather than its ID");
    check(ParameterRegistry{controller}.parameter("/show/compositions/"+composition.str()+"/layers/order").type == ParameterType::id_list,
          "Composition layer order is missing from the registry");
}
void live_layer_edits_and_distinct_geometry_spaces() {
    auto project = fixture(); const auto surface = project.surface, root = project.root_instance, input = project.content_input;
    const auto composition = std::get<CompositionSource>(project.instances.definitions().asset(project.instances.instance(root).asset).source).composition;
    auto definitions = project.instances.definitions().data();
    auto& layers = definitions.compositions.at(composition).layers;
    const auto slot = layers.front().id;
    layers.push_back({id("media-layer"),"Movie layer",StreamKind::video,AssetPort{id("media"),id("picture")},std::nullopt});
    static_cast<void>(project.instances.reconcile_definitions(DefinitionSnapshot{definitions}));
    const auto media = project.instances.instance(root).layers.at(id("media-layer")).instance;
    project.instances.set_transport(media,{{99,{1,25}},{1,1},true,false});
    ProjectController controller{std::move(project)}; ParameterRegistry registry{controller};
    const auto before = controller.snapshot();
    const auto base = "/show/compositions/"+composition.str()+"/layers";
    const auto reordered = registry.write(controller,0,{
        {base+"/order",std::vector<Id>{id("media-layer"),slot}},
        {base+"/media-layer/name",std::string{"Renamed nested movie"}}});
    check(reordered.snapshot->instances.instances() == before->instances.instances() &&
          reordered.snapshot->instances.instance(media).transport.position.ticks == 99,
          "Registered layer reorder/rename reset nested source state");
    ParameterRegistry next{controller};
    const auto changed = next.write(controller,1,{{base+"/media-layer/source",LayerSource{InputPort{input}}}});
    check(changed.retired_sources == std::vector<Id>{media} && changed.snapshot->instances.instance(root).layers.empty(),
          "Typed layer-source edit did not reconcile definition-owned children");
    ParameterRegistry current{controller};
    const auto bound = current.execute(controller,2,{BindInput{root,input,AssetPort{id("media"),id("picture")}}});
    check(bound.created_sources.size() == 1 && ParameterRegistry{controller}.parameter(
        "/show/sources/"+root.str()+"/inputs/"+input.str()+"/source").access == Access::read_only,
        "Registered structural input action failed to expose the new derived binding");
    const auto patch = bound.snapshot->stage.data().patches.begin()->first;
    const auto old_surface = bound.snapshot->stage.data().surfaces.at(surface).geometry;
    const auto patch_base = "/stage/patches/"+patch.str()+"/calibration";
    const auto chart_base = "/stage/layouts/layout/charts/"+surface.str();
    const auto geometry = ParameterRegistry{controller}.write(controller,3,{
        {patch_base+"/points/p00/projectorPixels",Vec2{-12,0}},
        {patch_base+"/referenceWidth",std::uint64_t{4096}},
        {chart_base+"/points/p00/compositionUv",Vec2{-0.2,0}}});
    const auto& calibration = geometry.snapshot->stage.data().patches.at(patch).calibration;
    check(calibration.vertices.front().projector_pixels == Vec2{-12,0} && calibration.reference_width == 4096 &&
          calibration.vertices.at(1).projector_pixels == bound.snapshot->stage.data().patches.at(patch).calibration.vertices.at(1).projector_pixels &&
          geometry.snapshot->stage.data().surfaces.at(surface).geometry == old_surface &&
          geometry.snapshot->stage.data().layouts.at(id("layout")).charts.front().vertices.front().composition_uv == Vec2{-0.2,0},
          "Registered chart/projector edits crossed coordinate spaces or automatically rescaled calibration");
}
}
int main() {
    try {
        metadata_and_atomic_writes(); rejection_and_derived_access(); stable_order_points_and_action_inventory();
        live_layer_edits_and_distinct_geometry_spaces();
        std::cout << "Passed typed parameter registry contracts\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Registry contract failed: " << error.what() << '\n'; return 1;
    }
}
