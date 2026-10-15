// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/domain/instances.hpp>
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace mapgyver::domain;
namespace {
Id id(const char* value) { return Id{value}; }
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void rejects(ErrorCode code, const std::function<void()>& operation) {
    try { operation(); }
    catch (const ModelError& error) { check(error.code() == code,"Instance edit failed for the wrong contract"); return; }
    throw std::runtime_error("Invalid instance edit unexpectedly succeeded");
}
InstancePool::IdGenerator generator(std::size_t& sequence) {
    return [&sequence]{return Id{"live-"+std::to_string(++sequence)};};
}
Definitions fixture() {
    Definitions definitions;
    definitions.assets.emplace(id("media"),Asset{id("media"),"Media",ExternalSource{"media.file","relative/movie.mp4",
        {{id("picture"),"Picture",StreamKind::video},{id("sound"),"Sound",StreamKind::audio}}}});
    definitions.assets.emplace(id("app"),Asset{id("app"),"Configured app",ExternalSource{"managed.app","configured_definition",
        {{id("a"),"First",StreamKind::video},{id("b"),"Second",StreamKind::video}}}});
    definitions.assets.emplace(id("root-asset"),Asset{id("root-asset"),"Root",CompositionSource{id("root")}});
    definitions.compositions.emplace(id("root"),Composition{id("root"),"Root",{{id("content"),"Content",StreamKind::video}},
        {{id("program"),"Program",StreamKind::video}},
        {{id("default"),"Default slot",StreamKind::video,InputPort{id("content")},std::nullopt},
         {id("app-a"),"App A",StreamKind::video,AssetPort{id("app"),id("a")},id("app-group")},
         {id("app-b"),"App B",StreamKind::video,AssetPort{id("app"),id("b")},id("app-group")},
         {id("media-layer"),"Independent media",StreamKind::video,AssetPort{id("media"),id("picture")},std::nullopt}}});
    definitions.assets.emplace(id("outer-asset"),Asset{id("outer-asset"),"Outer",CompositionSource{id("outer")}});
    definitions.compositions.emplace(id("outer"),Composition{id("outer"),"Outer",{},{{id("program"),"Program",StreamKind::video}},
        {{id("one"),"First root",StreamKind::video,AssetPort{id("root-asset"),id("program")},std::nullopt},
         {id("two"),"Second root",StreamKind::video,AssetPort{id("root-asset"),id("program")},std::nullopt}}});
    return definitions;
}
void independent_and_shared_transport() {
    std::size_t sequence = 0;
    InstancePool pool{DefinitionSnapshot{fixture()},{},generator(sequence)};
    const auto first = pool.place({id("media"),id("picture")});
    const auto second = pool.place({id("media"),id("picture")});
    check(first.source.instance != second.source.instance,"Default placements reused one live source");
    TransportIntent intent{{1501,{1001,30000}},{-1,2},true,true};
    pool.set_transport(first.source.instance,intent);
    check(pool.instance(second.source.instance).transport == TransportIntent{},"Independent transport changed with another placement");
    check(pool.instance(first.source.instance).transport == intent,"Rational transport intent was rounded/altered");
    const auto shared = pool.place_shared({first.source.instance,id("sound")});
    check(shared.source.instance == first.source.instance,"Explicit sharing created a second source");
    check(pool.instance(first.source.instance).owner_references == 2,"Shared source has the wrong ownership count");
    check(pool.remove_placement(first.id).empty(),"Removing one shared consumer retired its still-used source");
    check(pool.instance(shared.source.instance).transport.playing,"Remaining consumer lost its logical transport");
    const auto retired = pool.remove_placement(shared.id);
    check(retired.size() == 1 && retired.front() == first.source.instance,"Last consumer did not retire its source");
    rejects(ErrorCode::missing_reference,[&]{static_cast<void>(pool.instance(first.source.instance));});
    check(pool.instance(second.source.instance).transport == TransportIntent{},"Retiring shared source changed independent placement");
    static_cast<void>(pool.remove_placement(second.id));
    check(pool.instances().empty() && pool.ownership_bindings() == 0,"Independent source ownership leaked after removal");
}
void nested_scopes() {
    std::size_t sequence = 0;
    InstancePool pool{DefinitionSnapshot{fixture()},{},generator(sequence)};
    const auto first = pool.place({id("outer-asset"),id("program")});
    const auto second = pool.place({id("outer-asset"),id("program")});
    check(pool.instances().size() == 14,"Nested composition instances did not materialize independent children");
    const auto root_a = pool.instance(first.source.instance).layers.at(id("one")).instance;
    const auto root_b = pool.instance(first.source.instance).layers.at(id("two")).instance;
    const auto root_c = pool.instance(second.source.instance).layers.at(id("one")).instance;
    const auto app_a = pool.instance(root_a).layers.at(id("app-a")).instance;
    check(app_a == pool.instance(root_a).layers.at(id("app-b")).instance,"Explicit local multi-port sharing was ignored");
    check(app_a != pool.instance(root_b).layers.at(id("app-a")).instance &&
          app_a != pool.instance(root_c).layers.at(id("app-a")).instance,"Local share group escaped its parent instance scope");
    pool.set_transport(app_a,{{99,{1,30}},{1,1},true,false});
    check(pool.instance(pool.instance(root_b).layers.at(id("app-a")).instance).transport.position.ticks == 0,
          "Nested reusable definition accidentally shared playheads");
    const auto retained = pool.place_shared({app_a,id("b")});
    const auto retired = pool.remove_placement(first.id);
    check(retired.size() == 6 && pool.instance(app_a).owner_references == 1,"Explicit external consumer did not preserve the nested child");
    static_cast<void>(pool.remove_placement(retained.id));
    static_cast<void>(pool.remove_placement(second.id));
    check(pool.instances().empty() && pool.ownership_bindings() == 0,"Nested ownership did not retire exactly once");
}
void input_binding_cycles_and_rollback() {
    std::size_t sequence = 0;
    InstancePool pool{DefinitionSnapshot{fixture()},{},generator(sequence)};
    const auto first = pool.place({id("root-asset"),id("program")});
    const auto second = pool.place({id("root-asset"),id("program")});
    check(pool.instance(first.source.instance).inputs.empty(),"Default content slot is not unbound");
    static_cast<void>(pool.bind_input(first.source.instance,id("content"),{id("media"),id("picture")}));
    static_cast<void>(pool.bind_input(second.source.instance,id("content"),{id("media"),id("picture")}));
    const auto first_media = pool.instance(first.source.instance).inputs.at(id("content")).instance;
    const auto second_media = pool.instance(second.source.instance).inputs.at(id("content")).instance;
    check(first_media != second_media,"Independent content-slot imports shared one source");
    pool.set_transport(first_media,{{123,{1,24}},{1,1},true,false});
    const auto old_instances = pool.instances(); const auto old_bindings = pool.ownership_bindings();
    rejects(ErrorCode::type_mismatch,[&]{static_cast<void>(pool.bind_input(first.source.instance,id("content"),{id("media"),id("sound")}));});
    rejects(ErrorCode::dependency_cycle,[&]{static_cast<void>(pool.bind_shared_input(first.source.instance,id("content"),first.source));});
    check(pool.instances() == old_instances && pool.ownership_bindings() == old_bindings,"Rejected binding changed live state/refcounts/transport");
    static_cast<void>(pool.unbind_input(first.source.instance,id("content")));
    static_cast<void>(pool.unbind_input(second.source.instance,id("content")));
    static_cast<void>(pool.bind_shared_input(first.source.instance,id("content"),second.source));
    const auto before = pool.instances();
    rejects(ErrorCode::dependency_cycle,[&]{static_cast<void>(pool.bind_shared_input(second.source.instance,id("content"),first.source));});
    check(pool.instances() == before,"Indirect cycle rejection partially replaced the existing input binding");
    check(pool.remove_placement(second.id).empty(),"A composition bound by another owner retired early");
    const auto retired = pool.remove_placement(first.id);
    check(retired.size() == 6 && pool.instances().empty() && pool.ownership_bindings() == 0,"DAG ownership retirement leaked/double-released children");
}
void lifecycle_capacity_and_invalid_intent() {
    std::size_t sequence = 0;
    InstancePool pool{DefinitionSnapshot{fixture()},{},generator(sequence)};
    const auto placement = pool.place({id("media"),id("picture")},true);
    pool.set_transport(placement.source.instance,{{15,{1,30}},{1,1},true,false});
    check(pool.remove_placement(placement.id).empty(),"Keep-alive policy was ignored");
    check(pool.instance(placement.source.instance).owner_references == 0 && pool.instance(placement.source.instance).transport.playing,
          "Keep-alive source lost its independent logical transport");
    const auto before = pool.instance(placement.source.instance).transport;
    rejects(ErrorCode::invalid_transport,[&]{pool.set_transport(placement.source.instance,{{-1,{1,30}},{1,1},false,false});});
    rejects(ErrorCode::invalid_transport,[&]{pool.set_transport(placement.source.instance,{{1,{1,0}},{1,1},false,false});});
    rejects(ErrorCode::invalid_transport,[&]{pool.set_transport(placement.source.instance,{{1,{1,30}},{1,0},false,false});});
    check(pool.instance(placement.source.instance).transport == before,"Rejected transport partially changed intent");
    check(pool.set_keep_alive(placement.source.instance,false).size() == 1 && pool.instances().empty(),"Explicit policy release failed to retire idle source");
    const auto nested = pool.place({id("root-asset"),id("program")},true);
    check(pool.remove_placement(nested.id).empty() && pool.instances().size() == 3,
          "Preloaded composition lost the child owners needed by its keep-alive policy");
    check(pool.set_keep_alive(nested.source.instance,false).size() == 3 && pool.instances().empty() && pool.ownership_bindings() == 0,
          "Releasing a preloaded composition did not retire its shared children exactly once");
    InstancePool limited{DefinitionSnapshot{fixture()},{4,1,4},generator(sequence)};
    const auto stable = limited.place({id("media"),id("picture")});
    const auto instances = limited.instances();
    rejects(ErrorCode::capacity_exceeded,[&]{static_cast<void>(limited.place_shared(stable.source));});
    check(limited.instances() == instances && limited.placements().size() == 1,"Placement capacity failure changed published state");
    InstancePool expanded{DefinitionSnapshot{fixture()},{4,16,64},generator(sequence)};
    rejects(ErrorCode::capacity_exceeded,[&]{static_cast<void>(expanded.place({id("outer-asset"),id("program")}));});
    check(expanded.instances().empty() && expanded.placements().empty() && expanded.ownership_bindings() == 0,
          "Nested expansion capacity failure leaked partially-created instances");
    InstancePool binding_limit{DefinitionSnapshot{fixture()},{64,64,1},generator(sequence)};
    rejects(ErrorCode::capacity_exceeded,[&]{static_cast<void>(binding_limit.place({id("root-asset"),id("program")}));});
    check(binding_limit.instances().empty() && binding_limit.ownership_bindings() == 0,"Ownership-binding capacity failure was not atomic");
    InstancePool colliding{DefinitionSnapshot{fixture()},{},[]{return id("collision");}};
    rejects(ErrorCode::duplicate_id,[&]{static_cast<void>(colliding.place({id("media"),id("picture")}));});
    check(colliding.instances().empty() && colliding.placements().empty() && colliding.ownership_bindings() == 0,
          "ID-generation failure published a partial source or placement");
}
void deep_instantiation_and_retirement() {
    Definitions definitions;
    constexpr std::size_t depth = 2048;
    for (std::size_t index = 0; index < depth; ++index) {
        const Id asset{"asset-"+std::to_string(index)}, composition{"composition-"+std::to_string(index)};
        definitions.assets.emplace(asset,Asset{asset,"Nested",CompositionSource{composition}});
        std::vector<Layer> layers;
        if (index+1 < depth) layers.push_back({id("child"),"Child",StreamKind::video,
            AssetPort{Id{"asset-"+std::to_string(index+1)},id("program")},std::nullopt});
        definitions.compositions.emplace(composition,Composition{composition,"Nested",{},
            {{id("program"),"Program",StreamKind::video}},std::move(layers)});
    }
    std::size_t sequence = 0;
    InstancePool pool{DefinitionSnapshot{std::move(definitions)},{depth,4,depth},generator(sequence)};
    const auto root = pool.place({id("asset-0"),id("program")});
    check(pool.instances().size() == depth && pool.ownership_bindings() == depth,"Deep instance expansion was incomplete");
    const auto before = pool.instances();
    check(pool.reconcile_definitions(pool.definitions()).empty() && pool.instances() == before,
          "Deep iterative reconciliation replaced unchanged source scopes");
    check(InstancePool::restore(pool.definitions(),pool.instances(),pool.placements()).instances() == before,
          "Deep iterative graph restoration changed scoped ownership");
    const auto retired = pool.remove_placement(root.id);
    check(retired.size() == depth && pool.instances().empty() && pool.ownership_bindings() == 0,
          "Deep iterative retirement failed or leaked source ownership");
}
void definition_reconciliation() {
    std::size_t sequence = 0;
    InstancePool pool{DefinitionSnapshot{fixture()},{},generator(sequence)};
    const auto root = pool.place({id("root-asset"),id("program")});
    const auto app = pool.instance(root.source.instance).layers.at(id("app-a")).instance;
    const auto media = pool.instance(root.source.instance).layers.at(id("media-layer")).instance;
    pool.set_transport(app,{{77,{1,24}},{1,2},true,true});
    static_cast<void>(pool.bind_input(root.source.instance,id("content"),{id("media"),id("picture")}));
    auto edited = fixture();
    edited.assets.at(id("app")).name = "Renamed app";
    auto& composition = edited.compositions.at(id("root"));
    composition.outputs.front().name = "Renamed program";
    std::reverse(composition.layers.begin(),composition.layers.end());
    composition.canvases.emplace(id("program"),Canvas{3840,2160});
    const auto before = pool.instances(); const auto bindings = pool.ownership_bindings();
    check(pool.reconcile_definitions(DefinitionSnapshot{edited}).empty() && pool.instances() == before &&
          pool.ownership_bindings() == bindings,"Rename/reorder/canvas edit reset live identity, input ownership or transport");

    // A surviving local group keeps its source even when the first layer changes.
    std::erase_if(composition.layers,[](const Layer& layer){return layer.id == id("app-a");});
    composition.layers.push_back({id("app-c"),"Third port consumer",StreamKind::video,
                                 AssetPort{id("app"),id("a")},id("app-group")});
    check(pool.reconcile_definitions(DefinitionSnapshot{edited}).empty(),"Surviving group unexpectedly retired its source");
    check(pool.instance(root.source.instance).layers.at(id("app-c")).instance == app &&
          pool.instance(app).transport == before.at(app).transport,"Local group identity depended on layer order or first layer");

    // Externally owned children survive replacement of the definition's layer.
    const auto consumer = pool.place_shared({media,id("picture")});
    auto& layer = *std::find_if(composition.layers.begin(),composition.layers.end(),
                               [](const Layer& value){return value.id == id("media-layer");});
    layer.source = AssetPort{id("app"),id("a")};
    check(pool.reconcile_definitions(DefinitionSnapshot{edited}).empty(),"Explicit external owner was lost during layer replacement");
    const auto replacement = pool.instance(root.source.instance).layers.at(id("media-layer")).instance;
    check(replacement != media && replacement != app && pool.instance(media).owner_references == 1,
          "Independent replacement reused another live source or dropped an external owner");
    check(pool.remove_placement(consumer.id) == std::vector<Id>{media},"Old layer source did not retire after its last consumer");

    // A new share group is explicit new state, rather than an arbitrary old playhead.
    layer.shared_group = id("new-group");
    const auto retired = pool.reconcile_definitions(DefinitionSnapshot{edited});
    check(retired == std::vector<Id>{replacement} &&
          pool.instance(root.source.instance).layers.at(id("media-layer")).instance != replacement,
          "Changing sharing scope silently selected an existing independent playhead");
    const auto current = pool.instances(); const auto definitions = pool.definitions().data();
    auto invalid = edited;
    auto& invalid_root = invalid.compositions.at(id("root"));
    invalid_root.inputs.clear();
    std::erase_if(invalid_root.layers,[](const Layer& value){return std::holds_alternative<InputPort>(value.source);});
    rejects(ErrorCode::missing_reference,[&]{static_cast<void>(pool.reconcile_definitions(DefinitionSnapshot{invalid}));});
    check(pool.instances() == current && pool.definitions().data() == definitions,
          "Removing a bound input partially published definitions or source state");
    invalid = edited;
    invalid.compositions.at(id("root")).outputs.clear();
    invalid.compositions.at(id("root")).canvases.clear();
    // Remove the unrelated definition reference so only a live placement rejects it.
    invalid.compositions.at(id("outer")).layers.clear();
    rejects(ErrorCode::missing_reference,[&]{static_cast<void>(pool.reconcile_definitions(DefinitionSnapshot{invalid}));});
    check(pool.instances() == current && pool.definitions().data() == definitions,
          "Removing a placed output partially changed state");
}
void reconciliation_retirement_capacity_and_allocator() {
    std::size_t sequence = 0;
    auto definitions = fixture();
    InstancePool pool{DefinitionSnapshot{definitions},{},generator(sequence)};
    const auto root = pool.place({id("root-asset"),id("program")},true);
    const auto media = pool.instance(root.source.instance).layers.at(id("media-layer")).instance;
    static_cast<void>(pool.remove_placement(root.id));
    auto& layers = definitions.compositions.at(id("root")).layers;
    std::erase_if(layers,[](const Layer& layer){return layer.id == id("media-layer");});
    definitions.assets.erase(id("media"));
    check(pool.reconcile_definitions(DefinitionSnapshot{definitions}) == std::vector<Id>{media} &&
          pool.instance(root.source.instance).keep_alive && pool.instance(root.source.instance).owner_references == 0,
          "Removing an unused definition failed to retire its child or preserve a preloaded root");

    InstancePool limited{DefinitionSnapshot{fixture()},{3,4,16},generator(sequence)};
    const auto placed = limited.place({id("root-asset"),id("program")});
    auto replacement = fixture();
    replacement.compositions.at(id("root")).layers.back().source = AssetPort{id("app"),id("a")};
    const auto before = limited.instances();
    rejects(ErrorCode::capacity_exceeded,[&]{static_cast<void>(limited.reconcile_definitions(DefinitionSnapshot{replacement}));});
    check(limited.instances() == before && limited.placements().at(placed.id) == placed,
          "Replacement did not reserve peak capacity before retiring the old source");

    InstancePool allocated{DefinitionSnapshot{fixture()},{},[next = 0]() mutable {return Id{"owned-"+std::to_string(++next)};}};
    const auto snapshot = allocated;
    const auto first = allocated.place({id("media"),id("picture")});
    allocated = snapshot;
    const auto next = allocated.place({id("media"),id("picture")});
    check(first.id != next.id && first.source.instance != next.source.instance,
          "Restoring a snapshot rewound the allocator and reused issued identities");
}
void restored_graph_validation() {
    std::size_t sequence = 0;
    InstancePool pool{DefinitionSnapshot{fixture()},{},generator(sequence)};
    const auto first = pool.place({id("root-asset"),id("program")});
    const auto second = pool.place({id("root-asset"),id("program")});
    const auto media = pool.instance(first.source.instance).layers.at(id("media-layer")).instance;
    static_cast<void>(pool.bind_shared_input(first.source.instance,id("content"),{media,id("picture")}));
    const auto shared = pool.place_shared({media,id("sound")});
    pool.set_transport(media,{{9007199254740993LL,{1001,30000}},{-1,2},true,true});
    auto restored = InstancePool::restore(pool.definitions(),pool.instances(),pool.placements(),{},generator(sequence));
    check(restored.instances() == pool.instances() && restored.placements() == pool.placements() &&
          restored.ownership_bindings() == pool.ownership_bindings(),"Logical graph restore changed identity, sharing or exact intent");
    check(restored.remove_placement(shared.id).empty(),"Restored shared descendant retired before other owners released it");
    const auto reject = [&](ErrorCode code, const std::function<void(std::map<Id,SourceInstance>&)>& mutate) {
        auto nodes = pool.instances(); mutate(nodes);
        rejects(code,[&]{static_cast<void>(InstancePool::restore(pool.definitions(),nodes,pool.placements()));});
    };
    reject(ErrorCode::invalid_definition,[&](auto& nodes){++nodes.at(media).owner_references;});
    reject(ErrorCode::invalid_definition,[&](auto& nodes){
        nodes.at(second.source.instance).layers.at(id("media-layer")).instance = media;
    });
    reject(ErrorCode::missing_reference,[&](auto& nodes){nodes.at(first.source.instance).layers.erase(id("media-layer"));});
    reject(ErrorCode::type_mismatch,[&](auto& nodes){nodes.at(first.source.instance).inputs.at(id("content")).port = id("sound");});
    reject(ErrorCode::dependency_cycle,[&](auto& nodes){nodes.at(first.source.instance).inputs.at(id("content")) = first.source;});
    reject(ErrorCode::invalid_transport,[&](auto& nodes){nodes.at(media).transport.rate.denominator = 0;});
    reject(ErrorCode::invalid_definition,[&](auto& nodes){nodes.emplace(id("orphan"),SourceInstance{id("orphan"),id("media"),{},false,0,{},{}});});
    auto preloaded = pool.instances();
    preloaded.emplace(id("preloaded"),SourceInstance{id("preloaded"),id("media"),{},true,0,{},{}});
    check(InstancePool::restore(pool.definitions(),preloaded,pool.placements()).instance(id("preloaded")).keep_alive,
          "Restoration discarded explicit preloaded state");
    rejects(ErrorCode::capacity_exceeded,[&]{static_cast<void>(InstancePool::restore(pool.definitions(),pool.instances(),pool.placements(),{1,64,64}));});
    rejects(ErrorCode::capacity_exceeded,[&]{static_cast<void>(InstancePool::restore(pool.definitions(),pool.instances(),pool.placements(),{64,64,1}));});
}
}
int main() {
    try {
        independent_and_shared_transport(); nested_scopes(); input_binding_cycles_and_rollback();
        lifecycle_capacity_and_invalid_intent(); deep_instantiation_and_retirement();
        definition_reconciliation(); reconciliation_retirement_capacity_and_allocator();
        restored_graph_validation();
        std::cout << "Passed portable source-instance ownership contracts\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Source-instance contract failed: " << error.what() << '\n'; return 1;
    }
}
