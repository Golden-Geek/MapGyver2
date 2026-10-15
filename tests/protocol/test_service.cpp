// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/protocol/service.hpp>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace mapgyver::protocol;
using namespace mapgyver::control;
namespace {
Id id(const char* value) {return Id{value};}
void check(bool valid,const char* message) {if (!valid) throw std::runtime_error(message);}
ProjectController fixture() {
    Definitions definitions;
    definitions.assets.emplace(id("media"),Asset{id("media"),"Movie",ExternalSource{"future.file","movie.mp4",{{id("picture"),"Picture",StreamKind::video}}}});
    return ProjectController{make_default_project(std::move(definitions),[next=0]() mutable {return Id{"project-"+std::to_string(++next)};})};
}
ControlService::Clock clock(std::chrono::steady_clock::time_point& now) {return [&now]{return now;};}
InstancePool::IdGenerator ids() {return [next=0]() mutable {return Id{"service-"+std::to_string(++next)};};}
Json hello(const char* client,bool edit = true,std::optional<Id> resume = std::nullopt) {
    return {{"protocolVersion",1},{"clientId",client},{"resumeSessionId",resume ? Json(resume->str()) : Json(nullptr)},{"requestEdit",edit}};
}
Json request(const Connection& connection,std::uint64_t sequence,const char* op,Json payload = Json::object(),std::optional<std::uint64_t> revision = std::nullopt) {
    return {{"protocolVersion",1},{"serviceId",connection.hello.at("serviceId")},{"clientId",connection.hello.at("clientId")},
        {"sessionId",connection.session.str()},{"requestId","request-"+std::to_string(sequence)},{"sequence",std::to_string(sequence)},
        {"expectedRevision",revision ? Json(std::to_string(*revision)) : Json(nullptr)},{"applyAt",nullptr},{"operation",op},{"payload",payload}};
}
Json execute(ControlService& service,const Connection& connection,const Json& message) {return service.handle(connection.session,message.dump());}
void accepted(const Json& reply,std::uint64_t revision) {
    check(reply.at("status") == "accepted" && reply.at("consumed") == true && reply.at("authoredRevision") == std::to_string(revision),"Command was not accepted at the expected authored revision");
    check(reply.at("renderer").at("appliedRevision").is_null() && reply.at("renderer").at("frame").is_null() &&
          reply.at("persistence").at("savedRevision").is_null(),"Authored acceptance invented a rendered or saved result");
}
void rejected(const Json& reply,const char* code,bool consumed) {
    if (reply.at("status") != "rejected" || reply.at("error").at("code") != code || reply.at("consumed") != consumed)
        throw std::runtime_error(std::string{"Expected rejection "}+code+"; received "+reply.dump());
}
void service_rejects(const char* code,const std::function<void()>& operation) {
    try {operation();} catch (const ServiceError& error) {check(error.code() == code,"Wrong session rejection"); return;}
    throw std::runtime_error("Invalid session operation succeeded");
}
void mutation_deduplication_and_revision() {
    auto now = std::chrono::steady_clock::time_point{};
    ControlService service{fixture(),{},clock(now),ids()};
    const auto owner = service.connect(hello("editor").dump());
    check(owner.hello.at("snapshot").at("isEditor") == true && owner.hello.at("nextSequence") == "0","Initial edit ownership/sequence is wrong");
    const auto baseline = service.snapshot();
    const auto root = baseline->instances.placements().at(baseline->root_placement).source.instance;
    const auto input = baseline->instances.definitions().composition(std::get<CompositionSource>(baseline->instances.definitions().asset(baseline->instances.instance(root).asset).source).composition).inputs.front().id;
    const auto bind = request(owner,0,"transaction",{{"commands",Json::array({wire::encode_command(BindInput{root,input,AssetPort{id("media"),id("picture")}})})}},0);
    const auto reply = execute(service,owner,bind); accepted(reply,1);
    const auto first = service.snapshot();
    const auto replay = execute(service,owner,bind); accepted(replay,1);
    check(replay.at("replayed") == true && service.snapshot() == first && service.revision() == 1,"Lost-ack retry executed a source creation again");
    auto mismatch = bind; mismatch["payload"]["commands"] = Json::array();
    rejected(execute(service,owner,mismatch),"request_mismatch",false);
    rejected(execute(service,owner,request(owner,3,"snapshot")),"sequence_gap",false);
    const auto stale = request(owner,1,"undo",Json::object(),0);
    rejected(execute(service,owner,stale),"revision_conflict",true);
    check(execute(service,owner,stale).at("replayed") == true && service.snapshot() == first,"Rejected retry changed state or bypassed deduplication");
    accepted(execute(service,owner,request(owner,2,"undo",Json::object(),1)),2);
    check(service.snapshot() == baseline,"Remote undo did not restore exact stable IDs and ownership");
    accepted(execute(service,owner,request(owner,3,"redo",Json::object(),2)),3);
    check(service.snapshot() == first,"Remote redo regenerated source state");
    const auto count_path = "/show/sources/"+root.str()+"/ownerReferences";
    rejected(execute(service,owner,request(owner,4,"parameters",{{"writes",Json::array({wire::encode_write({count_path,std::uint64_t{999}})})}},3)),"read_only",true);
    check(service.snapshot() == first && service.revision() == 3,"Derived remote write changed authoritative state");
}
void editing_owner_and_session_binding() {
    auto now = std::chrono::steady_clock::time_point{};
    ControlService service{fixture(),{},clock(now),ids()};
    const auto editor = service.connect(hello("editor").dump()), viewer = service.connect(hello("viewer").dump());
    check(viewer.hello.at("snapshot").at("isEditor") == false,"Second client stole editing ownership");
    rejected(execute(service,viewer,request(viewer,0,"undo",Json::object(),0)),"not_editor",true);
    rejected(execute(service,viewer,request(viewer,1,"claimEdit",{{"expectedOwnershipGeneration","1"}})),"editor_occupied",true);
    auto forged = request(viewer,2,"snapshot"); forged["sessionId"] = editor.session.str();
    rejected(execute(service,viewer,forged),"session_mismatch",false);
    auto other_service = request(viewer,2,"snapshot"); other_service["serviceId"] = "restarted-service";
    rejected(execute(service,viewer,other_service),"service_mismatch",false);
    accepted(execute(service,editor,request(editor,0,"transferEdit",{{"expectedOwnershipGeneration","1"},{"targetSession",viewer.session.str()}})),0);
    rejected(execute(service,editor,request(editor,1,"undo",Json::object(),0)),"not_editor",true);
    rejected(execute(service,viewer,request(viewer,2,"releaseEdit",{{"expectedOwnershipGeneration","1"}})),"ownership_conflict",true);
    accepted(execute(service,viewer,request(viewer,3,"releaseEdit",{{"expectedOwnershipGeneration","2"}})),0);
    accepted(execute(service,editor,request(editor,2,"claimEdit",{{"expectedOwnershipGeneration","3"}})),0);
    check(service.revision() == 0,"Session ownership changed authored project revision");
    service_rejects("session_in_use",[&]{static_cast<void>(service.connect(hello("editor",true,editor.session).dump()));});
}
void reconnect_retention_and_cache_expiry() {
    auto now = std::chrono::steady_clock::time_point{}; ServiceLimits limits; limits.cache_entries = 2; limits.sessions = 2;
    ControlService service{fixture(),limits,clock(now),ids()};
    const auto editor = service.connect(hello("editor").dump());
    const auto original = request(editor,0,"snapshot"); accepted(execute(service,editor,original),0);
    service.disconnect(editor.session);
    const auto other = service.connect(hello("other").dump());
    const auto resumed = service.connect(hello("editor",true,editor.session).dump());
    check(resumed.session == editor.session && resumed.hello.at("nextSequence") == "1" &&
          resumed.hello.at("snapshot").at("isEditor") == false,"Reconnect changed sequence or stole the current editor");
    check(execute(service,resumed,original).at("replayed") == true,"Reconnect lost a retained terminal reply");
    accepted(execute(service,resumed,request(resumed,1,"snapshot")),0);
    accepted(execute(service,resumed,request(resumed,2,"registry")),0);
    rejected(execute(service,resumed,original),"request_expired",false);
    check(service.revision() == 0,"Evicted request executed again");
    service.disconnect(resumed.session);
    service_rejects("session_capacity",[&]{static_cast<void>(service.connect(hello("third").dump()));});
    now += std::chrono::seconds{61};
    service_rejects("session_expired",[&]{static_cast<void>(service.connect(hello("editor",true,editor.session).dump()));});
    const auto fresh = service.connect(hello("fresh").dump());
    check(fresh.session != editor.session && fresh.hello.at("nextSequence") == "0" && other.hello.at("sessionId") != fresh.hello.at("sessionId"),"Expired identity was silently resumed or rewound");
}
void subscriptions_and_bounded_journal() {
    auto now = std::chrono::steady_clock::time_point{}; ServiceLimits limits; limits.journal_events = 2; limits.event_batch = 1;
    ControlService service{fixture(),limits,clock(now),ids()};
    const auto editor = service.connect(hello("editor").dump()), observer = service.connect(hello("observer",false).dump());
    const auto subscribed = execute(service,observer,request(observer,0,"subscribe",{{"afterEvent",nullptr}})); accepted(subscribed,0);
    check(subscribed.at("payload").at("resyncRequired") == true && subscribed.at("payload").at("snapshot").at("data").contains("project"),"Initial subscription did not provide a complete snapshot");
    const auto cursor = wire::read_unsigned(subscribed.at("payload").at("cursor"));
    check(!service.notifications(observer.session,cursor),"Idle subscription invented changes");
    const auto surface = service.snapshot()->stage.data().surfaces.begin()->first;
    for (std::uint64_t index = 0; index < 3; ++index)
        accepted(execute(service,editor,request(editor,index,"parameters",{{"writes",Json::array({wire::encode_write({"/stage/surfaces/"+surface.str()+"/name",std::string{"Surface "+std::to_string(index)}})})}},index)),index+1);
    const auto gap = service.notifications(observer.session,cursor);
    check(gap && gap->at("resyncRequired") == true && gap->at("snapshot").at("authoredRevision") == "3", "Slow subscriber silently missed evicted structural changes");
    const auto tail = wire::read_unsigned(gap->at("cursor"));
    accepted(execute(service,editor,request(editor,3,"parameters",{{"writes",Json::array({wire::encode_write({"/stage/surfaces/"+surface.str()+"/name",std::string{"Next"}})})}},3)),4);
    const auto batch = service.notifications(observer.session,tail);
    check(batch && batch->at("resyncRequired") == false && batch->at("events").size() == 1 && batch->at("events")[0]["kind"] == "authoredChanged", "Resumable bounded notification batch is incorrect");
    const auto delivered = wire::read_unsigned(batch->at("cursor"));
    check(service.notifications(observer.session,tail) == batch && !service.notifications(observer.session,delivered),"Notification replay/cursor progression lost events");
    accepted(execute(service,observer,request(observer,1,"unsubscribe")),4);
    check(!service.notifications(observer.session,tail),"Unsubscribed observer still receives traffic");
    rejected(execute(service,observer,request(observer,2,"subscribe",{{"afterEvent","999"}})),"event_cursor_ahead",true);
}
void rollback_response_capacity_and_malformed_input() {
    auto now = std::chrono::steady_clock::time_point{}; ServiceLimits limits; limits.response_bytes = 65536;
    ControlService service{fixture(),limits,clock(now),ids()}; const auto editor = service.connect(hello("editor").dump());
    const auto baseline = service.snapshot();
    const auto oversized = request(editor,0,"parameters",{{"writes",Json::array({wire::encode_write({"/library/assets/media/locator",std::string(100000,'x')})})}},0);
    rejected(execute(service,editor,oversized),"response_capacity",true);
    check(service.snapshot() == baseline && service.revision() == 0 && execute(service,editor,oversized).at("replayed") == true,"Snapshot capacity rejection partially published or repeated an edit");
    auto scheduled = request(editor,1,"undo",Json::object(),0);
    scheduled["applyAt"] = {{"ticks","10"},{"timeBase",{{"numerator","1"},{"denominator","1000"}}}};
    rejected(execute(service,editor,scheduled),"schedule_unavailable",true);
    rejected(execute(service,editor,request(editor,2,"save")),"unsupported_operation",true);
    auto invalid = request(editor,3,"snapshot"); invalid["extra"] = true;
    rejected(execute(service,editor,invalid),"malformed",false);
    rejected(service.handle(editor.session,"{\"a\":1,\"a\":2}"),"malformed",false);
    auto future = request(editor,3,"snapshot"); future["protocolVersion"] = 2;
    rejected(execute(service,editor,future),"unsupported_version",false);
    auto wrong = request(editor,3,"snapshot"); wrong["sequence"] = 3;
    rejected(execute(service,editor,wrong),"malformed",false);
    auto no_revision = request(editor,3,"undo");
    rejected(execute(service,editor,no_revision),"revision_required",true);
    accepted(execute(service,editor,request(editor,4,"snapshot")),0);
    check(service.snapshot() == baseline,"Malformed/capability failures changed model intent");
}
void byte_cache_eviction_and_structural_batch_rollback() {
    auto now = std::chrono::steady_clock::time_point{}; ServiceLimits limits;
    limits.request_bytes = 65536; limits.response_bytes = 65536; limits.cache_bytes = 131072;
    ControlService service{fixture(),limits,clock(now),ids()}; const auto editor = service.connect(hello("editor").dump());
    const auto original = service.snapshot();
    const auto placed = request(editor,0,"transaction",{{"commands",Json::array({wire::encode_command(PlaceSource{{id("media"),id("picture")}})})}},0);
    accepted(execute(service,editor,placed),1); const auto stable = service.snapshot();
    check(stable->instances.instances().size() == original->instances.instances().size()+1,"Placement command failed to create one independent logical source");
    for (std::uint64_t sequence = 1; sequence <= 20; ++sequence) accepted(execute(service,editor,request(editor,sequence,"snapshot")),1);
    rejected(execute(service,editor,placed),"request_expired",false);
    check(service.snapshot() == stable,"Byte-evicted mutation was executed again");
    auto reused = request(editor,21,"snapshot"); reused["requestId"] = "request-20";
    rejected(execute(service,editor,reused),"request_id_reused",true);
    auto too_large = request(editor,22,"parameters",{{"writes",Json::array({wire::encode_write({"/library/assets/media/locator",std::string(65536,'x')})})}},1);
    rejected(execute(service,editor,too_large),"capacity_exceeded",false);
    accepted(execute(service,editor,request(editor,22,"snapshot")),1);
    const auto root = stable->instances.placements().at(stable->root_placement).source.instance;
    const auto composition = std::get<CompositionSource>(stable->instances.definitions().asset(stable->instances.instance(root).asset).source).composition;
    const auto input = stable->instances.definitions().composition(composition).inputs.front().id;
    auto invalid_stage = stable->stage.data(); invalid_stage.patches.begin()->second.surface = id("missing-surface");
    const auto invalid = request(editor,23,"transaction",{{"commands",Json::array({
        wire::encode_command(BindInput{root,input,AssetPort{id("media"),id("picture")}}),wire::encode_command(ReplaceStage{invalid_stage})})}},1);
    rejected(execute(service,editor,invalid),"missing_reference",true);
    check(service.snapshot() == stable && service.revision() == 1,"A failed remote structural batch published an earlier input/source edit");
}
}
int main() {
    try {
        mutation_deduplication_and_revision(); editing_owner_and_session_binding(); reconnect_retention_and_cache_expiry();
        subscriptions_and_bounded_journal(); rollback_response_capacity_and_malformed_input();
        byte_cache_eviction_and_structural_batch_rollback();
        std::cout << "Passed portable reliable control session contracts\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Control session contract failed: " << error.what() << '\n'; return 1;
    }
}
