// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/control/project.hpp>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace mapgyver::control;
namespace {
Id id(const char* value) {return Id{value};}
void check(bool valid, const char* message) {if (!valid) throw std::runtime_error(message);}
void model_rejects(ErrorCode code, const std::function<void()>& operation) {
    try {operation();} catch (const ModelError& error) {check(error.code() == code,"Wrong model rejection"); return;}
    throw std::runtime_error("Invalid model transaction succeeded");
}
void control_rejects(ControlErrorCode code, const std::function<void()>& operation) {
    try {operation();} catch (const ControlError& error) {check(error.code() == code,"Wrong control rejection"); return;}
    throw std::runtime_error("Invalid control operation succeeded");
}
DefaultProject fixture() {
    Definitions library;
    library.assets.emplace(id("media"),Asset{id("media"),"Movie",ExternalSource{"media.file","relative/movie.mp4",
        {{id("picture"),"Picture",StreamKind::video},{id("audio"),"Audio",StreamKind::audio}}}});
    return make_default_project(std::move(library),[next = 0]() mutable {return Id{"project-"+std::to_string(++next)};});
}
void atomic_state_and_undo() {
    auto project = fixture(); const auto root = project.root_instance, input = project.content_input, surface = project.surface;
    ProjectController controller{std::move(project)};
    const auto original = controller.snapshot(); const auto calibration = original->stage.data().patches;
    auto stage = original->stage.data(); stage.surfaces.at(surface).name = "Renamed surface";
    auto definitions = original->instances.definitions().data(); definitions.assets.at(id("media")).name = "Renamed movie";
    const auto first = controller.transact(0,{ReplaceDefinitions{definitions},ReplaceStage{stage},
                                            BindInput{root,input,AssetPort{id("media"),id("picture")}}});
    check(first.revision == 1 && first.created_sources.size() == 1 && first.retired_sources.empty(),"Wrong authored change/source delta");
    const auto media = first.created_sources.front();
    check(first.snapshot->instances.instance(root).inputs.at(input).instance == media &&
          first.snapshot->stage.data().patches == calibration,"Import/rename changed physical calibration");
    check(original->instances.instances().size() == 1 && original->stage.data().surfaces.at(surface).name == "Surface 1",
          "Published immutable snapshot changed after a later transaction");
    const auto second = controller.transact(1,{SetTransport{media,{{1501,{1001,30000}},{-1,2},true,true}}});
    check(second.snapshot->instances.instance(media).transport.position.ticks == 1501,"Transport intent was altered");
    const auto undo_transport = controller.undo(2);
    check(undo_transport.revision == 3 && undo_transport.snapshot == first.snapshot && controller.can_redo(),"Undo failed to restore exact state/identity");
    const auto undo_import = controller.undo(3);
    check(undo_import.snapshot == original && undo_import.retired_sources == std::vector<Id>{media},"Undo did not report logical retirement");
    const auto redo_import = controller.redo(4);
    check(redo_import.snapshot == first.snapshot && redo_import.created_sources == std::vector<Id>{media},"Redo regenerated source IDs");
    const auto redo_transport = controller.redo(5);
    check(redo_transport.snapshot == second.snapshot && controller.revision() == 6,"Redo failed to restore transport intent");
    control_rejects(ControlErrorCode::revision_conflict,[&]{static_cast<void>(controller.undo(5));});
    check(controller.snapshot() == second.snapshot && controller.revision() == 6,"Stale undo changed the model");
}
void whole_candidate_validation_and_rollback() {
    auto project = fixture(); const auto root_placement = project.root_placement, surface = project.surface;
    ProjectController controller{std::move(project)};
    const auto original = controller.snapshot();
    auto stage = original->stage.data(); stage.outputs.begin()->second.name = "Should roll back";
    stage.patches.begin()->second.surface = id("missing-surface");
    model_rejects(ErrorCode::missing_reference,[&]{static_cast<void>(controller.transact(0,
        {PlaceSource{{id("media"),id("picture")}},ReplaceStage{stage}}));});
    check(controller.snapshot() == original && controller.revision() == 0 && !controller.can_undo(),
          "Final stage failure leaked an earlier source creation, state or journal entry");
    control_rejects(ControlErrorCode::empty_transaction,[&]{static_cast<void>(controller.transact(0,{}));});
    model_rejects(ErrorCode::missing_reference,[&]{static_cast<void>(controller.transact(0,{RemovePlacement{root_placement}}));});
    const auto surface_placement = *original->stage.data().surfaces.at(surface).content_placement;
    model_rejects(ErrorCode::missing_reference,[&]{static_cast<void>(controller.transact(0,{RemovePlacement{surface_placement}}));});
    // The stage reference is temporarily dangling inside the candidate, but valid
    // when the complete structural batch is published.
    const auto removed = controller.transact(0,{RemovePlacement{surface_placement},ReplaceStage{Stage{}}});
    check(removed.snapshot->stage.data().surfaces.empty() && removed.snapshot->instances.instances().size() == 1 &&
          removed.snapshot->instances.placements().contains(root_placement),"Combined structural deletion did not preserve the independent show owner");
    check(controller.undo(1).snapshot == original,"Structural undo did not restore dependent patch/surface/placement IDs");
    auto reused = original->stage.data(); reused.surfaces.at(surface).content_placement = root_placement;
    model_rejects(ErrorCode::invalid_definition,[&]{static_cast<void>(controller.transact(2,{ReplaceStage{reused}}));});
    check(controller.snapshot() == original && controller.can_redo(),"Failed branch discarded redo or changed the root lease");
}
void independent_assignment_and_branching() {
    auto project = fixture(); const auto surface = project.surface;
    const auto root = project.instances.instance(project.root_instance).asset;
    const auto port = project.instances.placements().at(project.root_placement).source.port;
    ProjectController controller{std::move(project)};
    const auto original = controller.snapshot();
    const auto assigned = controller.transact(0,{AssignSurfaceContent{surface,AssetPort{root,port}}});
    check(assigned.created_sources.size() == 1 && assigned.created_placements.size() == 1 && assigned.retired_sources.empty(),
          "Independent surface assignment did not keep the show root owner");
    const auto source = assigned.created_sources.front(), placement = assigned.created_placements.front();
    check(assigned.snapshot->stage.data().surfaces.at(surface).content_placement == placement &&
          assigned.snapshot->stage.data().patches == original->stage.data().patches,"Surface assignment did not preserve patch calibration");
    model_rejects(ErrorCode::type_mismatch,[&]{static_cast<void>(controller.transact(1,
        {AssignSurfaceContent{surface,AssetPort{id("media"),id("picture")}}}));});
    check(controller.snapshot() == assigned.snapshot && controller.revision() == 1,"Rejected direct-media assignment changed live model");
    static_cast<void>(controller.undo(1));
    const auto branch = controller.transact(2,{AssignSurfaceContent{surface,AssetPort{root,port}}});
    check(branch.created_sources.front() != source && branch.created_placements.front() != placement && !controller.can_redo(),
          "Undo branch reused issued IDs or retained obsolete redo history");
}
void capacity_and_unknown_configuration() {
    auto project = fixture(); const auto output = project.output;
    ProjectLimits limits; limits.history_entries = 2; limits.commands = 2;
    ProjectController controller{std::move(project),limits};
    for (std::uint64_t revision = 0; revision < 3; ++revision) {
        auto stage = controller.snapshot()->stage.data();
        stage.outputs.at(output).adapter = "future.unavailable";
        stage.outputs.at(output).endpoint = "persistent-binding-"+std::to_string(revision);
        static_cast<void>(controller.transact(revision,{ReplaceStage{stage}}));
    }
    check(controller.undo(3).snapshot->stage.data().outputs.at(output).endpoint == "persistent-binding-1",
          "Undo changed unknown adapter configuration");
    check(controller.undo(4).snapshot->stage.data().outputs.at(output).endpoint == "persistent-binding-0" && !controller.can_undo(),
          "History entry capacity did not evict the oldest whole transaction");
    control_rejects(ControlErrorCode::history_empty,[&]{static_cast<void>(controller.undo(5));});
    const auto stable = controller.snapshot();
    control_rejects(ControlErrorCode::capacity_exceeded,[&]{static_cast<void>(controller.transact(5,
        {ReplaceStage{stable->stage.data()},ReplaceStage{stable->stage.data()},ReplaceStage{stable->stage.data()}}));});
    check(controller.snapshot() == stable && controller.can_redo(),"Command-capacity rejection changed redo/history");
    limits.history_text_bytes = 1;
    ProjectController tiny_history{fixture(),limits}; const auto before = tiny_history.snapshot();
    control_rejects(ControlErrorCode::capacity_exceeded,[&]{static_cast<void>(tiny_history.transact(0,{ReplaceStage{before->stage.data()}}));});
    check(tiny_history.snapshot() == before && !tiny_history.can_undo(),"History byte-capacity failure published state without undo");
    limits.history_text_bytes = 67108864; limits.snapshot_text_bytes = 10000;
    ProjectController bounded{fixture(),limits}; auto definitions = bounded.snapshot()->instances.definitions().data();
    std::get<ExternalSource>(definitions.assets.at(id("media")).source).locator = std::string(10001,'x');
    control_rejects(ControlErrorCode::capacity_exceeded,[&]{static_cast<void>(bounded.transact(0,{ReplaceDefinitions{definitions}}));});
    check(bounded.revision() == 0 && !bounded.can_undo(),"Project text capacity was not enforced atomically");
}
}
int main() {
    try {
        atomic_state_and_undo(); whole_candidate_validation_and_rollback();
        independent_assignment_and_branching(); capacity_and_unknown_configuration();
        std::cout << "Passed atomic authored project transaction contracts\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Project transaction contract failed: " << error.what() << '\n'; return 1;
    }
}
