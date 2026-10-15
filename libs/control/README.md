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

The typed parameter registry, protocol envelopes, native render compilation and
persistence are subsequent slices. This code is independent of OS/GPU headers
and does not qualify M01 hardware requirements or M02 ahead of its dependency.
