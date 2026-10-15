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

The next live-state slice must allocate independent source/child instances for
each placement by default. Child ownership is scoped by parent instance and
stable layer ID; local sharing groups are scoped by the parent instance too.
Inputs bind a source per composition instance, with explicit existing-instance
sharing as an alternative. Actual live bindings need their own cycle validation,
including attempts to feed an instance from itself or its descendants. Rational
transport timestamps, reference/lifecycle policy and configurable capacity
limits belong to the instance owner, not the asset. No decoder success is implied
by changing logical transport intent.

Canonical parameter descriptors/commands, surface/projector calibration and
persistence are subsequent M02 slices; no empty backend/plugin abstractions are
added here. Unit model evidence remains separate from M01 hardware qualification.
