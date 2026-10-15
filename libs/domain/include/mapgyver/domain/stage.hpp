// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/domain/instances.hpp>
#include <array>

namespace mapgyver::domain {
struct Vec2 {
    double x = 0, y = 0;
    bool operator==(const Vec2&) const = default;
};
struct Triangle {
    std::array<Id,3> vertices;
    bool operator==(const Triangle&) const = default;
};
struct SurfaceVertex {
    Id id;
    Vec2 local; // Logical surface coordinates, not projector pixels.
    Vec2 uv;
    bool operator==(const SurfaceVertex&) const = default;
};
struct SurfaceGeometry {
    std::vector<SurfaceVertex> vertices;
    std::vector<Triangle> triangles;
    bool operator==(const SurfaceGeometry&) const = default;
};
struct Surface {
    Id id;
    std::string name;
    // Each surface has its own logical placement lease. Multiple leases may
    // explicitly consume one composition instance/port; direct media is invalid.
    std::optional<Id> content_placement;
    SurfaceGeometry geometry;
    bool operator==(const Surface&) const = default;
};
struct SurfaceEdge {
    Id surface, first, second;
    bool operator==(const SurfaceEdge&) const = default;
};
struct Seam {
    SurfaceEdge first, second;
    bool reversed = false;
    bool operator==(const Seam&) const = default;
};
struct SurfaceGroup {
    Id id;
    std::string name;
    std::vector<Id> surfaces;
    std::vector<Seam> seams; // Deliberate metadata; not automatic unfolding/alignment.
    bool operator==(const SurfaceGroup&) const = default;
};
enum class LayoutMode { independent, continuous };
struct LayoutVertex {
    Id id;
    Vec2 surface_uv, composition_uv;
    bool operator==(const LayoutVertex&) const = default;
};
struct SurfaceChart {
    Id surface;
    std::vector<LayoutVertex> vertices;
    std::vector<Triangle> triangles;
    bool operator==(const SurfaceChart&) const = default;
};
struct Layout {
    Id id;
    std::string name;
    Id group;
    LayoutMode mode = LayoutMode::independent;
    std::vector<SurfaceChart> charts;
    bool operator==(const Layout&) const = default;
};
struct DesiredOutputMode {
    std::uint32_t width = 1920, height = 1080;
    Rational refresh_hz{60,1};
    bool operator==(const DesiredOutputMode&) const = default;
};
struct OutputTarget {
    Id id;
    std::string name;
    std::string adapter; // Configured name, including unavailable/unknown adapters.
    std::string endpoint; // Persistent desired binding; never an OS pointer/index.
    DesiredOutputMode desired;
    bool operator==(const OutputTarget&) const = default;
};
enum class WarpMode { homography, mesh };
struct WarpVertex {
    Id id;
    Vec2 surface_uv, projector_pixels;
    bool operator==(const WarpVertex&) const = default;
};
struct Calibration {
    WarpMode mode = WarpMode::homography;
    std::uint32_t reference_width = 1920, reference_height = 1080;
    std::vector<WarpVertex> vertices;
    std::vector<Triangle> triangles;
    // Explicit stable-ID boundary for a homography quad. Mesh mode may omit it.
    std::vector<Id> boundary;
    bool operator==(const Calibration&) const = default;
};
struct ProjectionPatch {
    Id id;
    std::string name;
    Id surface, output;
    Calibration calibration;
    bool operator==(const ProjectionPatch&) const = default;
};
struct Stage {
    std::map<Id,Surface> surfaces;
    std::map<Id,SurfaceGroup> groups;
    std::map<Id,Layout> layouts;
    std::map<Id,OutputTarget> outputs;
    std::map<Id,ProjectionPatch> patches;
    bool operator==(const Stage&) const = default;
};
struct StageLimits {
    std::size_t surfaces = 65536, groups = 65536, layouts = 65536, outputs = 65536, patches = 262144;
    std::size_t vertices = 1048576, triangles = 1048576;
};
class StageSnapshot {
public:
    explicit StageSnapshot(Stage stage, const InstancePool& instances, StageLimits limits = {});
    [[nodiscard]] const Stage& data() const noexcept { return *stage_; }
private:
    std::shared_ptr<const Stage> stage_;
};
[[nodiscard]] SurfaceGeometry unit_quad();
[[nodiscard]] Calibration quad_calibration(std::uint32_t width, std::uint32_t height);
[[nodiscard]] SurfaceChart quad_chart(const Id& surface, Vec2 canvas_min = {0,0}, Vec2 canvas_max = {1,1});

struct DefaultProject {
    InstancePool instances;
    StageSnapshot stage;
    Id root_placement, root_instance, content_input, surface, output;
};
// Logical one-surface startup only. No physical adapter capability is claimed.
[[nodiscard]] DefaultProject make_default_project(Definitions library = {}, InstancePool::IdGenerator ids = Id::random);
} // namespace mapgyver::domain
