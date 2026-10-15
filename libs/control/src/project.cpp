// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/control/project.hpp>
#include <algorithm>
#include <limits>
#include <type_traits>

namespace mapgyver::control {
namespace {
void require(bool valid, ControlErrorCode code, const char* message) {
    if (!valid) throw ControlError(code,message);
}
void add(std::size_t& total, std::size_t value, std::size_t limit) {
    require(total <= limit && value <= limit-total,ControlErrorCode::capacity_exceeded,"Project/history capacity exceeded");
    total += value;
}
template<class Map> const auto& find(const Map& values, const Id& id) {
    const auto found = values.find(id);
    if (found == values.end()) throw ModelError(ErrorCode::missing_reference,"Command target is missing",{id});
    return found->second;
}
}
ProjectController::ProjectController(DefaultProject project, ProjectLimits limits) : limits_(limits) {
    require(limits_.history_entries > 0,ControlErrorCode::capacity_exceeded,"Undo history requires at least one entry");
    auto state = std::make_shared<const ProjectState>(ProjectState{
        std::move(project.instances),std::move(project.stage),std::move(project.root_placement)});
    static_cast<void>(validate(*state)); state_ = std::move(state);
}
ProjectController::Cost ProjectController::validate(const ProjectState& state) const {
    const auto& root = find(state.instances.placements(),state.root_placement);
    const auto& asset = state.instances.definitions().asset(state.instances.instance(root.source.instance).asset);
    if (!std::holds_alternative<CompositionSource>(asset.source) ||
        state.instances.definitions().output({asset.id,root.source.port}).kind != StreamKind::video)
        throw ModelError(ErrorCode::type_mismatch,"Show root must own a video composition output",{root.id});
    for (const auto& [id,surface] : state.stage.data().surfaces)
        if (surface.content_placement == state.root_placement)
            throw ModelError(ErrorCode::invalid_definition,"Show root and surface must own distinct placement leases",{id,root.id});
    static_cast<void>(StageSnapshot{state.stage.data(),state.instances,limits_.stage});
    Cost cost;
    const auto records = [&](std::size_t count = 1){add(cost.records,count,limits_.snapshot_records);};
    const auto text = [&](const std::string& value){add(cost.text,value.size(),limits_.snapshot_text_bytes);};
    const auto identity = [&](const Id& id){text(id.str());};
    const auto named = [&](const auto& value){records(); identity(value.id); text(value.name);};
    const auto ports = [&](const auto& values){for (const auto& value : values) named(value);};
    for (const auto& [id,asset_value] : state.instances.definitions().data().assets) {
        static_cast<void>(id); named(asset_value);
        if (const auto* external = std::get_if<ExternalSource>(&asset_value.source)) {
            text(external->adapter); text(external->locator); ports(external->outputs);
        } else identity(std::get<CompositionSource>(asset_value.source).composition);
    }
    for (const auto& [id,composition] : state.instances.definitions().data().compositions) {
        static_cast<void>(id); named(composition); ports(composition.inputs); ports(composition.outputs);
        for (const auto& layer : composition.layers) {
            named(layer); if (layer.shared_group) identity(*layer.shared_group);
            if (const auto* source = std::get_if<AssetPort>(&layer.source)) {identity(source->asset); identity(source->port);}
            else identity(std::get<InputPort>(layer.source).port);
        }
        for (const auto& [port,canvas] : composition.canvases) {
            records(); identity(port); text(canvas.pixel_format); text(canvas.colour_space); text(canvas.alpha);
        }
    }
    const auto instance_port = [&](const InstancePort& port){identity(port.instance); identity(port.port);};
    for (const auto& [id,node] : state.instances.instances()) {
        records(); identity(id); identity(node.asset);
        for (const auto& [layer,source] : node.layers) {records(); identity(layer); instance_port(source);}
        for (const auto& [input,source] : node.inputs) {records(); identity(input); instance_port(source);}
    }
    for (const auto& [id,placement] : state.instances.placements()) {records(); identity(id); instance_port(placement.source);}
    const auto mesh = [&](const auto& value) {
        for (const auto& point : value.vertices) {records(); identity(point.id);}
        for (const auto& triangle : value.triangles) {records(); for (const auto& id : triangle.vertices) identity(id);}
    };
    for (const auto& [id,surface] : state.stage.data().surfaces) {
        static_cast<void>(id); named(surface); mesh(surface.geometry); if (surface.content_placement) identity(*surface.content_placement);
    }
    for (const auto& [id,group] : state.stage.data().groups) {
        static_cast<void>(id); named(group);
        for (const auto& surface : group.surfaces) {records(); identity(surface);}
        for (const auto& seam : group.seams) {
            records();
            for (const auto& edge : {seam.first,seam.second}) {identity(edge.surface); identity(edge.first); identity(edge.second);}
        }
    }
    for (const auto& [id,layout] : state.stage.data().layouts) {
        static_cast<void>(id); named(layout); identity(layout.group);
        for (const auto& chart : layout.charts) {records(); identity(chart.surface); mesh(chart);}
    }
    for (const auto& [id,output] : state.stage.data().outputs) {
        static_cast<void>(id); named(output); text(output.adapter); text(output.endpoint);
    }
    for (const auto& [id,patch] : state.stage.data().patches) {
        static_cast<void>(id); named(patch); identity(patch.surface); identity(patch.output); mesh(patch.calibration);
        for (const auto& point : patch.calibration.boundary) {records(); identity(point);}
    }
    identity(state.root_placement);
    return cost;
}
void ProjectController::expect(std::uint64_t revision) const {
    require(revision == revision_,ControlErrorCode::revision_conflict,"Authored revision is stale");
    require(revision_ < std::numeric_limits<std::uint64_t>::max(),ControlErrorCode::capacity_exceeded,"Authored revision exhausted");
}
AuthoredChange ProjectController::change_to(std::shared_ptr<const ProjectState> next) const {
    AuthoredChange change{revision_+1,std::move(next),{},{},{}};
    for (const auto& [id,node] : state_->instances.instances()) {
        static_cast<void>(node); if (!change.snapshot->instances.instances().contains(id)) change.retired_sources.push_back(id);
    }
    for (const auto& [id,node] : change.snapshot->instances.instances()) {
        static_cast<void>(node); if (!state_->instances.instances().contains(id)) change.created_sources.push_back(id);
    }
    for (const auto& [id,placement] : change.snapshot->instances.placements()) {
        static_cast<void>(placement); if (!state_->instances.placements().contains(id)) change.created_placements.push_back(id);
    }
    return change;
}
AuthoredChange ProjectController::transact(std::uint64_t revision, const std::vector<Command>& commands) {
    expect(revision);
    require(!commands.empty(),ControlErrorCode::empty_transaction,"Transaction is empty");
    require(commands.size() <= limits_.commands,ControlErrorCode::capacity_exceeded,"Transaction command capacity exceeded");
    auto pool = state_->instances;
    auto stage = state_->stage.data();
    for (const auto& command : commands) std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T,ReplaceDefinitions>) static_cast<void>(pool.reconcile_definitions(DefinitionSnapshot{value.value}));
        else if constexpr (std::is_same_v<T,ReplaceStage>) stage = value.value;
        else if constexpr (std::is_same_v<T,PlaceSource>) static_cast<void>(pool.place(value.source,value.keep_alive));
        else if constexpr (std::is_same_v<T,ShareSource>) static_cast<void>(pool.place_shared(value.source));
        else if constexpr (std::is_same_v<T,RemovePlacement>) static_cast<void>(pool.remove_placement(value.placement));
        else if constexpr (std::is_same_v<T,BindInput>) {
            if (const auto* source = std::get_if<AssetPort>(&value.source)) static_cast<void>(pool.bind_input(value.owner,value.input,*source,value.keep_alive));
            else {
                if (value.keep_alive) throw ModelError(ErrorCode::invalid_definition,"Shared binding cannot implicitly change another owner's keep-alive policy",{value.owner});
                static_cast<void>(pool.bind_shared_input(value.owner,value.input,std::get<InstancePort>(value.source)));
            }
        } else if constexpr (std::is_same_v<T,UnbindInput>) static_cast<void>(pool.unbind_input(value.owner,value.input));
        else if constexpr (std::is_same_v<T,SetTransport>) pool.set_transport(value.instance,value.value);
        else if constexpr (std::is_same_v<T,SetKeepAlive>) static_cast<void>(pool.set_keep_alive(value.instance,value.value));
        else if constexpr (std::is_same_v<T,AssignSurfaceContent>) {
            const auto old = find(stage.surfaces,value.surface).content_placement;
            Placement placement = std::visit([&](const auto& source) {
                if constexpr (std::is_same_v<std::decay_t<decltype(source)>,AssetPort>) return pool.place(source);
                else return pool.place_shared(source);
            },value.source);
            stage.surfaces.at(value.surface).content_placement = placement.id;
            if (old) static_cast<void>(pool.remove_placement(*old));
        }
    },command);
    auto next = std::make_shared<const ProjectState>(ProjectState{pool,StageSnapshot{std::move(stage),pool,limits_.stage},state_->root_placement});
    const auto before = validate(*state_), after = validate(*next);
    Cost entry_cost = before;
    add(entry_cost.records,after.records,limits_.history_records);
    add(entry_cost.text,after.text,limits_.history_text_bytes);
    auto history = history_;
    history.erase(history.begin()+static_cast<std::ptrdiff_t>(cursor_),history.end());
    history.push_back({state_,next,entry_cost});
    Cost retained;
    // Evict oldest complete entries; never truncate an individual transaction.
    while (history.size() > limits_.history_entries) history.erase(history.begin());
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->cost.records > limits_.history_records-retained.records || it->cost.text > limits_.history_text_bytes-retained.text) {
            history.erase(history.begin(),it.base()); break;
        }
        retained.records += it->cost.records; retained.text += it->cost.text;
    }
    auto change = change_to(next); // Finish all throwing work before publication.
    history_.swap(history); cursor_ = history_.size(); state_ = std::move(next); ++revision_;
    return change;
}
AuthoredChange ProjectController::undo(std::uint64_t revision) {
    expect(revision); require(can_undo(),ControlErrorCode::history_empty,"No transaction to undo");
    auto change = change_to(history_[cursor_-1].before);
    state_ = change.snapshot; --cursor_; ++revision_; return change;
}
AuthoredChange ProjectController::redo(std::uint64_t revision) {
    expect(revision); require(can_redo(),ControlErrorCode::history_empty,"No transaction to redo");
    auto change = change_to(history_[cursor_].after);
    state_ = change.snapshot; ++cursor_; ++revision_; return change;
}
} // namespace mapgyver::control
