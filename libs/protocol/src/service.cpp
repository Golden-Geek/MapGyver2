// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/protocol/service.hpp>
#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include <new>

namespace mapgyver::protocol {
using namespace mapgyver::control;
using namespace mapgyver::domain;
namespace {
void require(bool valid,const char* code,const char* message) {if (!valid) throw ServiceError(code,message);}
void keys(const Json& value,const auto& fields) {
    require(value.is_object() && value.size() == fields.size(),"malformed","Object has missing or unknown fields");
    for (const auto* field : fields) require(value.contains(field),"malformed","Required field is missing");
}
std::string text(const Json& value) {require(value.is_string(),"malformed","Expected a string"); return value.get<std::string>();}
bool boolean(const Json& value) {require(value.is_boolean(),"malformed","Expected a boolean"); return value.get<bool>();}
void version(const Json& value) {require(value.is_number_integer() && value == 1,"unsupported_version","Unsupported control protocol version");}
Json nullable(const std::optional<Id>& id) {return id ? Json(id->str()) : Json(nullptr);}
enum class Op {transaction,parameters,undo,redo,snapshot,registry,subscribe,unsubscribe,claim,transfer,release};
struct Operation {Op op; const char* name; bool editor, revision; std::vector<const char*> fields;};
const std::array operations{
    Operation{Op::transaction,"transaction",true,true,{"commands"}},Operation{Op::parameters,"parameters",true,true,{"writes"}},
    Operation{Op::undo,"undo",true,true,{}},Operation{Op::redo,"redo",true,true,{}},
    Operation{Op::snapshot,"snapshot",false,false,{}},Operation{Op::registry,"registry",false,false,{}},
    Operation{Op::subscribe,"subscribe",false,false,{"afterEvent"}},Operation{Op::unsubscribe,"unsubscribe",false,false,{}},
    Operation{Op::claim,"claimEdit",false,false,{"expectedOwnershipGeneration"}},
    Operation{Op::transfer,"transferEdit",true,false,{"expectedOwnershipGeneration","targetSession"}},
    Operation{Op::release,"releaseEdit",true,false,{"expectedOwnershipGeneration"}}};
const Operation& operation(const std::string& name) {
    const auto found = std::find_if(operations.begin(),operations.end(),[&](const auto& item){return name == item.name;});
    require(found != operations.end(),"unsupported_operation","Control operation is unavailable"); return *found;
}
struct Request {
    Id service, client, session, id;
    std::uint64_t sequence;
    std::optional<std::uint64_t> revision;
    std::optional<MediaTime> apply_at;
    std::string op;
    Json payload;
};
Request decode(const Json& value) {
    keys(value,std::array{"protocolVersion","serviceId","clientId","sessionId","requestId","sequence","expectedRevision","applyAt","operation","payload"});
    version(value.at("protocolVersion"));
    return {Id{text(value.at("serviceId"))},Id{text(value.at("clientId"))},Id{text(value.at("sessionId"))},Id{text(value.at("requestId"))},
        wire::read_unsigned(value.at("sequence")),value.at("expectedRevision").is_null() ? std::nullopt : std::optional{wire::read_unsigned(value.at("expectedRevision"))},
        value.at("applyAt").is_null() ? std::nullopt : std::optional{wire::read_media_time(value.at("applyAt"))},text(value.at("operation")),value.at("payload")};
}
Json error(const std::exception& exception) {
    std::string code = "internal_failure";
    Json ids = Json::array(); std::size_t omitted = 0;
    if (const auto* service = dynamic_cast<const ServiceError*>(&exception)) code = service->code();
    else if (const auto* model = dynamic_cast<const ModelError*>(&exception)) {
        static constexpr std::array names{"invalid_id","duplicate_id","invalid_name","invalid_port","missing_reference","type_mismatch","dependency_cycle","invalid_definition","capacity_exceeded","invalid_transport","invalid_geometry","invalid_layout"};
        code = names.at(static_cast<std::size_t>(model->code()));
        for (const auto& id : model->involved()) if (ids.size() < 32) ids.push_back(id.str()); else ++omitted;
    } else if (const auto* control = dynamic_cast<const ControlError*>(&exception)) {
        static constexpr std::array names{"revision_conflict","empty_transaction","history_empty","capacity_exceeded"}; code = names.at(static_cast<std::size_t>(control->code()));
    } else if (const auto* registry = dynamic_cast<const RegistryError*>(&exception)) {
        static constexpr std::array names{"missing_address","read_only","type_mismatch","out_of_range","stale_snapshot","capacity_exceeded"}; code = names.at(static_cast<std::size_t>(registry->code()));
    } else if (const auto* wire_error = dynamic_cast<const wire::Error*>(&exception)) {
        static constexpr std::array names{"malformed","unsupported_version","capacity_exceeded"}; code = names.at(static_cast<std::size_t>(wire_error->code()));
    }
    return {{"code",code},{"message",code == "internal_failure" ? "Internal control failure" : std::string{exception.what()}.substr(0,512)},
            {"ids",ids},{"omittedIds",std::to_string(omitted)}};
}
}
struct ControlService::State {
    struct Cached {std::uint64_t sequence; Id id; std::shared_ptr<const std::string> request; std::shared_ptr<const Json> reply; std::size_t bytes;};
    struct Session {
        Id client; bool connected = true, subscribed = false;
        std::chrono::steady_clock::time_point retain_until{};
        std::uint64_t next = 0; std::deque<Cached> cache{}; std::size_t cache_bytes = 0;
    };
    Id service; ProjectController controller; std::map<Id,Session> sessions;
    std::optional<Id> owner;
    std::uint64_t ownership_generation = 0, event_sequence = 0;
    std::deque<Json> journal;
    std::shared_ptr<const Json> view;
    State(Id id,ProjectController project) : service(std::move(id)),controller(std::move(project)) {}
    void event(const char* kind,const ServiceLimits& limits) {
        require(event_sequence < std::numeric_limits<std::uint64_t>::max(),"capacity_exceeded","Event sequence exhausted");
        journal.push_back({{"sequence",std::to_string(++event_sequence)},{"kind",kind},
            {"authoredRevision",std::to_string(controller.revision())},{"ownershipGeneration",std::to_string(ownership_generation)}});
        while (journal.size() > limits.journal_events) journal.pop_front();
    }
    void ownership(std::optional<Id> next,const ServiceLimits& limits) {
        require(ownership_generation < std::numeric_limits<std::uint64_t>::max(),"capacity_exceeded","Ownership generation exhausted");
        owner = std::move(next); ++ownership_generation; event("ownershipChanged",limits);
    }
    void prepare_view(const ServiceLimits& limits) {
        auto value = std::make_shared<const Json>(Json{{"project",wire::encode_project(*controller.snapshot())},{"registry",wire::describe(ParameterRegistry{controller})}});
        require(value->dump().size() <= limits.response_bytes-4096,"response_capacity","Authored state exceeds the configured snapshot response budget");
        view = std::move(value);
    }
    Json full_snapshot(const Id& session) const {
        return {{"type","snapshot"},{"serviceId",service.str()},{"authoredRevision",std::to_string(controller.revision())},
            {"eventSequence",std::to_string(event_sequence)},{"editOwner",nullable(owner)},{"isEditor",owner == session},
            {"ownershipGeneration",std::to_string(ownership_generation)},{"data",*view},
            {"renderer",{{"available",false},{"reason","render_worker_not_connected"},{"appliedRevision",nullptr},{"showTime",nullptr},{"frame",nullptr}}},
            {"persistence",{{"available",false},{"reason","durable_storage_not_implemented"},{"savedRevision",nullptr}}}};
    }
    Json events(const Id& session,std::optional<std::uint64_t> after,const ServiceLimits& limits) const {
        require(!after || *after <= event_sequence,"event_cursor_ahead","Event cursor is ahead of this service");
        const bool gap = !after || (!journal.empty() && *after < wire::read_unsigned(journal.front().at("sequence"))-1);
        if (gap) return {{"type","events"},{"resyncRequired",true},{"cursor",std::to_string(event_sequence)},{"events",Json::array()},{"snapshot",full_snapshot(session)}};
        Json batch = Json::array(); auto cursor = *after;
        for (const auto& item : journal) if (wire::read_unsigned(item.at("sequence")) > *after && batch.size() < limits.event_batch) {
            batch.push_back(item); cursor = wire::read_unsigned(item.at("sequence"));
        }
        return {{"type","events"},{"resyncRequired",false},{"cursor",std::to_string(cursor)},{"events",batch},{"snapshot",nullptr}};
    }
    Json response(const Request* request,bool consumed,Json payload,Json failure = nullptr) const {
        return {{"protocolVersion",1},{"type","reply"},{"serviceId",service.str()},
            {"requestId",request ? Json(request->id.str()) : Json(nullptr)},
            {"sequence",request ? Json(std::to_string(request->sequence)) : Json(nullptr)},
            {"consumed",consumed},{"replayed",false},{"status",failure.is_null() ? "accepted" : "rejected"},
            {"authoredRevision",std::to_string(controller.revision())},{"payload",std::move(payload)},{"error",std::move(failure)},
            {"renderer",{{"available",false},{"reason","render_worker_not_connected"},{"appliedRevision",nullptr},{"showTime",nullptr},{"frame",nullptr}}},
            {"persistence",{{"available",false},{"reason","durable_storage_not_implemented"},{"savedRevision",nullptr}}}};
    }
};
ControlService::ControlService(ProjectController controller,ServiceLimits limits,Clock clock,InstancePool::IdGenerator ids)
    : limits_(limits),clock_(std::move(clock)),ids_(std::make_shared<InstancePool::IdGenerator>(std::move(ids))) {
    require(static_cast<bool>(clock_) && static_cast<bool>(*ids_),"invalid_configuration","Control clock and ID allocator are required");
    require(limits.sessions > 0 && limits.cache_entries > 0 && limits.journal_events > 0 && limits.event_batch > 0 &&
        limits.request_bytes > 0 && limits.response_bytes >= 8192 && limits.cache_bytes >= limits.response_bytes &&
        limits.request_bytes <= limits.cache_bytes-limits.response_bytes && limits.resume_window.count() > 0 && limits.resume_window <= std::chrono::hours{24},
        "invalid_configuration","Invalid control service capacities");
    state_ = std::make_unique<State>((*ids_)(),std::move(controller)); state_->prepare_view(limits_);
}
ControlService::~ControlService() = default;
void ControlService::expire() {
    const auto now = clock_();
    std::erase_if(state_->sessions,[&](const auto& pair){return !pair.second.connected && pair.second.retain_until <= now;});
}
Connection ControlService::connect(std::string_view bytes) {
    expire(); const auto message = wire::parse(bytes,{limits_.request_bytes,262144,64});
    keys(message,std::array{"protocolVersion","clientId","resumeSessionId","requestEdit"}); version(message.at("protocolVersion"));
    const Id client{text(message.at("clientId"))}; const bool request_edit = boolean(message.at("requestEdit"));
    auto candidate = std::make_unique<State>(*state_);
    std::optional<Id> session;
    if (!message.at("resumeSessionId").is_null()) {
        session = Id{text(message.at("resumeSessionId"))};
        const auto found = candidate->sessions.find(*session);
        require(found != candidate->sessions.end(),"session_expired","Session retention expired; obtain a new session and snapshot");
        require(found->second.client == client,"session_mismatch","Session belongs to a different client identity");
        require(!found->second.connected,"session_in_use","Session already has a live connection");
        found->second.connected = true; found->second.subscribed = false;
    } else {
        require(candidate->sessions.size() < limits_.sessions,"session_capacity","Session capacity exhausted; retained sessions are not evicted early");
        for (int attempt = 0; attempt < 8; ++attempt) {
            const auto next = (*ids_)();
            if (next != candidate->service && !candidate->sessions.contains(next)) {session = next; break;}
        }
        require(session.has_value(),"duplicate_id","Session ID allocator repeatedly collided");
        candidate->sessions.emplace(*session,State::Session{client});
    }
    if (request_edit && !candidate->owner) candidate->ownership(session,limits_);
    Json available = Json::array();
    for (const auto& item : operations) available.push_back({{"operation",item.name},{"requiresEditor",item.editor},{"requiresRevision",item.revision},{"payloadFields",item.fields}});
    Json reply{{"protocolVersion",1},{"type","hello"},{"serviceId",candidate->service.str()},
        {"clientId",client.str()},{"sessionId",session->str()},{"nextSequence",std::to_string(candidate->sessions.at(*session).next)},
        {"resumeWindowMs",std::to_string(limits_.resume_window.count())},{"operations",available},{"snapshot",candidate->full_snapshot(*session)}};
    require(reply.dump().size() <= limits_.response_bytes,"response_capacity","Hello response exceeds its configured budget");
    Connection connection{*session,std::move(reply)}; state_.swap(candidate); return connection;
}
void ControlService::disconnect(const Id& session) {
    const auto found = state_->sessions.find(session);
    if (found == state_->sessions.end() || !found->second.connected) return;
    auto candidate = std::make_unique<State>(*state_); auto& client = candidate->sessions.at(session);
    const auto now = clock_();
    const auto window = std::chrono::duration_cast<std::chrono::steady_clock::duration>(limits_.resume_window);
    require(now <= std::chrono::steady_clock::time_point::max()-window,"capacity_exceeded","Session retention time exceeds clock range");
    client.connected = false; client.subscribed = false; client.retain_until = now+window;
    if (candidate->owner == session) candidate->ownership(std::nullopt,limits_);
    state_.swap(candidate);
}
Json ControlService::handle(const Id& connection_session,std::string_view bytes) {
    expire(); std::optional<Request> request;
    try {
        const auto document = wire::parse(bytes,{limits_.request_bytes,262144,64}); request = decode(document);
        require(request->service == state_->service,"service_mismatch","Service restarted; reload identity and state");
        const auto found = state_->sessions.find(connection_session);
        require(found != state_->sessions.end() && found->second.connected,"session_expired","Connection session is unavailable");
        require(request->session == connection_session && request->client == found->second.client,"session_mismatch","Envelope does not match the bound connection");
        const auto canonical = std::make_shared<const std::string>(document.dump());
        require(canonical->size() <= limits_.request_bytes,"request_capacity","Canonical request exceeds its configured budget");
        if (request->sequence < found->second.next) {
            const auto cached = std::find_if(found->second.cache.begin(),found->second.cache.end(),[&](const auto& item){return item.sequence == request->sequence;});
            require(cached != found->second.cache.end(),"request_expired","Request outcome left the bounded cache; old sequences never execute again");
            require(*cached->request == *canonical,"request_mismatch","Sequence was already used for a different request");
            auto reply = *cached->reply; reply["replayed"] = true; return reply;
        }
        require(request->sequence == found->second.next,"sequence_gap","Send the next sequence before later requests");
        require(request->sequence < std::numeric_limits<std::uint64_t>::max(),"sequence_exhausted","Session sequence exhausted");
        auto candidate = std::make_unique<State>(*state_); Json reply;
        try {
            require(std::none_of(found->second.cache.begin(),found->second.cache.end(),[&](const auto& item){return item.id == request->id;}),"request_id_reused","Request ID is already retained for another sequence");
            const auto& op = operation(request->op); keys(request->payload,op.fields);
            require(!request->apply_at,"schedule_unavailable","Show-clock scheduling is unavailable until a native worker is connected");
            require(!op.editor || candidate->owner == connection_session,"not_editor","Only the current editing owner can change state");
            require(!op.revision || request->revision.has_value(),"revision_required","An authored revision is required for this operation");
            auto& controller = candidate->controller;
            require(!op.revision || *request->revision == controller.revision(),"revision_conflict","Authored revision is stale");
            ParameterRegistry registry{controller}; Json payload = Json::object();
            const auto previous = controller.revision();
            switch (op.op) {
            case Op::transaction: {
                const auto& values = request->payload.at("commands");
                require(values.is_array() && values.size() <= limits_.operations_per_request,"capacity_exceeded","Command batch is invalid or oversized");
                std::vector<Command> commands; commands.reserve(values.size()); for (const auto& value : values) commands.push_back(wire::decode_command(value));
                static_cast<void>(registry.execute(controller,*request->revision,commands)); break;
            }
            case Op::parameters: {
                const auto& values = request->payload.at("writes");
                require(values.is_array() && values.size() <= limits_.operations_per_request,"capacity_exceeded","Parameter batch is invalid or oversized");
                std::vector<ParameterWrite> writes; writes.reserve(values.size()); for (const auto& value : values) writes.push_back(wire::decode_write(value));
                static_cast<void>(registry.write(controller,*request->revision,writes)); break;
            }
            case Op::undo: static_cast<void>(registry.undo(controller,*request->revision)); break;
            case Op::redo: static_cast<void>(registry.redo(controller,*request->revision)); break;
            case Op::snapshot: payload = candidate->full_snapshot(connection_session); break;
            case Op::registry: payload = candidate->view->at("registry"); break;
            case Op::subscribe: {
                const auto& after = request->payload.at("afterEvent");
                payload = candidate->events(connection_session,after.is_null() ? std::nullopt : std::optional{wire::read_unsigned(after)},limits_);
                candidate->sessions.at(connection_session).subscribed = true; break;
            }
            case Op::unsubscribe: candidate->sessions.at(connection_session).subscribed = false; break;
            case Op::claim: case Op::transfer: case Op::release: {
                require(wire::read_unsigned(request->payload.at("expectedOwnershipGeneration")) == candidate->ownership_generation,"ownership_conflict","Editing ownership generation is stale");
                if (op.op == Op::claim) {
                    require(!candidate->owner || candidate->owner == connection_session,"editor_occupied","Editing ownership is held by another session");
                    if (!candidate->owner) candidate->ownership(connection_session,limits_);
                } else if (op.op == Op::release) candidate->ownership(std::nullopt,limits_);
                else {
                    const Id target{text(request->payload.at("targetSession"))};
                    const auto target_session = candidate->sessions.find(target);
                    require(target_session != candidate->sessions.end() && target_session->second.connected,"session_expired","Transfer target is not connected");
                    candidate->ownership(target,limits_);
                }
                payload = {{"editOwner",nullable(candidate->owner)},{"ownershipGeneration",std::to_string(candidate->ownership_generation)}}; break;
            }
            }
            if (controller.revision() != previous) {candidate->prepare_view(limits_); candidate->event("authoredChanged",limits_);}
            reply = candidate->response(&*request,true,std::move(payload));
            require(reply.dump().size() <= limits_.response_bytes,"response_capacity","Control reply exceeds its configured budget");
        } catch (const std::bad_alloc&) {throw;}
        catch (const std::exception& exception) {candidate = std::make_unique<State>(*state_); reply = candidate->response(&*request,true,nullptr,error(exception));}
        const auto reply_bytes = reply.dump().size();
        require(reply_bytes <= limits_.response_bytes,"response_capacity","Control reply exceeds its configured budget");
        const auto cost = canonical->size()+reply_bytes;
        require(cost <= limits_.cache_bytes,"cache_capacity","Request outcome cannot fit its configured cache");
        auto& session = candidate->sessions.at(connection_session);
        while (session.cache.size() >= limits_.cache_entries || cost > limits_.cache_bytes-session.cache_bytes) {session.cache_bytes -= session.cache.front().bytes; session.cache.pop_front();}
        session.cache.push_back({request->sequence,request->id,canonical,std::make_shared<const Json>(reply),cost}); session.cache_bytes += cost;
        ++session.next; state_.swap(candidate); return reply;
    } catch (const std::bad_alloc&) {throw;}
    catch (const std::exception& exception) {return state_->response(request ? &*request : nullptr,false,nullptr,error(exception));}
}
std::optional<Json> ControlService::notifications(const Id& session,std::uint64_t after_event) const {
    const auto found = state_->sessions.find(session);
    require(found != state_->sessions.end() && found->second.connected,"session_expired","Notification session is unavailable");
    if (!found->second.subscribed || after_event == state_->event_sequence) return std::nullopt;
    auto value = state_->events(session,after_event,limits_);
    require(value.dump().size() <= limits_.response_bytes,"response_capacity","Notification exceeds its configured budget"); return value;
}
std::shared_ptr<const ProjectState> ControlService::snapshot() const {return state_->controller.snapshot();}
std::uint64_t ControlService::revision() const {return state_->controller.revision();}
} // namespace mapgyver::protocol
