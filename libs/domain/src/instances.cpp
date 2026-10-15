// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/domain/instances.hpp>
#include <algorithm>
#include <utility>

namespace mapgyver::domain {
namespace {
void require(bool valid, ErrorCode code, const char* message, std::vector<Id> involved = {}) {
    if (!valid) throw ModelError(code,message,std::move(involved));
}
}
InstancePool::InstancePool(DefinitionSnapshot definitions, InstanceLimits limits, IdGenerator ids)
    : definitions_(std::move(definitions)), limits_(limits), ids_(std::move(ids)) {
    require(static_cast<bool>(ids_),ErrorCode::invalid_definition,"Instance ID generator is missing");
}
const SourceInstance& InstancePool::instance(const Id& id) const {
    const auto found = instances_.find(id);
    if (found == instances_.end()) throw ModelError(ErrorCode::missing_reference,"Source instance does not exist",{id});
    return found->second;
}
Id InstancePool::fresh_id() {
    for (int attempt = 0; attempt < 8; ++attempt) {
        auto id = ids_();
        if (!instances_.contains(id) && !placements_.contains(id)) return id;
    }
    throw ModelError(ErrorCode::duplicate_id,"Instance ID generation repeatedly collided");
}
void InstancePool::acquire(const Id& id) {
    require(bindings_ < limits_.ownership_bindings,ErrorCode::capacity_exceeded,"Source ownership-binding capacity exceeded",{id});
    ++instances_.at(id).owner_references; ++bindings_;
}
Id InstancePool::instantiate(const Id& asset, bool keep_alive) {
    const auto create = [&](const Id& definition, bool keep) {
        static_cast<void>(definitions_.asset(definition));
        require(instances_.size() < limits_.instances,ErrorCode::capacity_exceeded,"Live source-instance capacity exceeded",{definition});
        auto id = fresh_id();
        instances_.emplace(id,SourceInstance{id,definition,{},keep,0,{},{}});
        return id;
    };
    const auto root = create(asset,keep_alive);
    std::vector<Id> pending{root};
    while (!pending.empty()) {
        const auto parent = pending.back(); pending.pop_back();
        const auto& definition = definitions_.asset(instances_.at(parent).asset);
        const auto* nested = std::get_if<CompositionSource>(&definition.source);
        if (nested == nullptr) continue;
        std::map<Id,Id> local_shared;
        for (const auto& layer : definitions_.composition(nested->composition).layers) {
            const auto* source = std::get_if<AssetPort>(&layer.source);
            if (source == nullptr) continue; // This composition instance's input slot is initially unbound.
            std::optional<Id> child;
            if (layer.shared_group) {
                const auto found = local_shared.find(*layer.shared_group);
                if (found != local_shared.end()) child = found->second;
            }
            if (!child) {
                child = create(source->asset,false);
                pending.push_back(*child);
                if (layer.shared_group) local_shared.emplace(*layer.shared_group,*child);
            }
            acquire(*child);
            instances_.at(parent).layers.emplace(layer.id,InstancePort{*child,source->port});
        }
    }
    return root;
}
const Port& InstancePool::output_port(const InstancePort& source) const {
    return definitions_.output({instance(source.instance).asset,source.port});
}
const Port& InstancePool::input_port(const Id& owner, const Id& port) const {
    const auto& source = definitions_.asset(instance(owner).asset);
    const auto* nested = std::get_if<CompositionSource>(&source.source);
    require(nested != nullptr,ErrorCode::type_mismatch,"Only composition instances expose content input slots",{owner,port});
    const auto& inputs = definitions_.composition(nested->composition).inputs;
    const auto found = std::find_if(inputs.begin(),inputs.end(),[&](const Port& value){return value.id == port;});
    if (found == inputs.end()) throw ModelError(ErrorCode::missing_reference,"Composition input port is missing",{owner,port});
    return *found;
}
Placement InstancePool::place(const AssetPort& source, bool keep_alive) {
    static_cast<void>(definitions_.output(source));
    InstancePool candidate = *this;
    require(candidate.placements_.size() < limits_.placements,ErrorCode::capacity_exceeded,"Placement capacity exceeded",{source.asset});
    const auto child = candidate.instantiate(source.asset,keep_alive);
    Placement result{candidate.fresh_id(),{child,source.port}};
    candidate.acquire(child); candidate.placements_.emplace(result.id,result);
    *this = std::move(candidate);
    return result;
}
Placement InstancePool::place_shared(const InstancePort& source) {
    static_cast<void>(output_port(source));
    InstancePool candidate = *this;
    require(candidate.placements_.size() < limits_.placements,ErrorCode::capacity_exceeded,"Placement capacity exceeded",{source.instance});
    Placement result{candidate.fresh_id(),source};
    candidate.acquire(source.instance); candidate.placements_.emplace(result.id,result);
    *this = std::move(candidate);
    return result;
}
void InstancePool::collect(const Id& root, std::vector<Id>& retired) {
    std::vector<Id> pending{root};
    while (!pending.empty()) {
        const auto id = pending.back(); pending.pop_back();
        const auto found = instances_.find(id);
        require(found != instances_.end(),ErrorCode::invalid_definition,"Internal source ownership reference is missing",{id});
        if (found->second.owner_references != 0 || found->second.keep_alive) continue;
        auto node = std::move(found->second); instances_.erase(found);
        retired.push_back(id);
        const auto decrement = [&](const InstancePort& port) {
            auto& child = instances_.at(port.instance);
            require(child.owner_references > 0 && bindings_ > 0,ErrorCode::invalid_definition,"Internal source reference count underflow",{port.instance});
            --child.owner_references; --bindings_;
            if (child.owner_references == 0 && !child.keep_alive) pending.push_back(port.instance);
        };
        for (const auto& [layer,source] : node.layers) { static_cast<void>(layer); decrement(source); }
        for (const auto& [input,source] : node.inputs) { static_cast<void>(input); decrement(source); }
    }
}
void InstancePool::release(const Id& id, std::vector<Id>& retired) {
    auto& node = instances_.at(id);
    require(node.owner_references > 0 && bindings_ > 0,ErrorCode::invalid_definition,"Internal source reference count underflow",{id});
    --node.owner_references; --bindings_;
    if (node.owner_references == 0 && !node.keep_alive) collect(id,retired);
}
std::vector<Id> InstancePool::remove_placement(const Id& id) {
    const auto found = placements_.find(id);
    if (found == placements_.end()) throw ModelError(ErrorCode::missing_reference,"Placement does not exist",{id});
    const auto source = found->second.source;
    InstancePool candidate = *this; std::vector<Id> retired;
    candidate.placements_.erase(id); candidate.release(source.instance,retired);
    *this = std::move(candidate);
    return retired;
}
std::vector<Id> InstancePool::bind_input(const Id& owner, const Id& input, const AssetPort& source, bool keep_alive) {
    require(input_port(owner,input).kind == definitions_.output(source).kind,ErrorCode::type_mismatch,"Content input/source kinds differ",{owner,input,source.asset,source.port});
    InstancePool candidate = *this; std::vector<Id> retired;
    const auto child = candidate.instantiate(source.asset,keep_alive);
    candidate.acquire(child);
    auto& inputs = candidate.instances_.at(owner).inputs;
    const auto old = inputs.find(input);
    if (old != inputs.end()) candidate.release(old->second.instance,retired);
    inputs.insert_or_assign(input,InstancePort{child,source.port});
    candidate.validate_live_graph();
    *this = std::move(candidate);
    return retired;
}
std::vector<Id> InstancePool::bind_shared_input(const Id& owner, const Id& input, const InstancePort& source) {
    require(input_port(owner,input).kind == output_port(source).kind,ErrorCode::type_mismatch,"Content input/source kinds differ",{owner,input,source.instance,source.port});
    InstancePool candidate = *this; std::vector<Id> retired;
    candidate.acquire(source.instance);
    auto& inputs = candidate.instances_.at(owner).inputs;
    const auto old = inputs.find(input);
    if (old != inputs.end()) candidate.release(old->second.instance,retired);
    inputs.insert_or_assign(input,source);
    candidate.validate_live_graph();
    *this = std::move(candidate);
    return retired;
}
std::vector<Id> InstancePool::unbind_input(const Id& owner, const Id& input) {
    static_cast<void>(input_port(owner,input));
    InstancePool candidate = *this; std::vector<Id> retired;
    auto& inputs = candidate.instances_.at(owner).inputs;
    const auto old = inputs.find(input);
    if (old == inputs.end()) return retired;
    const auto source = old->second.instance;
    inputs.erase(old); candidate.release(source,retired);
    *this = std::move(candidate);
    return retired;
}
std::vector<Id> InstancePool::set_keep_alive(const Id& id, bool keep_alive) {
    static_cast<void>(instance(id));
    InstancePool candidate = *this; std::vector<Id> retired;
    auto& node = candidate.instances_.at(id); node.keep_alive = keep_alive;
    if (!keep_alive && node.owner_references == 0) candidate.collect(id,retired);
    *this = std::move(candidate);
    return retired;
}
void InstancePool::set_transport(const Id& id, TransportIntent intent) {
    static_cast<void>(instance(id));
    require(intent.position.ticks >= 0 && intent.position.time_base.numerator > 0 && intent.position.time_base.denominator > 0 &&
            intent.rate.denominator > 0,ErrorCode::invalid_transport,"Transport intent needs nonnegative position and valid rational time/rate",{id});
    instances_.at(id).transport = intent;
}
void InstancePool::validate_live_graph() const {
    std::map<Id,std::vector<Id>> edges;
    for (const auto& [id,node] : instances_) {
        auto& children = edges[id];
        for (const auto& [layer,source] : node.layers) { static_cast<void>(layer); children.push_back(source.instance); }
        for (const auto& [input,source] : node.inputs) { static_cast<void>(input); children.push_back(source.instance); }
    }
    std::map<Id,unsigned> colors;
    struct Visit { Id id; std::size_t next = 0; };
    std::vector<Visit> stack;
    for (const auto& [root,unused] : edges) {
        static_cast<void>(unused);
        if (colors[root] != 0) continue;
        colors[root] = 1; stack.push_back({root,0});
        while (!stack.empty()) {
            auto& visit = stack.back(); const auto& children = edges.at(visit.id);
            if (visit.next == children.size()) { colors[visit.id] = 2; stack.pop_back(); continue; }
            const auto child = children[visit.next++];
            if (colors[child] == 1) {
                std::vector<Id> involved;
                const auto first = std::find_if(stack.begin(),stack.end(),[&](const Visit& item){return item.id == child;});
                for (auto p = first; p != stack.end(); ++p) involved.push_back(p->id);
                involved.push_back(child);
                throw ModelError(ErrorCode::dependency_cycle,"Live source input-binding cycle",std::move(involved));
            }
            if (colors[child] == 0) { colors[child] = 1; stack.push_back({child,0}); }
        }
    }
}
} // namespace mapgyver::domain
