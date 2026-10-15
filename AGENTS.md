# Implementation instructions

These instructions apply when implementing the application in the target repository. Preparing or editing this planning pack does not count as implementing an application milestone.

## Read only what the task needs

Read `docs/STATUS.md`, `docs/DECISIONS.md`, the requested milestone in `IMPLEMENTATION_PLAN.md`, and the relevant contracts/tests. `docs/backlog.json` owns IDs and dependency order. For multi-step work, use `PLANS.md` and maintain `docs/execution/ACTIVE.md`. Use `python tools/show_milestone.py Mxx` for focused backlog context. Do not reload every document for every small change.

## Product invariants

- Windows x64 is the first qualified release. Domain/control/persistence code must remain portable C++20.
- Surface, texture and projector counts are arbitrary. Wall/floor are example labels only.
- A default composition makes one-surface import/drop/map/play direct. More complex routing remains editable without requiring every user to build a graph.
- Assets are reusable definitions; source instances own playback/process state. Independent placements are independent by default. Shared live instances are explicit.
- Compositions expose named typed ports. Surfaces consume composition content; projection patches own physical calibration. Cue changes do not rewrite calibration.
- UI, OSC, OSCQuery and remote commands use the same validated domain model. Read-only state stays read-only. Structural actions are remotely available.
- Native program output and its clock do not depend on browser callbacks, UI visibility or preview subscribers.
- Keep unsupported platform features as configured objects with capability/status reasons. Never delete them on load.
- GPL-3.0-or-later is the implementation default for newly authored app code. Verify third-party provenance. Do not add a license exception or distribute proprietary runtimes without the owner-approved legal path recorded in an ADR.

## Architecture and deadline rules

- Platform GPU/OS/SDK headers belong in adapters. No HWND, D3D pointer, IOSurface, DMA-BUF or renderer-library type leaks into domain, persistence or network schemas.
- Renderer choice is provisional until M01. Use the smallest render interface needed by a measured path; avoid speculative engines, generic plugin frameworks and empty future backends.
- Render/audio deadline paths must not perform disk/network operations, media probing, JSON work, process launches, shader compilation, synchronous readback or unbounded allocation/locking.
- Control changes produce validated immutable render state. Resource handoff has explicit ownership, completion synchronization and generation IDs.
- Queues, caches, imports and previews have capacity/backpressure/cancellation policies. Report CPU transfers separately from GPU copies.
- Use a monotonic show clock and rational media timestamps. Define applied revision/frame; never claim synchronized physical scanout from a shared software timestamp alone.
- Keep NDI optional at build/runtime until its licensing and native dependency gates are resolved. Required functionality remains visibly open if it is blocked.

## Working and validation

Implement the selected ready milestone in coherent vertical slices. Inspect existing code before editing, preserve unrelated changes and use the project build/test wrappers. Add meaningful tests for state, timing, ownership, protocol, persistence and recovery contracts; avoid tests that only restate trivial implementation.

Never fake SDK success, hardware counters, decoder acceleration or supported capabilities. A mock, WARP run, hosted compile, emulated architecture and real-GPU run are different evidence classes. If required Windows/GPU hardware is unavailable, report exactly what passed and keep that gate open. Continue independent authorized work; do not mark dependent milestones complete.

Do not suppress failures with blanket `continue-on-error`, empty tests, skipped assertions or widened tolerances without investigation. A test quarantine needs a tracked reason and must not satisfy the affected release requirement.

## Commits and milestone completion

The commit sequence in `docs/backlog.json` is the default slicing plan. Preserve commit IDs in progress evidence even if one slice requires several coherent commits. Keep builds green at each implementation commit when the applicable environment exists.

For tasks authorizing implementation, create local commits for completed tested slices using the planned Conventional Commit subject or a more precise equivalent. Do not amend/rewrite other work, force push, publish releases or change remote repository policy outside the current task authorization.

Before checkpointing a milestone: satisfy its scenarios, run required checks, record actual commands/results/source SHAs/CI URLs and known gaps in `docs/progress/Mxx.md`, update STATUS and the execution plan, and make the completion documentation commit. Annotated `milestone/mxx` tags may point only to a final green commit with the required evidence. Do not write an invented hash or a self-referential completion hash into the file.

All milestones start planned. A documentation-only completion record cannot substitute for a missing hardware qualification. Continue across completed milestones when the task authorizes it; no routine approval checkpoint is required at each commit.
