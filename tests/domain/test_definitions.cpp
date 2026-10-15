// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/domain/definitions.hpp>
#include <algorithm>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>

using namespace mapgyver::domain;
namespace {
Id id(const char* value) { return Id{value}; }
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void rejects(ErrorCode code, const std::function<void()>& operation, const std::function<void(const ModelError&)>& inspect = {}) {
    try { operation(); }
    catch (const ModelError& error) {
        check(error.code() == code, "Model rejected input for the wrong contract");
        if (inspect) inspect(error);
        return;
    }
    throw std::runtime_error("Invalid model unexpectedly passed validation");
}
Definitions fixture() {
    Definitions definitions;
    definitions.assets.emplace(id("video"), Asset{id("video"),"Video",ExternalSource{"media.file","relative/movie.mp4",
        {{id("picture"),"Picture",StreamKind::video},{id("sound"),"Sound",StreamKind::audio}}}});
    definitions.assets.emplace(id("app"), Asset{id("app"),"Multi-port app",ExternalSource{"transport.spout","configured_app",
        {{id("a"),"Alpha",StreamKind::video},{id("b"),"Beta",StreamKind::video}}}});
    definitions.compositions.emplace(id("root"), Composition{id("root"),"Root",
        {{id("content"),"Content",StreamKind::video}},{{id("program"),"Program",StreamKind::video}},
        {{id("default-slot"),"Content slot",StreamKind::video,InputPort{id("content")},std::nullopt},
         {id("picture-layer"),"Picture",StreamKind::video,AssetPort{id("video"),id("picture")},std::nullopt}}});
    definitions.assets.emplace(id("root-asset"), Asset{id("root-asset"),"Reusable root",CompositionSource{id("root")}});
    return definitions;
}
void identity_and_immutability() {
    rejects(ErrorCode::invalid_id,[]{ static_cast<void>(Id{"../not-an-id"}); });
    rejects(ErrorCode::invalid_id,[]{ static_cast<void>(Id{"with space"}); });
    rejects(ErrorCode::invalid_id,[]{ static_cast<void>(Id{""}); });
    rejects(ErrorCode::invalid_id,[]{ static_cast<void>(Id{std::string(65,'a')}); });
    std::set<Id> generated;
    for (int index = 0; index < 128; ++index) {
        const auto value = Id::random();
        check(value.str().size() == 36 && value.str()[14] == '4' &&
              std::string_view{"89ab"}.find(value.str()[19]) != std::string_view::npos, "Generated ID is not UUIDv4");
        check(generated.insert(value).second, "ID generation collided");
    }
    auto definitions = fixture();
    const DefinitionSnapshot initial{definitions};
    definitions.assets.at(id("video")).name = "Renamed file";
    definitions.compositions.at(id("root")).name = "Room — façade";
    std::reverse(definitions.compositions.at(id("root")).layers.begin(),definitions.compositions.at(id("root")).layers.end());
    const DefinitionSnapshot edited{definitions};
    check(initial.asset(id("video")).name == "Video", "Published definition snapshot mutated through authoring data");
    check(edited.asset(id("video")).id == initial.asset(id("video")).id, "Rename changed asset identity");
    check(edited.composition(id("root")).layers.back().id == id("default-slot"), "Reorder replaced stable layer identity");
    check(edited.output({id("root-asset"),id("program")}).kind == StreamKind::video, "Composition asset lost typed output");
    rejects(ErrorCode::missing_reference,[&]{static_cast<void>(edited.asset(id("missing")));});
    rejects(ErrorCode::missing_reference,[&]{static_cast<void>(edited.output({id("video"),id("program")}));});
}
void typed_ports_and_explicit_sharing() {
    auto definitions = fixture();
    auto& composition = definitions.compositions.at(id("root"));
    composition.layers.push_back({id("app-a"),"First app port",StreamKind::video,AssetPort{id("app"),id("a")},id("shared-app")});
    composition.layers.push_back({id("app-b"),"Second app port",StreamKind::video,AssetPort{id("app"),id("b")},id("shared-app")});
    // More than two named outputs use the same collection model.
    for (int index = 0; index < 17; ++index)
        composition.outputs.push_back({Id{"out-"+std::to_string(index)},"Output "+std::to_string(index),StreamKind::video});
    const DefinitionSnapshot valid{definitions};
    check(valid.outputs(id("root-asset")).size() == 18, "Named output collection has a special two-output limit");
    check(!valid.composition(id("root")).layers[1].shared_group.has_value(), "Independent source default became implicit sharing");
    auto invalid = definitions;
    invalid.compositions.at(id("root")).layers.back().source = AssetPort{id("video"),id("picture")};
    rejects(ErrorCode::invalid_definition,[&]{static_cast<void>(DefinitionSnapshot{invalid});});
    invalid = definitions;
    invalid.compositions.at(id("root")).layers[1].source = AssetPort{id("video"),id("sound")};
    rejects(ErrorCode::type_mismatch,[&]{static_cast<void>(DefinitionSnapshot{invalid});});
    invalid = definitions;
    invalid.compositions.at(id("root")).layers.front().source = InputPort{id("absent")};
    rejects(ErrorCode::missing_reference,[&]{static_cast<void>(DefinitionSnapshot{invalid});});
    invalid = definitions;
    invalid.compositions.at(id("root")).layers.front().shared_group = id("redundant-input-group");
    rejects(ErrorCode::invalid_definition,[&]{static_cast<void>(DefinitionSnapshot{invalid});});
}
void invalid_graphs() {
    auto definitions = fixture();
    definitions.compositions.at(id("root")).layers.push_back(
        {id("self"),"Recursive",StreamKind::video,AssetPort{id("root-asset"),id("program")},std::nullopt});
    rejects(ErrorCode::dependency_cycle,[&]{static_cast<void>(DefinitionSnapshot{definitions});},[](const ModelError& error){
        check(error.involved().size() == 2 && error.involved().front() == error.involved().back(), "Cycle did not report involved stable identities");
    });
    definitions = fixture();
    definitions.assets.emplace(id("nested-asset"),Asset{id("nested-asset"),"Nested",CompositionSource{id("nested")}});
    definitions.compositions.emplace(id("nested"),Composition{id("nested"),"Nested",{},{{id("out"),"Output",StreamKind::video}},
        {{id("inside"),"Root inside",StreamKind::video,AssetPort{id("root-asset"),id("program")},std::nullopt}}});
    definitions.compositions.at(id("root")).layers.push_back(
        {id("outer"),"Nested inside",StreamKind::video,AssetPort{id("nested-asset"),id("out")},std::nullopt});
    rejects(ErrorCode::dependency_cycle,[&]{static_cast<void>(DefinitionSnapshot{definitions});},[](const ModelError& error){
        check(error.involved().size() == 3 && error.involved().front() == error.involved().back(), "Indirect cycle path was incomplete");
    });
    definitions = fixture();
    definitions.assets.at(id("video")).id = id("changed-key");
    rejects(ErrorCode::invalid_definition,[&]{static_cast<void>(DefinitionSnapshot{definitions});});
    definitions = fixture();
    definitions.compositions.at(id("root")).outputs.push_back({id("content"),"Collision",StreamKind::video});
    rejects(ErrorCode::duplicate_id,[&]{static_cast<void>(DefinitionSnapshot{definitions});});
    definitions = fixture();
    auto& layers = definitions.compositions.at(id("root")).layers;
    layers.push_back(layers.front());
    rejects(ErrorCode::duplicate_id,[&]{static_cast<void>(DefinitionSnapshot{definitions});});
    definitions = fixture();
    std::get<ExternalSource>(definitions.assets.at(id("app")).source).outputs.back().name = "Alpha";
    rejects(ErrorCode::invalid_port,[&]{static_cast<void>(DefinitionSnapshot{definitions});});
}
void unsupported_and_names() {
    auto definitions = fixture();
    auto& source = std::get<ExternalSource>(definitions.assets.at(id("app")).source);
    source.adapter = "future.platform.receiver";
    source.locator = "configured endpoint remains unchanged";
    const DefinitionSnapshot snapshot{definitions};
    check(snapshot.data() == definitions, "Unknown configured adapter disappeared or changed");
    definitions.assets.at(id("video")).name = std::string{"invalid\xc0\x80",9};
    rejects(ErrorCode::invalid_name,[&]{static_cast<void>(DefinitionSnapshot{definitions});});
    definitions = fixture();
    definitions.assets.at(id("video")).name = "line\nbreak";
    rejects(ErrorCode::invalid_name,[&]{static_cast<void>(DefinitionSnapshot{definitions});});
}
void deep_graph() {
    Definitions definitions;
    constexpr int depth = 2048;
    for (int index = 0; index < depth; ++index) {
        const Id asset{"asset-"+std::to_string(index)}, composition{"composition-"+std::to_string(index)};
        definitions.assets.emplace(asset,Asset{asset,"Nested source",CompositionSource{composition}});
        std::vector<Layer> layers;
        if (index+1 < depth)
            layers.push_back({id("child"),"Nested child",StreamKind::video,
                AssetPort{Id{"asset-"+std::to_string(index+1)},id("out")},std::nullopt});
        definitions.compositions.emplace(composition,Composition{composition,"Nested composition",{},
            {{id("out"),"Output",StreamKind::video}},std::move(layers)});
    }
    const DefinitionSnapshot valid{definitions};
    check(valid.data().assets.size() == depth, "Deep acyclic graph lost authored nodes");
    definitions.compositions.at(Id{"composition-"+std::to_string(depth-1)}).layers.push_back(
        {id("cycle"),"Cycle",StreamKind::video,AssetPort{id("asset-0"),id("out")},std::nullopt});
    rejects(ErrorCode::dependency_cycle,[&]{static_cast<void>(DefinitionSnapshot{definitions});},[](const ModelError& error){
        check(error.involved().size() == depth+1, "Deep cycle path was truncated or validation recursed incorrectly");
    });
}
}
int main() {
    try {
        identity_and_immutability(); typed_ports_and_explicit_sharing(); invalid_graphs(); unsupported_and_names(); deep_graph();
        std::cout << "Passed portable domain definition contracts\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Domain definition contract failed: " << error.what() << '\n'; return 1;
    }
}
