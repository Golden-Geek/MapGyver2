// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/assets/import.hpp>
#include <mapgyver/control/wire.hpp>
#include <iostream>
using namespace mapgyver;
using namespace domain;
namespace {
void check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
void run() {
    control::ProjectController controller{make_default_project()};const auto initial = controller.snapshot();
    auto imported = assets::import_image(*initial,"images/missing.png","Reusable image",true,[]{return Id{"image"};});
    const auto change = control::ParameterRegistry{controller}.execute(controller,0,imported.commands);
    check(change.snapshot->stage.data() == initial->stage.data(),"Import rewrote routing or physical calibration");
    const auto& source = std::get<ExternalSource>(change.snapshot->instances.definitions().asset(imported.asset).source);
    check(source.adapter == "mapgyver.image" && source.locator == "images/missing.png","Import probed/deleted an offline reference or lost its relative path");
    const auto root = change.snapshot->instances.placements().at(change.snapshot->root_placement).source;
    const auto& root_asset = change.snapshot->instances.definitions().asset(change.snapshot->instances.instance(root.instance).asset);
    const auto& composition = change.snapshot->instances.definitions().composition(std::get<CompositionSource>(root_asset.source).composition);
    check(composition.layers[0].visual->fit == ContentFit::fit,"Default import requires manual source-fit setup");
    const auto placed = controller.transact(1,{control::PlaceSource{{imported.asset,Id{"picture"}}},control::PlaceSource{{imported.asset,Id{"picture"}}}});
    unsigned instances = 0;for (const auto& [id,item] : placed.snapshot->instances.instances()) {static_cast<void>(id);if (item.asset == imported.asset) ++instances;}
    check(instances == 3,"Reusable image definition silently shared independent source state");
    const auto encoded = control::wire::encode_project(*placed.snapshot);
    check(control::wire::encode_project(control::wire::decode_project(encoded)) == encoded,"Offline image definitions did not survive persistence");
    auto legacy = encoded;auto& visual = legacy["definitions"]["compositions"][composition.id.str()]["layers"][0]["visual"];visual.erase("fit");
    const auto restored = control::wire::encode_project(control::wire::decode_project(legacy));
    check(restored["definitions"]["compositions"][composition.id.str()]["layers"][0]["visual"]["fit"] == "stretch","Earlier layer visual documents lost the stretch default");
    static_cast<void>(controller.undo(2));static_cast<void>(controller.undo(3));
    check(controller.snapshot() == initial,"Image import bypassed shared undo");
    const auto existing = initial->instances.instance(root.instance).asset;
    bool rejected = false;try {static_cast<void>(assets::import_image(*initial,"other.png","Duplicate",false,[existing]{return existing;}));} catch (const ModelError&) {rejected = true;}
    check(rejected && controller.snapshot() == initial && controller.revision() == 4,"Duplicate asset identity changed the project");
    auto definitions = initial->instances.definitions().data();definitions.compositions.begin()->second.inputs.push_back({Id{"second-input"},"Second",StreamKind::video});
    static_cast<void>(controller.transact(4,{control::ReplaceDefinitions{definitions}}));const auto stable = controller.snapshot();
    rejected = false;try {static_cast<void>(assets::import_image(*stable,"x.png","Image"));} catch (const ModelError&) {rejected = true;}
    check(rejected && controller.snapshot() == stable,"Ambiguous image import silently selected an input");
}
}
int main() {try {run();std::cout << "Validated image import, reusable definitions, offline references and undo passed\n";return 0;}
    catch(const std::exception& error){std::cerr << error.what() << '\n';return 1;}}
