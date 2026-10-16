// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/control/wire.hpp>
#include <fstream>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

using namespace mapgyver::control;
namespace wire = mapgyver::control::wire;
namespace {
Id id(const char* value) {return Id{value};}
void check(bool valid,const char* message) {if (!valid) throw std::runtime_error(message);}
void rejects(wire::ErrorCode code,const std::function<void()>& operation) {
    try {operation();} catch (const wire::Error& error) {check(error.code() == code,"Wrong wire rejection"); return;}
    throw std::runtime_error("Invalid wire input succeeded");
}
void model_rejects(mapgyver::domain::ErrorCode code,const std::function<void()>& operation) {
    try {operation();} catch (const ModelError& error) {check(error.code() == code,"Wrong restored model rejection"); return;}
    throw std::runtime_error("Invalid restored model succeeded");
}
ProjectState fixture(std::size_t surfaces = 1) {
    Definitions definitions;
    definitions.assets.emplace(id("media"),Asset{id("media"),"Movie",ExternalSource{"media.file","média/映像.mov",
        {{id("picture"),"Picture",StreamKind::video},{id("sound"),"Sound",StreamKind::audio}}}});
    definitions.assets.emplace(id("app"),Asset{id("app"),"Configured future app",ExternalSource{"future.app","configured-app",
        {{id("one"),"One",StreamKind::video},{id("two"),"Two",StreamKind::video}}}});
    auto project = make_default_project(std::move(definitions),[next = 0]() mutable {return Id{"wire-"+std::to_string(++next)};});
    auto edited = project.instances.definitions().data();
    const auto root_asset = project.instances.instance(project.root_instance).asset;
    const auto root_composition = std::get<CompositionSource>(edited.assets.at(root_asset).source).composition;
    auto& composition = edited.compositions.at(root_composition);
    composition.layers.push_back({id("app-a"),"App A",StreamKind::video,AssetPort{id("app"),id("one")},id("group")});
    composition.layers.push_back({id("app-b"),"App B",StreamKind::video,AssetPort{id("app"),id("two")},id("group")});
    composition.canvases.begin()->second.pixel_format = "future-rgb96";
    composition.canvases.begin()->second.colour_space = "future-linear";
    static_cast<void>(project.instances.reconcile_definitions(DefinitionSnapshot{edited}));
    static_cast<void>(project.instances.bind_input(project.root_instance,project.content_input,{id("media"),id("picture")}));
    const auto bound = project.instances.instance(project.root_instance).inputs.at(project.content_input).instance;
    project.instances.set_transport(bound,{{9007199254740993LL,{1001,30000}},{-2,3},true,true});
    const auto independent = project.instances.place({id("media"),id("picture")});
    static_cast<void>(project.instances.place_shared({independent.source.instance,id("sound")}));
    project.instances.set_transport(independent.source.instance,{{42,{1,24}},{1,1},false,false});
    auto stage = project.stage.data();
    const auto root_port = project.instances.placements().at(project.root_placement).source;
    std::vector<Id> members{project.surface};
    for (std::size_t index = 1; index < surfaces; ++index) {
        const Id surface{"surface-"+std::to_string(index)}, patch{"patch-"+std::to_string(index)};
        const auto lease = project.instances.place_shared(root_port);
        stage.surfaces.emplace(surface,Surface{surface,"Surface",lease.id,unit_quad()});
        stage.patches.emplace(patch,ProjectionPatch{patch,"Patch",surface,project.output,quad_calibration(1920,1080)});
        members.push_back(surface);
    }
    stage.outputs.emplace(id("unknown-output"),OutputTarget{id("unknown-output"),"Configured NDI","ndi.output","persistent-binding",{4096,2160,{60000,1001}}});
    auto mesh = quad_calibration(1920,1080); mesh.mode = WarpMode::mesh; mesh.boundary.clear();
    mesh.vertices.push_back({id("center"),{0.5,0.5},{960,540}});
    mesh.triangles = {{{id("p00"),id("p10"),id("center")}},{{id("p10"),id("p11"),id("center")}},
                      {{id("p11"),id("p01"),id("center")}},{{id("p01"),id("p00"),id("center")}}};
    stage.patches.emplace(id("mesh-patch"),ProjectionPatch{id("mesh-patch"),"Mesh",project.surface,id("unknown-output"),mesh});
    std::vector<Seam> seams;
    if (members.size() > 1) seams.push_back({{members[0],id("p10"),id("p11")},{members[1],id("p00"),id("p01")},true});
    stage.groups.emplace(id("surfaces"),SurfaceGroup{id("surfaces"),"Surface group",members,seams});
    std::vector<SurfaceChart> charts;
    for (const auto& surface : members) charts.push_back(quad_chart(surface));
    stage.layouts.emplace(id("layout"),Layout{id("layout"),"Continuous layout",id("surfaces"),LayoutMode::continuous,charts});
    return {project.instances,StageSnapshot{std::move(stage),project.instances},project.root_placement};
}
void project_round_trips() {
    for (const auto count : {1U,3U,37U}) {
        const auto original = fixture(count);
        const auto document = wire::encode_project(original);
        const auto restored = wire::decode_project(wire::parse(document.dump()));
        check(restored.instances.instances() == original.instances.instances() &&
              restored.instances.placements() == original.instances.placements() &&
              restored.instances.definitions().data() == original.instances.definitions().data() &&
              restored.stage.data() == original.stage.data() && restored.root_placement == original.root_placement,
              "Project round trip changed identities, scopes, transport, geometry or configured placeholders");
        check(wire::encode_project(restored) == document,"Re-encoding changed the complete typed project document");
        check(document.dump().find("9007199254740993") != std::string::npos &&
              restored.stage.data().outputs.at(id("unknown-output")).adapter == "ndi.output",
              "Exact timestamp or unavailable configured adapter was lost");
        ProjectController controller{restored};
        const auto old = controller.snapshot();
        const auto root = old->instances.placements().at(old->root_placement).source.instance;
        static_cast<void>(controller.transact(0,{SetTransport{root,{{7,{1,25}},{1,1},true,false}}}));
        check(original.instances.instance(root).transport.position.ticks == 0 &&
              controller.snapshot()->instances.instance(root).transport.position.ticks == 7,
              "Editing restored state changed the original snapshot");
    }
}
void malformed_parser_and_model() {
    rejects(wire::ErrorCode::malformed,[]{static_cast<void>(wire::parse("{\"key\":1,\"key\":2}"));});
    rejects(wire::ErrorCode::malformed,[]{static_cast<void>(wire::parse("{\"a\":[{\"b\":1,\"b\":2}]}"));});
    check(wire::parse("[{\"a\":1},{\"a\":2}]").size() == 2,"Duplicate-key check confused sibling objects");
    rejects(wire::ErrorCode::malformed,[]{static_cast<void>(wire::parse(std::string{"\""}+static_cast<char>(0xff)+"\""));});
    rejects(wire::ErrorCode::malformed,[]{static_cast<void>(wire::parse("1e9999"));});
    rejects(wire::ErrorCode::malformed,[]{static_cast<void>(wire::parse("{} trailing"));});
    rejects(wire::ErrorCode::capacity_exceeded,[]{static_cast<void>(wire::parse("[1,2,3]",{3,100,10}));});
    rejects(wire::ErrorCode::capacity_exceeded,[]{static_cast<void>(wire::parse("[1,2,3]",{100,2,10}));});
    rejects(wire::ErrorCode::capacity_exceeded,[]{static_cast<void>(wire::parse("[[[[0]]]]",{100,100,2}));});
    const auto state = fixture(); const auto original = wire::encode_project(state);
    const auto malformed = [&](const std::function<void(wire::Json&)>& mutate) {
        auto document = original; mutate(document);
        rejects(wire::ErrorCode::malformed,[&]{static_cast<void>(wire::decode_project(document));});
    };
    auto future = original; future["schemaVersion"] = 2;
    rejects(wire::ErrorCode::unsupported_version,[&]{static_cast<void>(wire::decode_project(future));});
    malformed([](auto& value){value["unknown"] = true;});
    malformed([](auto& value){value.erase("placements");});
    malformed([](auto& value){value["stage"]["outputs"]["unknown-output"]["unknown"] = true;});
    malformed([](auto& value){value["stage"]["outputs"]["unknown-output"]["desired"]["width"] = std::numeric_limits<std::uint64_t>::max();});
    malformed([](auto& value){value["sources"].begin().value()["keepAlive"] = "true";});
    malformed([](auto& value){value["sources"].begin().value()["transport"]["position"]["ticks"] = 9007199254740993ULL;});
    malformed([](auto& value){value["definitions"]["assets"]["media"]["source"]["type"] = "UnknownFutureSource";});
    malformed([](auto& value){value["definitions"]["assets"]["media"]["source"]["value"]["outputs"][0]["kind"] = "unknown-stream";});
    malformed([](auto& value){value["stage"]["patches"]["mesh-patch"]["calibration"]["triangles"][0]["vertices"] = {"p00","p10"};});
    for (const auto* invalid : {"01","+1","-0","9223372036854775808"})
        malformed([&](auto& value){value["sources"].begin().value()["transport"]["position"]["ticks"] = invalid;});
    const auto model_invalid = [&](mapgyver::domain::ErrorCode code,const std::function<void(wire::Json&)>& mutate) {
        auto document = original; mutate(document);
        model_rejects(code,[&]{static_cast<void>(wire::decode_project(document));});
    };
    model_invalid(mapgyver::domain::ErrorCode::invalid_definition,[](auto& value){value["sources"].begin().value()["ownerReferences"] = "999";});
    model_invalid(mapgyver::domain::ErrorCode::missing_reference,[](auto& value){value["stage"]["patches"]["mesh-patch"]["surface"] = "missing";});
    model_invalid(mapgyver::domain::ErrorCode::invalid_transport,[](auto& value){value["sources"].begin().value()["transport"]["rate"]["denominator"] = "0";});
    model_invalid(mapgyver::domain::ErrorCode::invalid_id,[](auto& value){value["rootPlacement"] = "../invalid";});
    const auto root = state.instances.placements().at(state.root_placement).source;
    const auto input = state.instances.instance(root.instance).inputs.begin()->first;
    const auto root_asset = state.instances.instance(root.instance).asset;
    const auto composition = std::get<CompositionSource>(state.instances.definitions().asset(root_asset).source).composition;
    model_invalid(mapgyver::domain::ErrorCode::dependency_cycle,[&](auto& value){
        value["definitions"]["compositions"][composition.str()]["layers"][0]["source"] =
            {{"type","AssetPort"},{"value",{{"asset",root_asset.str()},{"port",root.port.str()}}}};
    });
    model_invalid(mapgyver::domain::ErrorCode::dependency_cycle,[&](auto& value){
        value["sources"][root.instance.str()]["inputs"][input.str()] = {{"instance",root.instance.str()},{"port",root.port.str()}};
    });
    auto bad_lease = original; bad_lease["stage"]["surfaces"].begin().value()["contentPlacement"] = state.root_placement.str();
    model_rejects(mapgyver::domain::ErrorCode::invalid_definition,[&]{static_cast<void>(wire::decode_project(bad_lease));});
    ProjectLimits small; small.snapshot_text_bytes = 1;
    try {static_cast<void>(wire::decode_project(original,small)); throw std::runtime_error("Project text budget was ignored");}
    catch (const ControlError& error) {check(error.code() == ControlErrorCode::capacity_exceeded,"Wrong project-budget rejection");}
}
void values_commands_and_descriptors() {
    rejects(wire::ErrorCode::malformed,[]{static_cast<void>(wire::encode_write({"/test/point",Vec2{std::numeric_limits<double>::quiet_NaN(),0}}));});
    const std::vector<ParameterValue> values{true,std::numeric_limits<std::uint64_t>::max(),1.25,std::string{"Text"},id("ref"),
        std::optional<Id>{},Rational{std::numeric_limits<std::int64_t>::min(),3},MediaTime{9007199254740993LL,{1001,30000}},
        Vec2{-0.25,2.0},StreamKind::audio,LayoutMode::continuous,WarpMode::mesh,LayerSource{AssetPort{id("asset"),id("port")}},
        InstancePort{id("instance"),id("port")},std::vector<Id>{id("one"),id("two")},
        std::vector<Triangle>{{{id("one"),id("two"),id("three")}}},
        std::vector<Seam>{{{id("one"),id("a"),id("b")},{id("two"),id("c"),id("d")},true}},ContentFit::fit,BlendMode::screen,MaskShape::ellipse};
    check(values.size() == std::variant_size_v<ParameterValue>,"A parameter alternative lacks wire coverage");
    for (const auto& value : values) {
        ParameterWrite write{"/test/value",value}; const auto encoded = wire::encode_write(write);
        const auto decoded = wire::decode_write(wire::parse(encoded.dump()));
        check(decoded.address == write.address && decoded.value == value,"Typed parameter value did not round trip exactly");
    }
    const auto state = fixture(); const auto root = state.instances.placements().at(state.root_placement).source;
    const auto input = state.instances.instance(root.instance).inputs.begin()->first;
    const std::vector<Command> commands{ReplaceDefinitions{state.instances.definitions().data()},ReplaceStage{state.stage.data()},
        PlaceSource{{id("media"),id("picture")},true},ShareSource{root},RemovePlacement{state.root_placement},
        BindInput{root.instance,input,AssetPort{id("media"),id("picture")}},UnbindInput{root.instance,input},
        SetTransport{root.instance,{{9007199254740993LL,{1001,30000}},{-1,2},true,true}},SetKeepAlive{root.instance,true},
        AssignSurfaceContent{state.stage.data().surfaces.begin()->first,root}};
    check(commands.size() == std::variant_size_v<Command>,"A structural command lacks wire coverage");
    for (const auto& command : commands) {
        const auto encoded = wire::encode_command(command);
        check(wire::encode_command(wire::decode_command(wire::parse(encoded.dump()))) == encoded,"Typed command did not round trip");
    }
    ProjectController controller{state}; ParameterRegistry registry{controller};
    const auto inventory = wire::describe(registry); const auto schema = wire::schema();
    check(inventory.at("revision") == "0" && inventory.at("parameters").size() == registry.inventory().size() &&
          inventory.at("actions").size() == registry.actions().size(),"Wire inventory differs from authoritative registry");
    std::set<std::string> addresses;
    for (const auto& parameter : inventory.at("parameters")) {
        check(parameter.at("type") == parameter.at("value").at("type"),"Descriptor type disagrees with its tagged current value");
        const auto address = parameter.at("address").get<std::string>();
        check(addresses.insert(address).second && registry.parameter(address).address == address,"Wire inventory omitted or duplicated a canonical address");
        check(parameter.at("available") == true,"Authored control availability was changed during export");
    }
    for (const auto& action : inventory.at("actions")) {
        const auto payload = action.at("payloadType").get<std::string>();
        check(payload == "expectedRevision" || schema.at("$defs").contains(payload),"Registered action has no generated payload schema");
    }
    const auto encoded = wire::encode_command(BindInput{root.instance,input,AssetPort{id("media"),id("picture")}});
    const auto change = registry.execute(controller,0,{wire::decode_command(wire::parse(encoded.dump()))});
    check(change.revision == 1 && change.created_sources.size() == 1,"Decoded command did not use the validated command path");
    const auto path = "/show/sources/"+root.instance.str()+"/ownerReferences";
    const auto read_only = wire::decode_write(wire::parse(wire::encode_write({path,std::uint64_t{999}}).dump()));
    try {static_cast<void>(ParameterRegistry{controller}.write(controller,1,{read_only})); throw std::runtime_error("Decoded write changed derived read-only state");}
    catch (const RegistryError& error) {check(error.code() == RegistryErrorCode::read_only,"Wrong decoded read-only rejection");}
}
}
int main(int argc,char** argv) {
    try {
        if (argc == 3 && std::string{argv[1]} == "--write-schema") {
            std::ofstream output{argv[2],std::ios::binary}; output << wire::schema().dump(2) << '\n';
            check(output.good(),"Unable to write generated schema"); return 0;
        }
        if (argc == 3 && std::string{argv[1]} == "--export-fixtures") {
            const std::filesystem::path directory{argv[2]}; std::filesystem::create_directories(directory);
            const auto save = [&](const char* name,const auto& value) {
                std::ofstream output{directory/name,std::ios::binary}; output << value.dump(2) << '\n';
                check(output.good(),"Unable to export wire fixture");
            };
            ProjectController controller{fixture(37)};
            save("project.json",wire::encode_project(*controller.snapshot()));
            save("registry.json",wire::describe(ParameterRegistry{controller}));
            save("command.json",wire::encode_command(ReplaceStage{controller.snapshot()->stage.data()}));
            save("write.json",wire::encode_write({"/test/position",MediaTime{9007199254740993LL,{1001,30000}}}));
            return 0;
        }
        project_round_trips(); malformed_parser_and_model(); values_commands_and_descriptors();
        if (argc == 3 && std::string{argv[1]} == "--schema") {
            std::ifstream input{argv[2],std::ios::binary}; check(input.good(),"Checked-in generated schema is missing");
            const std::string bytes{std::istreambuf_iterator<char>{input},std::istreambuf_iterator<char>{}};
            check(wire::parse(bytes) == wire::schema(),"Checked-in schema differs from codec field registration");
        } else check(argc == 1,"Unknown wire-test arguments");
        std::cout << "Passed strict portable control/model wire contracts\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Wire contract failed: " << error.what() << '\n'; return 1;
    }
}
