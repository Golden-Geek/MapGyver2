# Portable domain model

DefinitionSnapshot is a validated immutable control-side model. Stable opaque
IDs own identity; names, layer order and adapter discovery do not. The library
uses only C++20 standard-library types and owns no native SDK/GPU/UI resources.

Assets are reusable definitions. CompositionSource references a composition
definition with typed input/output ports and ordered layers. AssetPort and
InputPort bindings use stable IDs, and type mismatches or dependency cycles
are rejected before publication. A local shared_group is an explicit request
to reuse one source within one composition instance, including different ports
of the same app. It never implies sharing across separate composition instances.
Configured unknown adapters remain authored objects; their runtime availability
will be a separate read-only status.

InstancePool allocates independent source/child instances for each placement by
default. Child ownership is scoped by parent instance and stable layer ID; local
sharing groups are scoped by the parent instance too. Inputs bind a source per
composition instance, with explicit existing-instance sharing as an alternative.
Live bindings have a separate iterative cycle check, including direct/indirect
feedback. Structural edits validate a candidate before publishing; configured
instance/placement/ownership capacities fail atomically, including nested graph
expansion. These are resource capacities, not a special surface/output count.

Rational transport intent and keep-alive/reference policy belong to the instance
owner. Removing one shared consumer preserves the owner; the last reference
retires it unless keep-alive says otherwise. Iterative retirement handles deep
graphs and shared descendants. Returned retired IDs request logical resource
retirement; they do not claim SDK/process disposal completed. Unknown configured
adapters can have retained intent without claiming readiness or decoded frames.
Actual decoder position/status remains a separate runtime report.

The pool has one control owner and is pinned to an immutable definition revision.
It is not a render-thread data source. reconcile_definitions builds a candidate
ownership graph: unchanged composition scope/layer/share-group IDs retain their
instances and transport intent through rename/reorder. Changed asset or sharing
scope creates independent state; surviving external owners and keep-alive roots
retain their source. Removed bound inputs/outputs reject the edit. Replacement
reserves peak instance capacity before old sources retire. Snapshot copies share
the ID allocation context, so undo cannot rewind a stateful allocator. Native
resource preparation and immutable render-state publication remain separate.

Canonical parameter descriptors/commands, advanced mapping and persistence
are subsequent slices; no empty backend/plugin abstractions are
added here. Unit model evidence remains separate from M01 hardware qualification.

StageSnapshot validates arbitrary surfaces, groups, explicit UV charts/seams and
many-to-many projection patches against the source-instance pool. Surfaces consume
video composition ports through distinct ownership leases. Continuous layouts
deliberately share one composition instance/output; independent layouts can use
different content. Geometric points/topology use stable IDs rather than indices.
Coordinates remain explicit: surface-local, surface UV, composition UV and
projector pixels. Degenerate/duplicate faces and invalid homography quads are
rejected. Mesh warps remain distinct from convex four-corner homographies.

Composition canvases own logical size/format/colour/alpha declarations. Output
targets own desired persistent adapter bindings/modes; patches own calibration
and its reference resolution. Changing content or desired output mode does not
rescale/rewrite calibration. Unknown adapters/formats stay configured without
claiming runtime support. Stage/geometry capacities are configurable resource
policies, not a special surface/projector count.

make_default_project creates a root composition with a content input/default
slot and Program canvas, one surface, one logical output and one four-corner
patch. A project-owned root lease keeps logical source ownership independent of
surface consumers. This is logical startup; native binding/frames are not implied.
The command owner must validate the combined instance/stage candidate before
publishing edits, including dependent-reference deletion and definition changes.
