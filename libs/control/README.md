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

Protocol wire encoding, generated API/TypeScript/OSCQuery exports, native runtime
capability reports, render compilation and persistence remain subsequent slices.
This code is independent of OS/GPU headers and does not qualify M01 hardware
requirements or M02 ahead of its dependency.
