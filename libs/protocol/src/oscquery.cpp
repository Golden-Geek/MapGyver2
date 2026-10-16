// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/protocol/oscquery.hpp>
#include <algorithm>
#include <limits>

namespace mapgyver::protocol::oscquery {
using namespace control;
using namespace domain;
namespace {
void require(bool valid,const char* message) {if (!valid) throw osc::Error(message);}
const std::string& string(const osc::Argument& value) {
    const auto* text = std::get_if<std::string>(&value); require(text != nullptr,"OSC argument must be a string"); return *text;
}
Json encoded(const ParameterDescriptor& descriptor) {return wire::encode_write({descriptor.address,descriptor.value}).at("write").at("value");}
std::string signature(const ParameterDescriptor& descriptor) {
    switch (descriptor.type) {
    case ParameterType::boolean: return "T";
    case ParameterType::unsigned_integer:
        return descriptor.range && descriptor.range->maximum <= 9007199254740991.0 ? "h" : "s";
    case ParameterType::real: return "d";
    case ParameterType::rational: return "ss";
    case ParameterType::media_time: return "sss";
    case ParameterType::point2: return "dd";
    default: return "s";
    }
}
bool json_value(ParameterType type) {
    return type == ParameterType::optional_reference || type == ParameterType::layer_source || type == ParameterType::instance_port ||
           type == ParameterType::id_list || type == ParameterType::triangles || type == ParameterType::seams;
}
Json values(const ParameterDescriptor& descriptor) {
    const auto encoded_value = encoded(descriptor).at("value");
    switch (descriptor.type) {
    case ParameterType::rational: return Json::array({encoded_value.at("numerator"),encoded_value.at("denominator")});
    case ParameterType::media_time: return Json::array({encoded_value.at("ticks"),encoded_value.at("timeBase").at("numerator"),encoded_value.at("timeBase").at("denominator")});
    case ParameterType::point2: return Json::array({encoded_value.at("x"),encoded_value.at("y")});
    case ParameterType::unsigned_integer:
        if (signature(descriptor) == "h") return Json::array({std::get<std::uint64_t>(descriptor.value)});
        return Json::array({encoded_value});
    default:
        return Json::array({json_value(descriptor.type) ? Json(encoded_value.dump()) : encoded_value});
    }
}
Json attributes(const ParameterDescriptor& descriptor) {
    const auto type = signature(descriptor);
    Json result{{"FULL_PATH",descriptor.address},{"TYPE",type},{"ACCESS",descriptor.access == Access::read_only ? 1 : 3},
        {"VALUE",values(descriptor)},{"DESCRIPTION",descriptor.description},
        {"X_MAPGYVER",{{"OWNER_ID",descriptor.owner.str()},{"PARAMETER_ID",descriptor.parameter.str()},{"MODEL_TYPE",encoded(descriptor).at("type")},
            {"PERSISTENCE",descriptor.persistence == Persistence::authored ? "authored" : descriptor.persistence == Persistence::resume_checkpoint ? "resumeCheckpoint" : "transient"},
            {"AVAILABLE",descriptor.available},{"AVAILABILITY_REASON",descriptor.availability_reason},
            {"VALUE_ENCODING",json_value(descriptor.type) ? "json" : type == "s" && descriptor.type == ParameterType::unsigned_integer ? "uint64-decimal" : "native"}}}};
    if (descriptor.default_value) result["X_MAPGYVER"]["DEFAULT"] = wire::encode_write({descriptor.address,*descriptor.default_value}).at("write").at("value");
    else result["X_MAPGYVER"]["DEFAULT"] = nullptr;
    if (!descriptor.unit.empty()) result["UNIT"] = std::vector<std::string>(type.size(),descriptor.unit);
    if (descriptor.range) {
        result["X_MAPGYVER"]["NUMERIC_RANGE"] = {{"MIN",descriptor.range->minimum},{"MAX",descriptor.range->maximum}};
        if (type == "h" || type == "d") result["RANGE"] = Json::array({{{"MIN",descriptor.range->minimum},{"MAX",descriptor.range->maximum}}});
    }
    if (descriptor.type == ParameterType::stream_kind) result["RANGE"] = Json::array({{{"VALS",{"video","audio","number","boolean","text","trigger"}}}});
    if (descriptor.type == ParameterType::layout_mode) result["RANGE"] = Json::array({{{"VALS",{"independent","continuous"}}}});
    if (descriptor.type == ParameterType::warp_mode) result["RANGE"] = Json::array({{{"VALS",{"homography","mesh"}}}});
    return result;
}
std::optional<std::string> unescape(std::string_view value) {
    std::string result;
    const auto digit = [](char c)->int {if (c >= '0' && c <= '9') return c-'0'; if (c >= 'a' && c <= 'f') return c-'a'+10; if (c >= 'A' && c <= 'F') return c-'A'+10; return -1;};
    for (std::size_t index = 0; index < value.size(); ++index) {
        char c = value[index];
        if (c == '%') {
            if (value.size()-index < 3 || digit(value[index+1]) < 0 || digit(value[index+2]) < 0) return std::nullopt;
            c = static_cast<char>(digit(value[index+1])*16+digit(value[index+2])); index += 2;
        }
        if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) >= 127) return std::nullopt;
        result += c;
    }
    return result;
}
}
Tree::Tree(const ParameterRegistry& registry,Host host) : host_(std::move(host)) {
    nodes_.emplace("/",Node{{{"FULL_PATH","/"},{"ACCESS",0}}, {}});
    for (auto descriptor : registry.inventory()) {
        insert(descriptor.address,attributes(descriptor)); methods_.insert(descriptor.address);
        parameters_.emplace(descriptor.address,std::move(descriptor));
    }
    for (auto action : registry.actions()) {
        insert(action.address,{{"FULL_PATH",action.address},{"TYPE","s"},{"ACCESS",2},{"DESCRIPTION",action.description},
            {"X_MAPGYVER",{{"ACTION_ID",action.id.str()},{"PAYLOAD_TYPE",action.payload_type},
                {"VALUE_ENCODING",action.payload_type == "expectedRevision" ? "uint64-decimal-revision" : "json-command-and-revision"},
                {"RELIABILITY","Use the reliable command envelope for acknowledgements and retry deduplication"}}}});
        methods_.insert(action.address); actions_.emplace(action.address,std::move(action));
    }
}
void Tree::insert(const std::string& address,Json attributes) {
    require(!address.empty() && address.front() == '/' && address.back() != '/',"Invalid canonical OSCQuery address");
    std::string parent = "/"; std::size_t start = 1;
    while (start < address.size()) {
        const auto separator = address.find('/',start); const auto end = separator == std::string::npos ? address.size() : separator;
        const auto name = address.substr(start,end-start); require(!name.empty(),"Empty canonical path component");
        const auto path = address.substr(0,end);
        nodes_.try_emplace(path,Node{{{"FULL_PATH",path},{"ACCESS",0}}, {}});
        nodes_.at(parent).children.emplace(name,path); parent = path; start = end+1;
    }
    require(!nodes_.at(address).attributes.contains("TYPE"),"Duplicate OSCQuery method");
    nodes_.at(address).attributes.update(attributes);
}
Json Tree::subtree(const std::string& address) const {
    const auto& node = nodes_.at(address); auto result = node.attributes;
    if (!node.children.empty()) {
        result["CONTENTS"] = Json::object();
        for (const auto& [name,path] : node.children) result["CONTENTS"][name] = subtree(path);
    }
    return result;
}
Json Tree::host_info() const {
    Json result{{"NAME",host_.name},{"EXTENSIONS",{{"ACCESS",true},{"VALUE",true},{"RANGE",true},{"DESCRIPTION",true},{"UNIT",true},
        {"LISTEN",host_.websockets},{"PATH_CHANGED",host_.websockets}}},
        {"X_MAPGYVER",{{"OSC_STRINGS","UTF-8"},{"BUNDLE_TIMETAGS","immediate-only; other timetags are rejected until native show scheduling exists"},
            {"INTEGER_ENCODING","Exact decimal strings where an OSCQuery JSON number cannot preserve the model range"}}}};
    if (host_.osc_port) result.update({{"OSC_IP",host_.address},{"OSC_PORT",host_.osc_port},{"OSC_TRANSPORT","UDP"}});
    if (host_.websockets && host_.websocket_port) result.update({{"WS_IP",host_.address},{"WS_PORT",host_.websocket_port}});
    return result;
}
HttpResult Tree::query(std::string_view target) const {
    if (target.size() > 8192) return {400,{{"error","query_too_large"}}};
    target = target.substr(0,target.find('#'));
    const auto separator = target.find('?'); const auto path = unescape(target.substr(0,separator));
    const auto attribute = separator == std::string_view::npos ? std::optional<std::string>{std::string{}} : unescape(target.substr(separator+1));
    if (!path || !attribute || path->empty() || path->front() != '/') return {400,{{"error","invalid_query"}}};
    if (*attribute == "HOST_INFO") return {200,host_info()};
    const auto found = nodes_.find(*path); if (found == nodes_.end()) return {404,{{"error","missing_path"}}};
    if (attribute->empty()) return {200,subtree(*path)};
    static const std::set<std::string> known{"FULL_PATH","CONTENTS","TYPE","ACCESS","VALUE","RANGE","DESCRIPTION","UNIT","X_MAPGYVER"};
    if (!known.contains(*attribute)) return {400,{{"error","unsupported_attribute"}}};
    if (*attribute == "VALUE" && (found->second.attributes.at("ACCESS").get<unsigned>()&1U) == 0) return {204,nullptr};
    if (*attribute == "CONTENTS") {
        const auto all = subtree(*path); return {200,all.contains("CONTENTS") ? Json{{"CONTENTS",all.at("CONTENTS")}} : Json::object()};
    }
    return {200,found->second.attributes.contains(*attribute) ? Json{{*attribute,found->second.attributes.at(*attribute)}} : Json::object()};
}
ParameterWrite Tree::write(const osc::Message& message) const {
    const auto found = parameters_.find(message.address); require(found != parameters_.end(),"OSC parameter path is missing");
    const auto& descriptor = found->second;
    if (descriptor.access != Access::read_write) throw RegistryError(RegistryErrorCode::read_only,"OSC parameter is read-only");
    const auto expected = signature(descriptor), actual = osc::types(message);
    require(actual == expected || (expected == "T" && actual == "F"),"OSC message type/quantity differs from advertised TYPE");
    Json value;
    switch (descriptor.type) {
    case ParameterType::boolean: value = std::get<bool>(message.arguments.at(0)); break;
    case ParameterType::real: value = std::get<double>(message.arguments.at(0)); break;
    case ParameterType::unsigned_integer:
        if (expected == "h") {const auto item = std::get<std::int64_t>(message.arguments.at(0)); require(item >= 0,"Unsigned OSC value cannot be negative"); value = std::to_string(item);}
        else value = string(message.arguments.at(0));
        break;
    case ParameterType::point2: value = {{"x",std::get<double>(message.arguments.at(0))},{"y",std::get<double>(message.arguments.at(1))}}; break;
    case ParameterType::rational: value = {{"numerator",string(message.arguments.at(0))},{"denominator",string(message.arguments.at(1))}}; break;
    case ParameterType::media_time: value = {{"ticks",string(message.arguments.at(0))},{"timeBase",{{"numerator",string(message.arguments.at(1))},{"denominator",string(message.arguments.at(2))}}}}; break;
    default: value = json_value(descriptor.type) ? wire::parse(string(message.arguments.at(0))) : Json(string(message.arguments.at(0))); break;
    }
    return wire::decode_write({{"schemaVersion",1},{"write",{{"address",message.address},{"value",{{"type",encoded(descriptor).at("type")},{"value",value}}}}}});
}
ActionInvocation Tree::invoke(const osc::Message& message) const {
    const auto found = actions_.find(message.address); require(found != actions_.end(),"OSC action path is missing");
    require(osc::types(message) == "s","OSC structural action requires its advertised string argument");
    const auto& payload = string(message.arguments.at(0));
    if (found->second.payload_type == "expectedRevision") {
        require(found->second.id.str() == "history-undo" || found->second.id.str() == "history-redo","Unsupported revision-only OSC action");
        const auto revision = wire::read_unsigned(Json(payload));
        return {std::nullopt,found->second.id.str() == "history-undo",revision};
    }
    const auto document = wire::parse(payload);
    require(document.is_object() && document.size() == 2 && document.contains("expectedRevision") && document.contains("command"),"OSC action needs command and expectedRevision");
    const auto command = wire::decode_command(document.at("command"));
    require(wire::encode_command(command).at("command").at("type") == found->second.payload_type,"OSC action payload belongs to another structural method");
    return {command,false,wire::read_unsigned(document.at("expectedRevision"))};
}
osc::Message Tree::value(const std::string& address) const {
    const auto found = parameters_.find(address); require(found != parameters_.end(),"OSC value path is missing");
    const auto type = signature(found->second); const auto data = values(found->second);
    osc::Message result{address,{}};
    for (std::size_t index = 0; index < type.size(); ++index) {
        switch (type[index]) {
        case 's': result.arguments.emplace_back(data.at(index).get<std::string>()); break;
        case 'h': result.arguments.emplace_back(data.at(index).get<std::int64_t>()); break;
        case 'd': result.arguments.emplace_back(data.at(index).get<double>()); break;
        case 'T': result.arguments.emplace_back(data.at(index).get<bool>()); break;
        default: throw osc::Error("Unsupported projected OSC type");
        }
    }
    return result;
}
Changes Tree::changes_from(const Tree& previous) const {
    Changes changes; changes.structure = methods_ != previous.methods_;
    for (const auto& [path,node] : nodes_) {
        const auto old = previous.nodes_.find(path);
        if (old == previous.nodes_.end()) {changes.structure = true; continue;}
        auto attributes = node.attributes, old_attributes = old->second.attributes;
        attributes.erase("VALUE"); old_attributes.erase("VALUE");
        if (attributes != old_attributes || node.children != old->second.children) changes.structure = true;
        if (node.attributes.contains("VALUE") && (!old->second.attributes.contains("VALUE") || node.attributes.at("VALUE") != old->second.attributes.at("VALUE")))
            changes.values.push_back(value(path));
    }
    return changes;
}
void Subscription::command(const Json& message,const Tree& tree) {
    require(message.is_object() && message.size() == 2 && message.contains("COMMAND") && message.contains("DATA") &&
            message.at("COMMAND").is_string() && message.at("DATA").is_string(),"Malformed OSCQuery WebSocket command");
    const auto& path = message.at("DATA").get_ref<const std::string&>();
    const auto& command = message.at("COMMAND").get_ref<const std::string&>();
    if (command == "IGNORE") {paths_.erase(path); return;}
    require(command == "LISTEN","Unsupported OSCQuery WebSocket command");
    require(tree.contains(path),"OSCQuery LISTEN path is missing");
    require(paths_.contains(path) || paths_.size() < capacity_,"OSCQuery subscription capacity exceeded");
    paths_.insert(path);
}
Notifications Subscription::select(const Changes& changes,const Tree& tree) {
    std::erase_if(paths_,[&](const auto& path){return !tree.contains(path);});
    Notifications result;
    if (changes.structure) result.structure = Json{{"COMMAND","PATH_CHANGED"},{"DATA","/"}};
    for (const auto& value : changes.values) if (paths_.contains(value.address)) result.values.push_back(value);
    return result;
}
Ingress::Ingress(ControlService& service)
    : service_(service),connection_(service.connect(Json{{"protocolVersion",1},{"clientId","external-osc"},{"resumeSessionId",nullptr},{"requestEdit",false}}.dump())) {}
