# Control and frame contracts

These contracts define behavior and ownership. They do not require implementing a speculative plugin SDK before real adapters exist. M02-M03 establish the typed model/commands; M08 establishes managed-app interoperation.

## Parameter registry

Each registered parameter has a stable parameter ID, owner ID, canonical address, type, default, validation/range, unit, readable/writable/trigger access, persistence class, description and capability/status flags. Owners are stable domain entities; library folders and friendly labels do not determine canonical identity.

Every application parameter uses this path: layer transforms, clip transport, mesh points, masks, color controls, output binding, client settings, cue actions and structural commands. Derived statuses are discoverable read-only. Optional UI session state belongs in a session namespace. API introspection compares registry inventory with exported tree nodes so new features cannot accidentally become remote-inaccessible.

Example canonical addresses:

```
/show/sources/<source-id>/transport/speed
/show/compositions/<composition-id>/layers/<layer-id>/opacity
/show/cues/<cue-id>/go
/stage/surfaces/<surface-id>/geometry/points/<point-id>/position
/outputs/<output-id>/status
/clients/<instance-id>/parameters/...
/system/capabilities/...
```

Friendly aliases can improve manual OSC operation. Moves/renames preserve canonical addresses. If a deliberate address migration is necessary, publish explicit migration information and OSCQuery structure changes rather than silently breaking bindings.

## Reliable commands

Use a versioned envelope containing protocol version, command ID, client/session identity, command type, expected authored revision, optional desired apply time, and typed payload. Never serialize C++ memory layouts or OS handles as protocol objects.

The controller validates access, finite numeric values, bounds, referential integrity, dependency cycles and capability policy. Structural batches validate entirely before changing authored state. Replies distinguish rejected, accepted, preparing, prepared and applied, with resulting revision, actual show time/frame and structured errors. Exact deduplication scope, retention window and reconnect session behavior are documented; launches and GO actions cannot run twice because an acknowledgement was lost.

Realtime parameter changes can be coalesced; structural actions and cue transactions cannot be dropped silently. Dragging a mesh sends a bounded batch/change set, not an unbounded message per point per event. Client overlays may move optimistically, then reconcile to the applied revision. Initial collaboration uses one edit owner and read-only viewers with explicit takeover/revision rules.

At render boundaries the worker applies complete validated state. Grouped changes on one worker share an application sequence/frame. An output sink may still scan out on an independent physical refresh; the contract must not claim optical synchronization without supporting hardware/presentation configuration.

## OSC and OSCQuery

Route writes/triggers through the same validator and command functions. Expose get/set/trigger and structural operations as documented OSC methods; expose types, ranges, units, access and current values through OSCQuery.

Advertise only implemented extensions. HOST_INFO and HTTP tree queries, binary OSC WebSocket values, exact-path LISTEN/IGNORE and PATH_CHANGED or the finer supported structure updates need interoperability tests against independent clients. Reconnect reloads structure and re-establishes subscriptions. A LISTEN on a container is not automatically a recursive subscription under the proposal.

UDP can lose/reorder messages and is not a reliable transaction channel. Support OSC bundles/timetags according to documented scheduler behavior; expose an explicit transaction/cue method for grouped application. The reliable WebSocket contract supplies acknowledgement, revision and resynchronization features beyond plain OSC. Do not call a bare HTTP tree endpoint a complete OSCQuery implementation.

## Source ports and frames

A source-port descriptor has stable source/port identity, display name, optional semantic role, stream kind, dimensions, pixel format, color/transfer/range, alpha convention, origin/orientation, frame rate/timebase, capability/status and generation. Dimensions and endpoint handles can change without changing the logical port ID.

A frame describes source PTS/clock domain, host arrival time, frame sequence, port/resource generation, dimensions/format and owned/reference lifetime. Payload is a CPU buffer or an internal native-GPU import descriptor with explicit synchronization. Public network descriptors identify a transport endpoint, not a dereferenceable native pointer. Invalid old-generation callbacks must be ignored safely.

A FrameSet associates one source instance/generation and frame identity with a set of named port frames. Cooperating producers publish completion only when every required member is valid and keep resources alive until the agreed consumption point. Texture rings/fences and reuse permissions must prevent a producer overwriting a still-sampled frame. A frame number in OSC metadata alone does not prove GPU completion.

Provide distinct consistency modes: coherent set when supported; hold last complete set; or explicitly independent latest-per-port sampling. Set readiness includes all required bound ports. Legacy independent senders cannot be declared coherent without a suitable producer publication mechanism.

## Managed application manifest

Include protocol version, application/package ID, instance ID, restart generation, display name, OSCQuery endpoint, host identity, supported controls, lifecycle/multi-instance capabilities, readiness/health and a list of port descriptors with transport-specific discovery information.

Port bindings persist stable IDs. Semantic names propose a match and routing presets store deliberate mapping. Reordering discovery results or changing transport endpoint names must not rewire the show.

Lifecycle states are stopped, starting, connecting, preparing, ready, active, stopping and failed/recovering. OSC server connectivity, process liveness and video readiness are separate states. Launch only configured executable definitions; use owned process handles and scoped environment/working-directory configuration. Discovery never grants arbitrary execution permission.

An attached app is externally owned by default. A launched app has explicit reference counting, keep-alive/preload/stop policy and graceful-then-forced shutdown behavior. Retry uses backoff and a crash-loop limit. Restart changes generation, invalidates old frame resources and reconciles parameter state with an explicit keep-local/accept-remote policy that avoids echo loops.

## Adapter and capability contract

Discovery, connect/prepare, frame acquisition/publication, health, disconnect and dispose have bounded, cancellable behavior. Include supported formats, CPU/GPU sharing path, synchronization, native architecture and runtime/library version in capability reports. A missing adapter preserves the source/output object and parameters with an unavailable reason.

Spout and Syphon are platform-local integrations. PipeWire direct video streams are separate from desktop/window capture and its portal permissions. NDI/OMT are network adapters with their own buffering, discovery, encode/decode and bandwidth costs. Apply the same logical model while measuring the actual data path.

## Project persistence

Use a versioned schema with entity/parameter IDs and portable references; serialize authored settings and deliberate resume checkpoints. Never persist process handles, GPU resource identifiers, transient queue contents or every frame tick. Unsupported optional components round-trip unchanged with capability status.

Read older schemas through tested migrations with backups. Reject unsupported newer schemas without modifying the source file. Save As/export uses a consistent SQLite backup/snapshot path including committed WAL state. An authored revision accepted in memory, applied by the renderer and durably saved are distinct statuses shown to operators and remote clients.
