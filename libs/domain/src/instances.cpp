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
    : definitions_(std::move(definitions)), limits_(limits), ids_(std::make_shared<IdGenerator>(std::move(ids))) {
    require(static_cast<bool>(*ids_),ErrorCode::invalid_definition,"Instance ID generator is missing");
}
InstancePool InstancePool::restore(DefinitionSnapshot definitions, std::map<Id,SourceInstance> instances,
                                   std::map<Id,Placement> placements, InstanceLimits limits, IdGenerator ids) {
    require(instances.size() <= limits.instances && placements.size() <= limits.placements,
            ErrorCode::capacity_exceeded,"Restored instance/placement capacity exceeded");
    InstancePool pool{std::move(definitions),limits,std::move(ids)};
    pool.instances_ = std::move(instances); pool.placements_ = std::move(placements);
    std::map<Id,std::uint64_t> stored_counts;
    for (auto& [id,node] : pool.instances_) {
        require(id == node.id && !pool.placements_.contains(id),ErrorCode::duplicate_id,"Restored instance identity is inconsistent or reused",{id,node.id});
        static_cast<void>(pool.definitions_.asset(node.asset));
        pool.set_transport(id,node.transport);
        stored_counts.emplace(id,node.owner_references); node.owner_references = 0;
    }
    const auto reference = [&](const InstancePort& source) {
        static_cast<void>(pool.output_port(source)); pool.acquire(source.instance);
    };
    for (const auto& [id,placement] : pool.placements_) {
        require(id == placement.id,ErrorCode::invalid_definition,"Restored placement identity differs from its key",{id,placement.id});
        reference(placement.source);
    }
    struct Scope {Id parent, token; bool shared; bool operator==(const Scope&) const = default;};
    std::map<Id,Scope> static_owners;
    for (const auto& [id,node] : pool.instances_) {
        const auto& asset = pool.definitions_.asset(node.asset);
        const auto* source = std::get_if<CompositionSource>(&asset.source);
        if (source == nullptr) {
            require(node.layers.empty() && node.inputs.empty(),ErrorCode::type_mismatch,"External sources cannot own composition bindings",{id});
            continue;
        }
        const auto& composition = pool.definitions_.composition(source->composition);
        std::size_t expected_layers = 0;
        std::map<Id,Id> shared_groups;
        for (const auto& layer : composition.layers) {
            const auto* expected = std::get_if<AssetPort>(&layer.source);
            if (expected == nullptr) continue;
            ++expected_layers;
            const auto found = node.layers.find(layer.id);
            require(found != node.layers.end(),ErrorCode::missing_reference,"Restored static layer binding is missing",{id,layer.id});
            const auto& binding = found->second;
            require(binding.port == expected->port && pool.instance(binding.instance).asset == expected->asset,
                    ErrorCode::type_mismatch,"Restored layer binding differs from its definition",{id,layer.id,binding.instance});
            const Scope scope{id,layer.shared_group.value_or(layer.id),layer.shared_group.has_value()};
            const auto [owner,inserted] = static_owners.emplace(binding.instance,scope);
            require(inserted || owner->second == scope,ErrorCode::invalid_definition,"Restored static children share state outside their declared scope",{id,layer.id,binding.instance});
            if (layer.shared_group) {
                const auto [group,first] = shared_groups.emplace(*layer.shared_group,binding.instance);
                require(first || group->second == binding.instance,ErrorCode::invalid_definition,"Restored share group uses different instances",{id,*layer.shared_group});
            }
            reference(binding);
        }
        require(expected_layers == node.layers.size(),ErrorCode::invalid_definition,"Restored source has extra static layer bindings",{id});
        for (const auto& [input,binding] : node.inputs) {
            require(pool.input_port(id,input).kind == pool.output_port(binding).kind,ErrorCode::type_mismatch,"Restored input binding has the wrong stream kind",{id,input});
            reference(binding);
        }
    }
    pool.validate_live_graph();
    for (const auto& [id,node] : pool.instances_) {
        require(node.owner_references == stored_counts.at(id),ErrorCode::invalid_definition,"Stored derived ownership count is inconsistent",{id});
        require(node.owner_references != 0 || node.keep_alive,ErrorCode::invalid_definition,"Restored source is unreachable and not preloaded",{id});
    }
    return pool;
}
const SourceInstance& InstancePool::instance(const Id& id) const {
    const auto found = instances_.find(id);
    if (found == instances_.end()) throw ModelError(ErrorCode::missing_reference,"Source instance does not exist",{id});
    return found->second;
}
Id InstancePool::fresh_id() {
    for (int attempt = 0; attempt < 8; ++attempt) {
        auto id = (*ids_)();
        if (!instances_.contains(id) && !placements_.contains(id)) return id;
    }
    throw ModelError(ErrorCode::duplicate_id,"Instance ID generation repeatedly collided");
}
void InstancePool::acquire(const Id& id) {
    require(bindings_ < limits_.ownership_bindings,ErrorCode::capacity_exceeded,"Source ownership-binding capacity exceeded",{id});
    ++instances_.at(id).owner_references; ++bindings_;
}
Id InstancePool::create_instance(const Id& asset, bool keep_alive) {
    static_cast<void>(definitions_.asset(asset));
    require(instances_.size() < limits_.instances,ErrorCode::capacity_exceeded,"Live source-instance capacity exceeded",{asset});
    auto id = fresh_id();
    instances_.emplace(id,SourceInstance{id,asset,{},keep_alive,0,{},{}});
    return id;
}
Id InstancePool::instantiate(const Id& asset, bool keep_alive) {
    const auto root = create_instance(asset,keep_alive);
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
                child = create_instance(source->asset,false);
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
std::vector<Id> InstancePool::reconcile_definitions(DefinitionSnapshot definitions) {
    InstancePool candidate = *this;
    candidate.definitions_ = std::move(definitions);
    std::map<Id,SourceInstance> rebuilt;
    std::vector<Id> pending;
    const auto enqueue = [&](const Id& id) {
        if (rebuilt.contains(id)) return;
        auto node = candidate.instance(id);
        static_cast<void>(candidate.definitions_.asset(node.asset));
        node.layers.clear(); node.inputs.clear(); node.owner_references = 0;
        rebuilt.emplace(id,std::move(node)); pending.push_back(id);
    };
    for (const auto& [id,placement] : placements_) {
        static_cast<void>(id); enqueue(placement.source.instance);
    }
    for (const auto& [id,node] : instances_) if (node.keep_alive) enqueue(id);
    while (!pending.empty()) {
        const auto parent = pending.back(); pending.pop_back();
        const auto original = instances_.find(parent);
        const auto& next_asset = candidate.definitions_.asset(rebuilt.at(parent).asset);
        const auto* next_source = std::get_if<CompositionSource>(&next_asset.source);
        if (next_source == nullptr) {
            require(original == instances_.end() || original->second.inputs.empty(),ErrorCode::type_mismatch,
                    "Unbind composition inputs before changing the source to another kind",{parent});
            continue;
        }
        const Composition* old_composition = nullptr;
        if (original != instances_.end()) {
            const auto& old_asset = definitions_.asset(original->second.asset);
            if (const auto* old_source = std::get_if<CompositionSource>(&old_asset.source))
                if (old_source->composition == next_source->composition) old_composition = &definitions_.composition(old_source->composition);
            require(old_composition != nullptr || original->second.inputs.empty(),ErrorCode::type_mismatch,
                    "Unbind inputs before replacing their composition definition scope",{parent});
        }
        const auto& next_composition = candidate.definitions_.composition(next_source->composition);
        if (original != instances_.end()) {
            for (const auto& [input,source] : original->second.inputs) {
                require(candidate.input_port(parent,input).kind == candidate.output_port(source).kind,ErrorCode::type_mismatch,
                        "A definition edit changed a bound input/source kind",{parent,input,source.instance,source.port});
                enqueue(source.instance);
                rebuilt.at(parent).inputs.emplace(input,source);
            }
        }
        std::map<Id,Id> old_groups, next_groups;
        if (old_composition != nullptr)
            for (const auto& layer : old_composition->layers)
                if (layer.shared_group && std::holds_alternative<AssetPort>(layer.source))
                    old_groups.emplace(*layer.shared_group,original->second.layers.at(layer.id).instance);
        for (const auto& layer : next_composition.layers) {
            const auto* source = std::get_if<AssetPort>(&layer.source);
            if (source == nullptr) continue;
            std::optional<Id> child;
            if (layer.shared_group) {
                const auto current = next_groups.find(*layer.shared_group);
                if (current != next_groups.end()) child = current->second;
                else {
                    const auto previous = old_groups.find(*layer.shared_group);
                    if (previous != old_groups.end() && instance(previous->second).asset == source->asset) child = previous->second;
                }
            } else if (old_composition != nullptr) {
                const auto previous = std::find_if(old_composition->layers.begin(),old_composition->layers.end(),
                    [&](const Layer& value){return value.id == layer.id;});
                if (previous != old_composition->layers.end() && !previous->shared_group)
                    if (const auto* old_source = std::get_if<AssetPort>(&previous->source))
                        if (old_source->asset == source->asset) child = original->second.layers.at(layer.id).instance;
            }
            if (!child) child = candidate.create_instance(source->asset,false);
            enqueue(*child);
            rebuilt.at(parent).layers.emplace(layer.id,InstancePort{*child,source->port});
            if (layer.shared_group) next_groups.insert_or_assign(*layer.shared_group,*child);
        }
    }
    std::vector<Id> retired;
    for (const auto& [id,node] : instances_) {
        static_cast<void>(node);
        if (!rebuilt.contains(id)) retired.push_back(id);
    }
    candidate.instances_ = std::move(rebuilt); candidate.bindings_ = 0;
    const auto reference = [&](const InstancePort& source) {
        static_cast<void>(candidate.output_port(source));
        candidate.acquire(source.instance);
    };
    for (const auto& [id,placement] : candidate.placements_) { static_cast<void>(id); reference(placement.source); }
    for (const auto& [id,node] : candidate.instances_) {
        static_cast<void>(id);
        for (const auto& [layer,source] : node.layers) { static_cast<void>(layer); reference(source); }
        for (const auto& [input,source] : node.inputs) { static_cast<void>(input); reference(source); }
    }
    candidate.validate_live_graph();
    *this = std::move(candidate);
    return retired;
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