Json Ingress::receive(std::span<const std::uint8_t> bytes) {
    require(!closed_,"External OSC channel is closed");
    const auto packet = osc::decode(bytes);
    require(packet.immediate(),"Non-immediate OSC timetags require an unavailable native show scheduler");
    if (packet.messages.empty()) return {{"status","ignored"},{"reason","empty_bundle"}};
    Tree tree{service_.registry()};
    std::vector<control::ParameterWrite> writes; std::vector<control::Command> commands;
    std::optional<std::uint64_t> revision; std::optional<bool> undo;
    for (const auto& message : packet.messages) {
        if (message.address.starts_with("/commands/")) {
            const auto action = tree.invoke(message);
            require(!revision || *revision == action.revision,"OSC bundle has conflicting authored revisions"); revision = action.revision;
            if (action.command) commands.push_back(*action.command);
            else {require(packet.messages.size() == 1,"OSC history triggers must be sent individually"); undo = action.undo;}
        } else writes.push_back(tree.write(message));
    }
    require(writes.empty() || commands.empty(),"Mixed parameter/structural OSC bundles require an explicit structural transaction");
    const auto operation = undo ? (*undo ? "undo" : "redo") : commands.empty() ? "parameters" : "transaction";
    Json payload = Json::object();
    if (!writes.empty()) {payload["writes"] = Json::array(); for (const auto& write : writes) payload["writes"].push_back(control::wire::encode_write(write));}
    if (!commands.empty()) {payload["commands"] = Json::array(); for (const auto& command : commands) payload["commands"].push_back(control::wire::encode_command(command));}
    const Json request{{"protocolVersion",1},{"serviceId",connection_.hello.at("serviceId")},{"clientId","external-osc"},
        {"sessionId",connection_.session.str()},{"requestId","osc-"+std::to_string(sequence_)},{"sequence",std::to_string(sequence_)},
        {"expectedRevision",std::to_string(revision.value_or(service_.revision()))},{"applyAt",nullptr},{"operation",operation},{"payload",payload}};
    auto reply = service_.handle(connection_.session,request.dump());
    if (reply.at("consumed").get<bool>()) ++sequence_;
    return reply;
}
void Ingress::close() {if (!closed_) {service_.disconnect(connection_.session); closed_ = true;}}
} // namespace mapgyver::protocol::oscquery
