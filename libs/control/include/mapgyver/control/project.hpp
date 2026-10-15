// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/domain/stage.hpp>

namespace mapgyver::control {
using namespace mapgyver::domain;
struct ProjectState {
    InstancePool instances;
    StageSnapshot stage;
    Id root_placement;
};
struct ReplaceDefinitions { Definitions value; };
struct ReplaceStage { Stage value; };
struct PlaceSource { AssetPort source; bool keep_alive = false; };
struct ShareSource { InstancePort source; };
struct RemovePlacement { Id placement; };
struct BindInput { Id owner, input; std::variant<AssetPort,InstancePort> source; bool keep_alive = false; };
struct UnbindInput { Id owner, input; };
struct SetTransport { Id instance; TransportIntent value; };
struct SetKeepAlive { Id instance; bool value; };
struct AssignSurfaceContent { Id surface; std::variant<AssetPort,InstancePort> source; };
using Command = std::variant<ReplaceDefinitions,ReplaceStage,PlaceSource,ShareSource,RemovePlacement,
                             BindInput,UnbindInput,SetTransport,SetKeepAlive,AssignSurfaceContent>;
enum class ControlErrorCode { revision_conflict, empty_transaction, history_empty, capacity_exceeded };
class ControlError : public std::runtime_error {
public:
    ControlError(ControlErrorCode code, const char* message) : std::runtime_error(message), code_(code) {}
    [[nodiscard]] ControlErrorCode code() const noexcept { return code_; }
private:
    ControlErrorCode code_;
};
struct ProjectLimits {
    std::size_t commands = 1024, history_entries = 128;
    // Logical records and UTF-8 bytes, not allocator/RSS measurements. History
    // conservatively charges both complete snapshots per entry, even if shared.
    std::size_t snapshot_records = 1048576, snapshot_text_bytes = 16777216;
    std::size_t history_records = 4194304, history_text_bytes = 67108864;
    StageLimits stage{};
};
struct AuthoredChange {
    std::uint64_t revision;
    std::shared_ptr<const ProjectState> snapshot;
    std::vector<Id> created_sources, retired_sources, created_placements;
};
// Single control owner. Publication here changes authored intent only. Native
// preparation, render application/frame and durable save have separate outcomes.
class ProjectController {
public:
    explicit ProjectController(DefaultProject project, ProjectLimits limits = {});
    explicit ProjectController(ProjectState project, ProjectLimits limits = {});
    [[nodiscard]] AuthoredChange transact(std::uint64_t expected_revision, const std::vector<Command>& commands);
    [[nodiscard]] AuthoredChange undo(std::uint64_t expected_revision);
    [[nodiscard]] AuthoredChange redo(std::uint64_t expected_revision);
    [[nodiscard]] std::shared_ptr<const ProjectState> snapshot() const noexcept { return state_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] bool can_undo() const noexcept { return cursor_ != 0; }
    [[nodiscard]] bool can_redo() const noexcept { return cursor_ < history_.size(); }
private:
    struct Cost { std::size_t records = 0, text = 0; };
    struct Entry { std::shared_ptr<const ProjectState> before, after; Cost cost; };
    [[nodiscard]] Cost validate(const ProjectState& state) const;
    void expect(std::uint64_t revision) const;
    [[nodiscard]] AuthoredChange change_to(std::shared_ptr<const ProjectState> next) const;
    ProjectLimits limits_;
    std::shared_ptr<const ProjectState> state_;
    std::uint64_t revision_ = 0;
    std::vector<Entry> history_;
    std::size_t cursor_ = 0;
};
} // namespace mapgyver::control
