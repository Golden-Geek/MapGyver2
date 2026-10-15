// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/domain/definitions.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace mapgyver::domain {
struct Rational {
    std::int64_t numerator = 1;
    std::int64_t denominator = 1;
    bool operator==(const Rational&) const = default;
};
struct MediaTime {
    std::int64_t ticks = 0;
    Rational time_base{1,1'000'000};
    bool operator==(const MediaTime&) const = default;
};
// Logical control intent, not evidence that a decoder/app/frame is ready or has
// applied a seek. Actual media/playback state is reported by the runtime adapter.
struct TransportIntent {
    MediaTime position{};
    Rational rate{};
    bool playing = false;
    bool loop = false;
    bool operator==(const TransportIntent&) const = default;
};
struct InstancePort {
    Id instance;
    Id port;
    bool operator==(const InstancePort&) const = default;
};
struct SourceInstance {
    Id id;
    Id asset;
    TransportIntent transport{};
    bool keep_alive = false;
    std::uint64_t owner_references = 0;
    // Layer and sharing identities are scoped by this parent live instance.
    std::map<Id,InstancePort> layers;
    std::map<Id,InstancePort> inputs;
    bool operator==(const SourceInstance&) const = default;
};
struct Placement {
    Id id;
    InstancePort source;
    bool operator==(const Placement&) const = default;
};
struct InstanceLimits {
    std::size_t instances = 65536;
    std::size_t placements = 65536;
    std::size_t ownership_bindings = 262144;
};
// A single control owner mutates this pool. Callers must not retain borrowed
// map references across edits; the render worker never reads this mutable pool.
// Every structural edit builds/validates a candidate before publishing it.
class InstancePool {
public:
    using IdGenerator = std::function<Id()>;
    explicit InstancePool(DefinitionSnapshot definitions, InstanceLimits limits = {}, IdGenerator ids = Id::random);
    // Rehydrate logical state only. Verify stored derived counts against a fresh
    // ownership traversal; no SDK/process/resource state is resumed here.
    [[nodiscard]] static InstancePool restore(DefinitionSnapshot definitions,
        std::map<Id,SourceInstance> instances, std::map<Id,Placement> placements,
        InstanceLimits limits = {}, IdGenerator ids = Id::random);
    [[nodiscard]] Placement place(const AssetPort& source, bool keep_alive = false);
    [[nodiscard]] Placement place_shared(const InstancePort& source);
    // Returned IDs are logical resources to retire, not completed SDK shutdowns.
    [[nodiscard]] std::vector<Id> remove_placement(const Id& placement);
    [[nodiscard]] std::vector<Id> bind_input(const Id& owner, const Id& input, const AssetPort& source, bool keep_alive = false);
    [[nodiscard]] std::vector<Id> bind_shared_input(const Id& owner, const Id& input, const InstancePort& source);
    [[nodiscard]] std::vector<Id> unbind_input(const Id& owner, const Id& input);
    [[nodiscard]] std::vector<Id> set_keep_alive(const Id& instance, bool keep_alive);
    void set_transport(const Id& instance, TransportIntent intent);
    // Reconcile definition-owned children by stable scope/layer/share-group IDs.
    // Replacements reserve capacity before old scopes retire; failure is atomic.
    [[nodiscard]] std::vector<Id> reconcile_definitions(DefinitionSnapshot definitions);
    [[nodiscard]] const SourceInstance& instance(const Id& id) const;
    [[nodiscard]] const std::map<Id,SourceInstance>& instances() const noexcept { return instances_; }
    [[nodiscard]] const std::map<Id,Placement>& placements() const noexcept { return placements_; }
    [[nodiscard]] std::size_t ownership_bindings() const noexcept { return bindings_; }
    [[nodiscard]] const DefinitionSnapshot& definitions() const noexcept { return definitions_; }
private:
    [[nodiscard]] Id fresh_id();
    [[nodiscard]] Id create_instance(const Id& asset, bool keep_alive);
    [[nodiscard]] Id instantiate(const Id& asset, bool keep_alive);
    [[nodiscard]] const Port& input_port(const Id& owner, const Id& port) const;
    [[nodiscard]] const Port& output_port(const InstancePort& source) const;
    void acquire(const Id& instance);
    void release(const Id& instance, std::vector<Id>& retired);
    void collect(const Id& instance, std::vector<Id>& retired);
    void validate_live_graph() const;
    DefinitionSnapshot definitions_;
    InstanceLimits limits_;
    // Allocation context survives candidate/history copies; restoring a model
    // snapshot must not rewind a stateful ID allocator and reuse issued IDs.
    std::shared_ptr<IdGenerator> ids_;
    std::map<Id,SourceInstance> instances_;
    std::map<Id,Placement> placements_;
    std::size_t bindings_ = 0;
};
} // namespace mapgyver::domain
