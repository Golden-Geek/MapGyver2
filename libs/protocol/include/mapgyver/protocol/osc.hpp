// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace mapgyver::protocol::osc {
struct Blob {std::vector<std::uint8_t> bytes; bool operator==(const Blob&) const = default;};
struct Timetag {std::uint64_t value = 1; bool operator==(const Timetag&) const = default;};
struct Nil {bool operator==(const Nil&) const = default;};
struct Impulse {bool operator==(const Impulse&) const = default;};
using Argument = std::variant<std::int32_t,float,std::string,Blob,std::int64_t,double,Timetag,bool,Nil,Impulse>;
struct Message {std::string address; std::vector<Argument> arguments; bool operator==(const Message&) const = default;};
struct Packet {
    std::vector<Message> messages;
    std::vector<Timetag> bundle_times; // Exact NTP bits; never rounded to a JSON number.
    [[nodiscard]] bool immediate() const noexcept;
};
struct Limits {std::size_t bytes = 1048576, string_bytes = 262144, arguments = 256, messages = 256, depth = 16, elements = 1024;};
class Error : public std::runtime_error {public: using std::runtime_error::runtime_error;};
[[nodiscard]] std::string types(const Message& message);
[[nodiscard]] std::vector<std::uint8_t> encode(const Message& message,Limits limits = {});
[[nodiscard]] std::vector<std::uint8_t> bundle(const std::vector<std::vector<std::uint8_t>>& elements,Timetag time = {},Limits limits = {});
[[nodiscard]] Packet decode(std::span<const std::uint8_t> bytes,Limits limits = {});
} // namespace mapgyver::protocol::osc
