// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/domain/definitions.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <set>
#include <utility>

namespace mapgyver::domain {
Id::Id(std::string value) : value_(std::move(value)) {
    if (value_.empty() || value_.size() > 64 ||
        value_.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
        throw ModelError(ErrorCode::invalid_id, "Stable ID must be a nonempty opaque ASCII path component of at most 64 bytes");
}
Id Id::random() {
    std::random_device entropy;
    std::uniform_int_distribution<unsigned> byte(0,255);
    std::array<unsigned,16> bytes{};
    for (auto& value : bytes) value = byte(entropy);
    bytes[6] = (bytes[6] & 15U) | 64U;
    bytes[8] = (bytes[8] & 63U) | 128U;
    constexpr char hex[] = "0123456789abcdef";
    std::string value;
    value.reserve(36);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) value += '-';
        value += hex[bytes[index] >> 4]; value += hex[bytes[index] & 15U];
    }
    return Id{std::move(value)};
}
ModelError::ModelError(ErrorCode code, std::string message, std::vector<Id> involved)
    : std::runtime_error(std::move(message)), code_(code), involved_(std::move(involved)) {}
namespace {
void require(bool valid, ErrorCode code, std::string message, std::vector<Id> involved) {
    if (!valid) throw ModelError(code, std::move(message), std::move(involved));
}
void name(std::string_view value, const Id& owner) {
    require(!value.empty() && value.size() <= 256, ErrorCode::invalid_name, "Display names must contain 1 to 256 UTF-8 bytes", {owner});
    // Validate UTF-8 including overlong encodings, surrogates and non-scalars.
    for (std::size_t offset = 0; offset < value.size();) {
        const auto first = static_cast<unsigned char>(value[offset++]);
        if (first < 128) {
            require(first >= 32 && first != 127, ErrorCode::invalid_name, "Display name contains a control character", {owner});
            continue;
        }
        unsigned continuation = 0, scalar = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { continuation = 1; scalar = first & 31U; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { continuation = 2; scalar = first & 15U; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { continuation = 3; scalar = first & 7U; minimum = 0x10000; }
        else throw ModelError(ErrorCode::invalid_name, "Display name is not valid UTF-8", {owner});
        require(offset + continuation <= value.size(), ErrorCode::invalid_name, "Truncated UTF-8 display name", {owner});
        for (unsigned index = 0; index < continuation; ++index) {
            const auto next = static_cast<unsigned char>(value[offset++]);
            require((next & 0xc0U) == 0x80U, ErrorCode::invalid_name, "Invalid UTF-8 continuation", {owner});
            scalar = (scalar << 6U) | (next & 63U);
        }
        require(scalar >= minimum && scalar <= 0x10ffff && !(scalar >= 0xd800 && scalar <= 0xdfff),
                ErrorCode::invalid_name, "Display name contains an invalid Unicode scalar", {owner});
    }
}
void ports(const std::vector<Port>& values, const Id& owner) {
    std::set<Id> ids; std::set<std::string> names;
    for (const auto& port : values) {
        name(port.name, owner);
        require(ids.insert(port.id).second, ErrorCode::duplicate_id, "Duplicate logical port ID", {owner,port.id});
        require(names.insert(port.name).second, ErrorCode::invalid_port, "Duplicate named port", {owner,port.id});
        require(port.kind == StreamKind::video || port.kind == StreamKind::audio || port.kind == StreamKind::number ||
                port.kind == StreamKind::boolean || port.kind == StreamKind::text || port.kind == StreamKind::trigger,
                ErrorCode::invalid_port, "Unknown stream kind", {owner,port.id});
    }
}
const Asset& find_asset(const Definitions& definitions, const Id& id) {
    const auto found = definitions.assets.find(id);
    if (found == definitions.assets.end()) throw ModelError(ErrorCode::missing_reference, "Missing asset", {id});
    return found->second;
}
const Composition& find_composition(const Definitions& definitions, const Id& id) {
    const auto found = definitions.compositions.find(id);
    if (found == definitions.compositions.end()) throw ModelError(ErrorCode::missing_reference, "Missing composition", {id});
    return found->second;
}
const std::vector<Port>& asset_outputs(const Definitions& definitions, const Id& id) {
    const auto& asset = find_asset(definitions,id);
    if (const auto* external = std::get_if<ExternalSource>(&asset.source)) return external->outputs;
    return find_composition(definitions,std::get<CompositionSource>(asset.source).composition).outputs;
}
const Port& find_port(const std::vector<Port>& values, const Id& port, const Id& owner) {
    const auto found = std::find_if(values.begin(),values.end(),[&](const Port& value){return value.id == port;});
    if (found == values.end()) throw ModelError(ErrorCode::missing_reference, "Missing named port", {owner,port});
    return *found;
}
void validate(const Definitions& definitions) {
    for (const auto& [id,asset] : definitions.assets) {
        require(id == asset.id, ErrorCode::invalid_definition, "Asset map identity differs from the object", {id,asset.id});
        name(asset.name,id);
        if (const auto* external = std::get_if<ExternalSource>(&asset.source)) {
            name(external->adapter,id); // A configured unknown adapter is preserved, not silently rejected/deleted.
            ports(external->outputs,id);
        } else {
            static_cast<void>(find_composition(definitions,std::get<CompositionSource>(asset.source).composition));
        }
    }
    for (const auto& [id,composition] : definitions.compositions) {
        require(id == composition.id, ErrorCode::invalid_definition, "Composition map identity differs from the object", {id,composition.id});
        name(composition.name,id); ports(composition.inputs,id); ports(composition.outputs,id);
        std::set<Id> port_ids, layer_ids;
        for (const auto& port : composition.inputs) port_ids.insert(port.id);
        for (const auto& port : composition.outputs)
            require(port_ids.insert(port.id).second, ErrorCode::duplicate_id, "Composition input/output identities overlap", {id,port.id});
        for (const auto& [port,canvas] : composition.canvases) {
            require(find_port(composition.outputs,port,id).kind == StreamKind::video,ErrorCode::type_mismatch,"Only video output ports own canvases",{id,port});
            require(canvas.width > 0 && canvas.height > 0,ErrorCode::invalid_definition,"Canvas dimensions must be positive",{id,port});
            name(canvas.pixel_format,id); name(canvas.colour_space,id); name(canvas.alpha,id);
        }
        std::map<Id,Id> shared_assets;
        for (const auto& layer : composition.layers) {
            require(layer_ids.insert(layer.id).second, ErrorCode::duplicate_id, "Duplicate layer identity", {id,layer.id});
            name(layer.name,layer.id);
            if (const auto* source = std::get_if<AssetPort>(&layer.source)) {
                const auto& port = find_port(asset_outputs(definitions,source->asset),source->port,source->asset);
                require(port.kind == layer.kind, ErrorCode::type_mismatch, "Source port cannot feed this layer kind", {id,layer.id,source->asset,source->port});
                if (layer.shared_group) {
                    const auto [group,inserted] = shared_assets.emplace(*layer.shared_group,source->asset);
                    require(inserted || group->second == source->asset, ErrorCode::invalid_definition,
                            "A local shared-source group must use the same reusable asset", {id,layer.id,*layer.shared_group});
                }
            } else {
                const auto& input = std::get<InputPort>(layer.source);
                require(!layer.shared_group, ErrorCode::invalid_definition, "Input slots already identify one bound stream; no local shared group applies", {id,layer.id});
                const auto& port = find_port(composition.inputs,input.port,id);
                require(port.kind == layer.kind, ErrorCode::type_mismatch, "Input port cannot feed this layer kind", {id,layer.id,input.port});
            }
        }
    }
    // Iterative DFS avoids a call-stack limit for deeply nested authored graphs.
    // Inputs have no fixed asset dependency; live input bindings need their own
    // instance-graph validation before activation (the next C01 ownership slice).
    std::map<Id,std::vector<Id>> edges;
    for (const auto& [id,asset] : definitions.assets) {
        auto& dependencies = edges[id];
        if (const auto* source = std::get_if<CompositionSource>(&asset.source))
            for (const auto& layer : find_composition(definitions,source->composition).layers)
                if (const auto* binding = std::get_if<AssetPort>(&layer.source)) dependencies.push_back(binding->asset);
    }
    std::map<Id,unsigned> colors;
    struct Visit { Id id; std::size_t next = 0; };
    std::vector<Visit> stack;
    for (const auto& [root,unused] : edges) {
        static_cast<void>(unused);
        if (colors[root] != 0) continue;
        colors[root] = 1; stack.push_back({root,0});
        while (!stack.empty()) {
            auto& visit = stack.back();
            const auto& dependencies = edges.at(visit.id);
            if (visit.next == dependencies.size()) { colors[visit.id] = 2; stack.pop_back(); continue; }
            const auto child = dependencies[visit.next++];
            if (colors[child] == 1) {
                std::vector<Id> involved;
                const auto first = std::find_if(stack.begin(),stack.end(),[&](const Visit& value){return value.id == child;});
                for (auto p = first; p != stack.end(); ++p) involved.push_back(p->id);
                involved.push_back(child);
                throw ModelError(ErrorCode::dependency_cycle,"Composition dependency cycle",std::move(involved));
            }
            if (colors[child] == 0) { colors[child] = 1; stack.push_back({child,0}); }
        }
    }
}
}
void validate_display_name(std::string_view value, const Id& owner) { name(value,owner); }
DefinitionSnapshot::DefinitionSnapshot(Definitions definitions) {
    validate(definitions);
    definitions_ = std::make_shared<const Definitions>(std::move(definitions));
}
const Asset& DefinitionSnapshot::asset(const Id& id) const { return find_asset(*definitions_,id); }
const Composition& DefinitionSnapshot::composition(const Id& id) const { return find_composition(*definitions_,id); }
const std::vector<Port>& DefinitionSnapshot::outputs(const Id& asset) const { return asset_outputs(*definitions_,asset); }
const Port& DefinitionSnapshot::output(const AssetPort& source) const { return find_port(outputs(source.asset),source.port,source.asset); }
} // namespace mapgyver::domain
