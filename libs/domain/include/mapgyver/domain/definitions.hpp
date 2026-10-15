// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace mapgyver::domain {
// Opaque stable identity. Labels, discovery order and paths never determine IDs.
class Id {
public:
    explicit Id(std::string value);
    [[nodiscard]] static Id random(); // UUIDv4; identity only, never an authentication token.
    [[nodiscard]] const std::string& str() const noexcept { return value_; }
    auto operator<=>(const Id&) const = default;
private:
    std::string value_;
};
enum class ErrorCode { invalid_id, duplicate_id, invalid_name, invalid_port, missing_reference, type_mismatch, dependency_cycle, invalid_definition, capacity_exceeded, invalid_transport, invalid_geometry, invalid_layout };
class ModelError : public std::runtime_error {
public:
    ModelError(ErrorCode code, std::string message, std::vector<Id> involved = {});
    [[nodiscard]] ErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const std::vector<Id>& involved() const noexcept { return involved_; }
private:
    ErrorCode code_;
    std::vector<Id> involved_;
};
void validate_display_name(std::string_view value, const Id& owner);
enum class StreamKind { video, audio, number, boolean, text, trigger };
struct Port {
    Id id;
    std::string name;
    StreamKind kind;
    bool operator==(const Port&) const = default;
};
struct ExternalSource {
    // A configured adapter name/reference, never a native handle or capability claim.
    // Unknown/unavailable adapters remain valid definitions for round-trip persistence.
    std::string adapter;
    std::string locator;
    std::vector<Port> outputs;
    bool operator==(const ExternalSource&) const = default;
};
struct CompositionSource {
    Id composition;
    bool operator==(const CompositionSource&) const = default;
};
struct Asset {
    Id id;
    std::string name;
    std::variant<ExternalSource, CompositionSource> source;
    bool operator==(const Asset&) const = default;
};
struct AssetPort {
    Id asset;
    Id port;
    bool operator==(const AssetPort&) const = default;
};
struct InputPort {
    Id port;
    bool operator==(const InputPort&) const = default;
};
struct Layer {
    Id id;
    std::string name;
    StreamKind kind;
    std::variant<AssetPort, InputPort> source;
    // Explicit sharing within one composition instance. Equal groups must refer
    // to the same asset; separate instantiations still own separate live scopes.
    std::optional<Id> shared_group;
    bool operator==(const Layer&) const = default;
};
struct Canvas {
    std::uint32_t width = 1920, height = 1080;
    std::string pixel_format = "rgba8-unorm";
    std::string colour_space = "srgb";
    std::string alpha = "straight";
    bool operator==(const Canvas&) const = default;
};
struct Composition {
    Id id;
    std::string name;
    std::vector<Port> inputs;
    std::vector<Port> outputs;
    std::vector<Layer> layers;
    // Logical video output sizes/formats remain independent of projector modes.
    // Missing configuration is authored/unprepared, not a claimed render format.
    std::map<Id,Canvas> canvases{};
    bool operator==(const Composition&) const = default;
};
struct Definitions {
    std::map<Id, Asset> assets;
    std::map<Id, Composition> compositions;
    bool operator==(const Definitions&) const = default;
};
// Control-side immutable validated snapshot. It owns no decoder, clock, process,
// texture or UI object. Render state will be compiled separately from this model.
class DefinitionSnapshot {
public:
    explicit DefinitionSnapshot(Definitions definitions);
    [[nodiscard]] const Definitions& data() const noexcept { return *definitions_; }
    [[nodiscard]] const Asset& asset(const Id& id) const;
    [[nodiscard]] const Composition& composition(const Id& id) const;
    [[nodiscard]] const std::vector<Port>& outputs(const Id& asset) const;
    [[nodiscard]] const Port& output(const AssetPort& source) const;
private:
    std::shared_ptr<const Definitions> definitions_;
};
} // namespace mapgyver::domain
