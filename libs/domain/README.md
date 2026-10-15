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
It is not a render-thread data source. Command-driven definition editing must
reconcile scoped instance ownership and publish separately compiled immutable
render state; definition rename/reorder must preserve live IDs and transport.

Canonical parameter descriptors/commands, surface/projector calibration and
persistence are subsequent M02 slices; no empty backend/plugin abstractions are
added here. Unit model evidence remains separate from M01 hardware qualification.
