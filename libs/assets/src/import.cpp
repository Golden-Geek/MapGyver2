// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/assets/import.hpp>
namespace mapgyver::assets {
ImageImport import_image(const control::ProjectState& state,std::string locator,std::string name,bool default_fit,domain::InstancePool::IdGenerator ids) {
    using namespace domain;
    if (locator.empty() || locator.size() > 65536 || !ids) throw ModelError(ErrorCode::invalid_definition,"Image import requires a bounded locator and ID allocator");
    const auto root = state.instances.placements().at(state.root_placement).source;
    auto definitions = state.instances.definitions().data();const auto& asset = definitions.assets.at(state.instances.instance(root.instance).asset);
    const auto* nested = std::get_if<CompositionSource>(&asset.source);
    if (!nested) throw ModelError(ErrorCode::type_mismatch,"Quick image import requires a composition root");
    auto& composition = definitions.compositions.at(nested->composition);std::optional<Id> input;
    for (const auto& port : composition.inputs) if (port.kind == StreamKind::video) {
        if (input) throw ModelError(ErrorCode::invalid_port,"Quick image import requires exactly one root video input");
        input = port.id;
    }
    if (!input) throw ModelError(ErrorCode::invalid_port,"Quick image import requires exactly one root video input");
    const auto id = ids();if (definitions.assets.contains(id) || definitions.compositions.contains(id)) throw ModelError(ErrorCode::duplicate_id,"Imported asset ID collides",{id});
    definitions.assets.emplace(id,Asset{id,std::move(name),ExternalSource{"mapgyver.image",std::move(locator),{{Id{"picture"},"Picture",StreamKind::video}}}});
    if (default_fit) for (auto& layer : composition.layers) {
        const auto* source = std::get_if<InputPort>(&layer.source);
        if (source && source->port == *input) {if (!layer.visual) layer.visual.emplace();layer.visual->fit = ContentFit::fit;}
    }
    return {id,{control::ReplaceDefinitions{std::move(definitions)},control::BindInput{root.instance,*input,AssetPort{id,Id{"picture"}},false}}};
}
}
