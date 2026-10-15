// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/control/registry.hpp>
#include <nlohmann/json.hpp>
#include <string_view>

namespace mapgyver::control::wire {
using Json = nlohmann::json;
enum class ErrorCode { malformed, unsupported_version, capacity_exceeded };
class Error : public std::runtime_error {
public:
    Error(ErrorCode code, const char* message) : std::runtime_error(message), code_(code) {}
    [[nodiscard]] ErrorCode code() const noexcept {return code_;}
private:
    ErrorCode code_;
};
struct Limits {std::size_t bytes = 16777216, events = 1048576, depth = 64;};
// Control/IO thread only. Reject duplicate keys, invalid UTF-8, nonfinite
// numbers, oversized documents and excessive depth before typed construction.
[[nodiscard]] Json parse(std::string_view bytes, Limits limits = {});
[[nodiscard]] Json encode_project(const ProjectState& project);
[[nodiscard]] ProjectState decode_project(const Json& document, ProjectLimits project_limits = {},
    InstanceLimits instance_limits = {}, InstancePool::IdGenerator ids = Id::random);
[[nodiscard]] Json encode_command(const Command& command);
[[nodiscard]] Command decode_command(const Json& document);
[[nodiscard]] Json encode_write(const ParameterWrite& write);
[[nodiscard]] ParameterWrite decode_write(const Json& document);
[[nodiscard]] Json describe(const ParameterRegistry& registry);
// Generated from the same field registration used by the codecs. Structural
// schema validity does not replace domain graph/geometry/capacity validation.
[[nodiscard]] Json schema();
} // namespace mapgyver::control::wire
