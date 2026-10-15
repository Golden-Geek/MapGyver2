# Portable authored control

ProjectController owns one editing thread's authoritative immutable ProjectState.
Ordered typed commands edit a candidate instance pool and stage. The complete
candidate validates before publication; a failed command, reference, capacity or
history reservation leaves state, revision and undo/redo unchanged. ID allocation
is deliberately consumed across rejected candidates and undo branches.

Definitions and stage replacement cover structural edits; source placement,
explicit sharing, input binding, transport intent, keep-alive and surface content
assignment have typed commands. Definition reconciliation preserves unchanged
live scopes. A surface assignment creates its own lease before releasing the old
one. The project-owned root video composition lease survives surface deletion.
Stage dependencies can be removed in the same batch as their placement owners.
Input references must be unbound before removing their definition ports.

Every operation requires the current authored revision. Successful transactions,
undo and redo increment it; even a valid no-op transaction is a journal entry.
Snapshots remain immutable, and undo/redo restores exact stable IDs and logical
intent. New edits after undo discard redo. Configurable command, snapshot record,
snapshot UTF-8 byte, history entry/record/byte capacities bound retained work.
History conservatively counts two complete logical snapshots per entry, including
IDs/references, and evicts whole oldest entries. These counts are software
accounting, not allocator or RSS measurements. Callers retaining their own
snapshot references own that additional lifetime and memory.

AuthoredChange identifies created/retired logical source IDs and new placements.
It does not launch/stop adapters, seek decoders or report a rendered frame. Undo
may restore an earlier logical source ID; native resources must use separate
generation IDs and preparation/application outcomes. Durable saves are separate.

ParameterRegistry freezes one authored revision's inventory. The registration
that supplies each descriptor also supplies its typed setter: types, context or
explicit defaults, scalar bounds, units, description, access and persistence
class all travel together. Its availability flag means the authored control can
be used; it does not claim the configured native adapter is supported or ready.
Unknown adapter/format values remain editable configured data. Reference and
topology defaults that depend on other objects are explicitly absent.

Canonical addresses contain stable owner/scope IDs and fixed parameter IDs.
Names, layer/port order and geometry-vector order do not change addresses.
Current definitions, canvas, source intent, stage, charts and calibration fields
are registered, with compound typed values for rational/media time, points,
bindings, ID lists, topology and seams. Identity/derived ownership bindings are
read-only; their structural changes use registered actions. Every typed command
must have an action descriptor, including source/placement/input ownership edits;
undo/redo are triggers too. New command types without registration fail closed.

Writes validate exact types, finite coordinates and registered scalar ranges,
then collect changes to the same object before one project transaction. Domain
validation still checks the complete candidate's references, topology and rational
constraints. Duplicate parameter writes are ordered with the last value winning.
Registry inventories from an older revision reject writes/actions even when undo
restores the same snapshot pointer. Parameter count and write-batch capacities
are configurable. Transport position/playing metadata requests explicit resume
checkpoints; it never labels an actual decoded playhead as authored persistence.

wire.hpp supplies strict versioned project, command, parameter-write and registry
documents. One field list drives encoding, decoding and generated JSON Schema.
Stable-ID maps and ordered vectors preserve identity and authored order. Tagged
variants use fixed semantic names, not C++ variant indices or memory layouts.
All 64-bit integers use canonical decimal strings, including rational components,
media ticks, derived counts and revisions; 32-bit dimensions remain JSON numbers.
This preserves exact values in JavaScript. The schema's x-mapgyver integer bounds
document string ranges; the codec enforces them. Graph/geometry and ownership
invariants remain domain validation, beyond structural JSON Schema validation.

External bytes must enter through wire::parse: configurable byte, parser-event
and nesting limits apply, with duplicate keys, invalid UTF-8 and malformed JSON
rejected. Typed decoding rejects missing/unknown fields, unknown enum/variant
tags, noncanonical integers and unsupported versions. These APIs run outside
render/audio deadlines. Project restoration recomputes ownership and verifies
stored derived counts, local static-child scope, typed bindings, rational intent,
cycles, reachability and capacities before validating the combined stage/root.
Restoration never launches an app, resumes a decoder or restores native resources.

ProjectDocument is a complete control snapshot, including requested transport
intent. It is not the durable project format or an automatic resume operation.
M12 must apply persistence/checkpoint metadata and SQLite durability/migrations.
The current wire command document is a typed payload, not the reliable M03
request/session/deduplication/scheduling envelope. OSCQuery service, generated
TypeScript exports, native capability reports and render application remain
subsequent slices. This code is independent of OS/GPU headers and does not
qualify M01 hardware requirements or M02 ahead of its dependency.

Regenerate the checked schema after changing wire field registration:

```powershell
& out/build/windows-local/libs/control/Debug/mapgyver_control_wire_test.exe --write-schema schemas/control-model-v1.schema.json
```

control.wire compares the checked schema with generated definitions and tests
complete 1/3/37-surface round trips, sharing/independent intent, unknown adapters,
all parameter/command alternatives, registry inventory and malformed input.
`--export-fixtures out/reports/wire-fixtures` exports all four document classes
for an independent JSON Schema validator. No validator package is linked or
bundled into MapGyver; local validation tool versions are retained as evidence.
