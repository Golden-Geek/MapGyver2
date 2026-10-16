// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/control/project.hpp>
#include <functional>

namespace mapgyver::control {
using LayerSource = std::variant<AssetPort,InputPort>;
using ParameterValue = std::variant<bool,std::uint64_t,double,std::string,Id,std::optional<Id>,Rational,
    MediaTime,Vec2,StreamKind,LayoutMode,WarpMode,LayerSource,InstancePort,
    std::vector<Id>,std::vector<Triangle>,std::vector<Seam>,ContentFit,BlendMode,MaskShape>;
enum class ParameterType { boolean, unsigned_integer, real, text, reference, optional_reference, rational,
    media_time, point2, stream_kind, layout_mode, warp_mode, layer_source, instance_port, id_list, triangles, seams,
    content_fit, blend_mode, mask_shape };
enum class Access { read_only, read_write, trigger };
enum class Persistence { authored, resume_checkpoint, transient };
struct NumericRange { double minimum, maximum; };
struct ParameterDescriptor {
    Id parameter, owner;
    std::string address;
    ParameterType type;
    ParameterValue value;
    // No universal default exists for a reference/topology that needs context.
    std::optional<ParameterValue> default_value;
    std::optional<NumericRange> range;
    std::string unit, description;
    Access access;
    Persistence persistence;
    // Availability of this authored-model control, never a native adapter or
    // hardware support claim. Runtime capabilities are separate read-only data.
    bool available = true;
    std::string availability_reason{};
};
struct ActionDescriptor {
    Id id;
    std::string address, payload_type, description;
    Access access = Access::trigger;
};
struct ParameterWrite { std::string address; ParameterValue value; };
struct RegistryLimits { std::size_t parameters = 262144, writes = 1024; };
enum class RegistryErrorCode { missing_address, read_only, type_mismatch, out_of_range, stale_snapshot, capacity_exceeded };
class RegistryError : public std::runtime_error {
public:
    RegistryError(RegistryErrorCode code, const char* message) : std::runtime_error(message), code_(code) {}
    [[nodiscard]] RegistryErrorCode code() const noexcept { return code_; }
private:
    RegistryErrorCode code_;
};
// A frozen inventory of one authored revision. All descriptors and writes are
// registered together; protocol/UI exports consume this same inventory.
class ParameterRegistry {
public:
    explicit ParameterRegistry(const ProjectController& controller, RegistryLimits limits = {});
    [[nodiscard]] const ParameterDescriptor& parameter(const std::string& address) const;
    [[nodiscard]] std::vector<ParameterDescriptor> inventory() const;
    [[nodiscard]] const std::vector<ActionDescriptor>& actions() const noexcept { return actions_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] AuthoredChange write(ProjectController& controller, std::uint64_t revision, const std::vector<ParameterWrite>& writes) const;
    [[nodiscard]] AuthoredChange execute(ProjectController& controller, std::uint64_t revision, const std::vector<Command>& commands) const;
    [[nodiscard]] AuthoredChange undo(ProjectController& controller, std::uint64_t revision) const;
    [[nodiscard]] AuthoredChange redo(ProjectController& controller, std::uint64_t revision) const;
private:
    struct Edit;
    struct Binding { ParameterDescriptor descriptor; std::function<void(Edit&,const ParameterValue&)> set; };
    void add(ParameterDescriptor descriptor, std::function<void(Edit&,const ParameterValue&)> setter);
    void check_revision(const ProjectController& controller, std::uint64_t revision) const;
    RegistryLimits limits_;
    std::shared_ptr<const ProjectState> snapshot_;
    std::uint64_t revision_;
    std::map<std::string,Binding> parameters_;
    std::vector<ActionDescriptor> actions_;
    std::map<std::size_t,std::size_t> command_actions_;
};
} // namespace mapgyver::control
