// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/protocol/oscquery.hpp>
#include <mapgyver/protocol/service.hpp>
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>

using namespace mapgyver::control;
namespace osc = mapgyver::protocol::osc;
namespace query = mapgyver::protocol::oscquery;
namespace {
void check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
void rejects(const std::function<void()>& operation) {
    try {operation();} catch (const osc::Error&) {return;}
    throw std::runtime_error("Invalid OSCQuery operation was accepted");
}
ProjectController fixture() {
    Definitions definitions;
    definitions.assets.emplace(Id{"media"},Asset{Id{"media"},"Media",ExternalSource{"future.media","relative.mov",
        {{Id{"picture"},"Picture",StreamKind::video},{Id{"order"},"Order collision",StreamKind::video}}}});
    return ProjectController{make_default_project(std::move(definitions))};
}
void collect(const query::Json& node,std::set<std::string>& methods) {
    check(node.contains("FULL_PATH"),"OSCQuery node omitted FULL_PATH");
    if (node.contains("TYPE")) {
        check(methods.insert(node.at("FULL_PATH").get<std::string>()).second,"OSCQuery method appears twice");
        if (node.at("ACCESS") != 2) {
            check(node.contains("VALUE") && node.at("VALUE").size() == node.at("TYPE").get<std::string>().size(),"OSCQuery value arity disagrees with TYPE");
            if (node.contains("UNIT")) check(node.at("UNIT").size() == node.at("VALUE").size(),"OSCQuery unit arity disagrees with TYPE");
        }
    }
    if (node.contains("CONTENTS")) for (const auto& child : node.at("CONTENTS")) collect(child,methods);
}
void inventory_http_metadata_and_exact_types() {
    auto controller = fixture(); ParameterRegistry registry{controller}; query::Tree tree{registry};
    std::set<std::string> expected; for (const auto& field : registry.inventory()) expected.insert(field.address);
    for (const auto& action : registry.actions()) expected.insert(action.address);
    std::set<std::string> exported; collect(tree.query("/").body,exported);
    check(exported == expected && tree.methods() == expected,"OSCQuery inventory differs from the authoritative registry");
    for (const auto& field : registry.inventory()) {
        const auto result = tree.query(field.address); check(result.status == 200,"Registered parameter is absent from HTTP projection");
        check(result.body.at("ACCESS") == (field.access == Access::read_only ? 1 : 3) &&
              result.body.at("X_MAPGYVER").at("OWNER_ID") == field.owner.str() && result.body.at("X_MAPGYVER").at("AVAILABLE") == field.available,
              "OSCQuery access/identity/capability metadata changed");
        if (field.access == Access::read_write) check(tree.write(tree.value(field.address)).value == field.value,"Projected OSC value cannot return through its typed setter mapping");
    }
    check(tree.query("/missing").status == 404 && tree.query("/?unknown").status == 400 &&
          tree.query("/%ff").status == 400 && tree.query("/?TYPE").body.empty(),"OSCQuery query/error semantics changed");
    check(tree.query("/commands/history/undo?VALUE").status == 204,"Trigger query invented a readable value");
    check(tree.query("/ignored/path?HOST_INFO").status == 200 && tree.host_info().at("EXTENSIONS").at("LISTEN") == false,
          "HOST_INFO depends on the path or advertises an unimplemented WebSocket transport");
    check(tree.query("/library/assets/media/outputs/order").body.contains("CONTENTS") && tree.query("/library/assets/media/outputs/order").body.contains("TYPE"),
          "Stable entity ID colliding with an order field destroyed the method/container projection");
    rejects([&]{static_cast<void>(tree.write({"/library/assets/media/name",{1.0}}));});
}
void round_trip_precision_and_validated_mutation() {
    auto controller = fixture(); const auto original = controller.snapshot();
    const auto root = original->instances.placements().at(original->root_placement).source.instance;
    const auto path = "/show/sources/"+root.str()+"/transport/position";
    query::Tree tree{ParameterRegistry{controller}};
    const auto change = tree.write({path,{std::string{"9007199254740993"},std::string{"1001"},std::string{"30000"}}});
    static_cast<void>(ParameterRegistry{controller}.write(controller,0,{change}));
    query::Tree current{ParameterRegistry{controller}};
    const auto metadata = current.query(path).body;
    check(metadata.at("TYPE") == "sss" && metadata.at("VALUE")[0] == "9007199254740993" &&
          std::get<MediaTime>(current.write(current.value(path)).value).ticks == 9007199254740993LL,"OSC/OSCQuery rounded a rational media timestamp");
    check(controller.snapshot()->stage.data().patches == original->stage.data().patches,"OSC transport intent rewrote calibration");
    const auto count_path = "/show/sources/"+root.str()+"/ownerReferences";
    try {static_cast<void>(current.write(current.value(count_path))); throw std::runtime_error("OSC writable mirror bypassed read-only access");}
    catch (const RegistryError& error) {check(error.code() == RegistryErrorCode::read_only,"Wrong readonly rejection");}
    const auto output = original->stage.data().outputs.begin()->first;
    const auto width_path = "/outputs/"+output.str()+"/desired/width";
    check(current.query(width_path).body.at("TYPE") == "h","Bounded dimensions are not exposed as native OSC integers");
    rejects([&]{static_cast<void>(current.write({width_path,{std::int32_t{1920}}}));});
    rejects([&]{static_cast<void>(current.write({width_path,{std::int64_t{-1}}}));});
    const auto too_big = current.write({width_path,{std::int64_t{4294967296LL}}});
    try {static_cast<void>(ParameterRegistry{controller}.write(controller,1,{too_big})); throw std::runtime_error("OSC range bypassed domain registry validation");}
    catch (const RegistryError& error) {check(error.code() == RegistryErrorCode::out_of_range,"Wrong scalar range rejection");}
    check(controller.revision() == 1,"Rejected OSC write changed authored state");
}
void structural_actions_and_diffs() {
    auto controller = fixture(); query::Tree before{ParameterRegistry{controller}};
    const auto rename = before.write({"/library/assets/media/name",{std::string{"Renamed"}}});
    static_cast<void>(ParameterRegistry{controller}.write(controller,0,{rename})); query::Tree after{ParameterRegistry{controller}};
    const auto renamed = after.changes_from(before);
    check(!renamed.structure && renamed.values.size() == 1 && renamed.values.front().address == "/library/assets/media/name" && after.methods() == before.methods(),
          "Rename changed canonical structure or omitted the value notification");
    const auto command = wire::encode_command(PlaceSource{{Id{"media"},Id{"picture"}}});
    const query::Json payload{{"expectedRevision","1"},{"command",command}};
    const auto action = after.invoke({"/commands/sources/place",{payload.dump()}});
    check(action.command.has_value() && action.revision == 1,"Structural OSC action lost its expected revision");
    static_cast<void>(ParameterRegistry{controller}.execute(controller,action.revision,{*action.command}));
    query::Tree structural{ParameterRegistry{controller}};
    check(structural.changes_from(after).structure,"Source creation did not invalidate OSCQuery structure");
    rejects([&]{static_cast<void>(after.invoke({"/commands/definitions/replace",{payload.dump()}}));});
    const auto undo = structural.invoke({"/commands/history/undo",{std::string{"2"}}});
    check(undo.undo && !undo.command && undo.revision == 2,"Undo trigger metadata/mapping changed");
    static_cast<void>(ParameterRegistry{controller}.undo(controller,undo.revision));
    const auto redo = query::Tree{ParameterRegistry{controller}}.invoke({"/commands/history/redo",{std::string{"3"}}});
    check(!redo.undo && !redo.command && redo.revision == 3,"Redo trigger metadata/mapping changed");
}
void exact_path_listen_ignore_and_reconnect() {
    auto controller = fixture(); query::Tree before{ParameterRegistry{controller}};
    query::Subscription parent, exact{1};
    parent.command({{"COMMAND","LISTEN"},{"DATA","/library/assets/media"}},before);
    exact.command({{"COMMAND","LISTEN"},{"DATA","/library/assets/media/name"}},before);
    exact.command({{"COMMAND","LISTEN"},{"DATA","/library/assets/media/name"}},before);
    rejects([&]{exact.command({{"COMMAND","LISTEN"},{"DATA","/library/assets/media/locator"}},before);});
    rejects([&]{parent.command({{"COMMAND","LISTEN"},{"DATA","/missing"}},before);});
    const auto value = before.write({"/library/assets/media/name",{std::string{"Changed"}}});
    static_cast<void>(ParameterRegistry{controller}.write(controller,0,{value}));
    query::Tree after{ParameterRegistry{controller}}; const auto changes = after.changes_from(before);
    check(parent.select(changes,after).values.empty() && exact.select(changes,after).values.size() == 1,
          "OSCQuery LISTEN recursively subscribed or failed exact-path selection");
    const auto binary = osc::encode(exact.select(changes,after).values.front());
    check(osc::decode(binary).messages.front().address == "/library/assets/media/name","OSCQuery value notification is not a standard binary OSC message");
    exact.command({{"COMMAND","IGNORE"},{"DATA","/library/assets/media/name"}},after);
    check(exact.select(changes,after).values.empty() && query::Subscription{}.select(changes,after).values.empty(),
          "IGNORE or a new connection retained a prior subscription");
    exact.command({{"COMMAND","LISTEN"},{"DATA","/library/assets/media/locator"}},after);
    auto definitions = controller.snapshot()->instances.definitions().data(); definitions.assets.erase(Id{"media"});
    static_cast<void>(ParameterRegistry{controller}.execute(controller,1,{ReplaceDefinitions{definitions}}));
    query::Tree removed{ParameterRegistry{controller}}; const auto notifications = exact.select(removed.changes_from(after),removed);
    check(exact.size() == 0 && notifications.structure && notifications.structure->at("COMMAND") == "PATH_CHANGED" &&
          notifications.structure->at("DATA") == "/","Removed methods retained listeners or failed structure invalidation");
}
void packet_ingress_uses_the_same_atomic_owner_validator() {
    mapgyver::protocol::ControlService service{fixture()};
    const auto editor = service.connect(query::Json{{"protocolVersion",1},{"clientId","editor"},{"resumeSessionId",nullptr},{"requestEdit",true}}.dump());
    query::Ingress ingress{service};
    const auto rename = osc::encode({"/library/assets/media/name",{std::string{"OSC edit"}}});
    check(ingress.receive(rename).at("error").at("code") == "not_editor" && service.revision() == 0,"External OSC channel stole edit ownership");
    const query::Json transfer{{"protocolVersion",1},{"serviceId",editor.hello.at("serviceId")},{"clientId","editor"},{"sessionId",editor.session.str()},
        {"requestId","transfer"},{"sequence","0"},{"expectedRevision",nullptr},{"applyAt",nullptr},{"operation","transferEdit"},
        {"payload",{{"expectedOwnershipGeneration","1"},{"targetSession",ingress.session().str()}}}};
    check(service.handle(editor.session,transfer.dump()).at("status") == "accepted","Explicit editing owner could not grant the external OSC channel");
    const auto original = service.snapshot();
    const auto root = original->instances.placements().at(original->root_placement).source.instance;
    const auto invalid_rate = osc::encode({"/show/sources/"+root.str()+"/transport/rate",{std::string{"1"},std::string{"0"}}});
    check(ingress.receive(osc::bundle({rename,invalid_rate})).at("error").at("code") == "invalid_transport" && service.snapshot() == original,
          "OSC bundle partially published before a later validation failure");
    rejects([&]{static_cast<void>(ingress.receive(osc::bundle({rename},osc::Timetag{2})));});
    check(service.snapshot() == original,"Unsupported scheduled OSC changed state");
    const auto accepted = ingress.receive(rename);
    check(accepted.at("status") == "accepted" && service.revision() == 1 && accepted.at("renderer").at("appliedRevision").is_null(),
          "OSC value did not enter the authoritative model or fabricated native application");
    const query::Json action{{"expectedRevision","1"},{"command",wire::encode_command(PlaceSource{{Id{"media"},Id{"picture"}}})}};
    const auto place = osc::encode({"/commands/sources/place",{action.dump()}});
    check(ingress.receive(place).at("status") == "accepted" && service.revision() == 2,"Structural OSC action did not use the domain command validator");
    const auto placed = service.snapshot();
    check(ingress.receive(place).at("error").at("code") == "revision_conflict" && service.snapshot() == placed,
          "Repeated stale UDP-style structural action created another source");
    const query::Json next_action{{"expectedRevision","2"},{"command",wire::encode_command(PlaceSource{{Id{"media"},Id{"picture"}}})}};
    rejects([&]{static_cast<void>(ingress.receive(osc::bundle({rename,osc::encode({"/commands/sources/place",{next_action.dump()}})})));});
    check(service.snapshot() == placed,"Rejected mixed OSC bundle partially changed state");
    ingress.close(); check(service.snapshot() == placed,"OSC channel disconnect rewrote project content/calibration");
}
}
int main() {
    try {inventory_http_metadata_and_exact_types(); round_trip_precision_and_validated_mutation(); structural_actions_and_diffs(); exact_path_listen_ignore_and_reconnect(); packet_ingress_uses_the_same_atomic_owner_validator();
        std::cout << "Passed registry-backed OSCQuery projection contracts\n"; return 0;}
    catch (const std::exception& error) {std::cerr << "OSCQuery contract failed: " << error.what() << '\n'; return 1;}
}
