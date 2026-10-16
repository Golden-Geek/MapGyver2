// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/control/project.hpp>
#include <array>
#include <span>

namespace mapgyver::render {
using domain::Id;
using PortKey = std::pair<Id,Id>; // source instance, named output port
struct Pixel {float r = 0, g = 0, b = 0, a = 0; bool operator==(const Pixel&) const = default;};
struct Image {
    Id resource;
    std::uint64_t generation;
    std::uint32_t width, height;
    std::shared_ptr<const std::vector<Pixel>> pixels; // linear-sRGB, premultiplied
};
using Images = std::map<PortKey,std::shared_ptr<const Image>>;
enum class Transfer { linear, srgb };
enum class Alpha { straight, premultiplied };
struct Encoding {Transfer transfer = Transfer::srgb; Alpha alpha = Alpha::straight;};
struct Issue {PortKey source; std::string reason;};
struct Node {
    PortKey source;
    std::uint32_t width, height;
    Encoding encoding;
    std::shared_ptr<const Image> image;
    std::vector<std::uint32_t> layers; // back-to-front, resolved video branches only
    std::optional<std::uint32_t> target;
};
struct Target {std::uint32_t width, height;}; // native internal RGBA16F
struct Surface {Id id; std::uint32_t node;};
struct Plan {
    std::uint64_t revision;
    std::vector<Node> nodes; // dependency-first, immutable after compilation
    std::vector<Target> targets;
    std::vector<Surface> surfaces;
    std::uint32_t preview;
    std::vector<Issue> issues;
    std::uint64_t target_bytes = 0, image_upload_bytes = 0, export_bytes = 0, fallback_bytes = 0;
};
struct Limits {
    std::size_t nodes = 16384, layers = 262144, targets = 256;
    std::uint32_t dimension = 16384;
    std::uint64_t target_bytes = 268435456, image_bytes = 268435456, total_payload_bytes = 536870912;
};
class Error : public std::runtime_error {public: using std::runtime_error::runtime_error;};
// Control/preparation path only. Never performs IO or touches native SDK objects.
[[nodiscard]] std::shared_ptr<const Plan> compile(const control::ProjectState& project,std::uint64_t revision,
                                               const Images& images,Limits limits = {});
// Preparation/reference functions; these allocate and are not deadline callbacks.
[[nodiscard]] std::shared_ptr<const Image> rgba8(Id resource,std::uint64_t generation,std::uint32_t width,
    std::uint32_t height,std::span<const std::uint8_t> bytes,Encoding encoding = {},std::uint64_t byte_limit = 268435456);
[[nodiscard]] std::shared_ptr<const Image> pattern(Id resource,std::uint64_t generation,std::uint32_t width,
    std::uint32_t height,std::string_view name,std::uint64_t byte_limit = 268435456);
[[nodiscard]] Pixel over(Pixel foreground,Pixel background) noexcept;
// Display reference composites linear premultiplied content over black, then
// encodes opaque sRGB. Named canvas exports retain their declared alpha/transfer.
[[nodiscard]] std::vector<std::uint8_t> reference(const Plan& plan,std::uint32_t node,bool display = false);
} // namespace mapgyver::render
