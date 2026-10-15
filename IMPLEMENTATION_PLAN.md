# Windows First Projection Mapping Implementation Plan

Build a GPL projection-mapping application with a native, deadline-driven media engine and an elegant local/remote web editor. Deliver a qualified Windows x64 beta first. Preserve architecture and early portable-core checks for Windows ARM64, macOS ARM64, Linux x64 and Linux ARM64. Raspberry Pi receives named hardware profiles; 32-bit armhf is a distinct optional port.

The first useful product must make one-surface operation direct and arbitrary connected-surface shows equally natural: import a source, place it in a default composition, map a surface, then launch content with cues. Surface counts, texture counts and projector counts are independent. A sophisticated internal model must reduce operator work rather than expose a wall of setup selectors.

This plan is an implementation contract for Codex. It provides 20 dependency-ordered milestones, 80 proposed commit slices, 16 requirement groups and 34 observable acceptance scenarios. IDs are authoritative in [docs/backlog.json](docs/backlog.json). No application implementation or remote CI execution is implied by this handoff.

## Delivery boundary and non-negotiable behavior

Windows x64 is the first fully qualified runtime/editor package. M00-M13 comprise that delivery. M14-M17 implement the remaining native ports; M18-M19 extend calibration and execution scale. Linux/macOS portable-domain checks run from M00, so a Windows feature cannot quietly contaminate the shared model with OS types.

A Windows beta must include organized library import and relinking, video playback and accurate scrubbing, layered/nested compositions, named outputs, cues/transitions, arbitrary surface sets, basic and advanced manual mapping, managed apps, required Windows transports, complete OSC/OSCQuery application control, remote UI and tested recovery. NDI's current distribution/legal gate is explicit. If a mandatory capability is unresolved, describe a narrower preview honestly and retain the blocked requirement; do not call the full contract complete.

The application has no two-surface special case. A source may expose one atlas, several semantic outputs or several independent video streams. A surface may use multiple layers. A surface can span several projectors, and a projector can cover several surfaces. Cues change content and source state; calibrated projection patches remain intact.

## User workflows that determine the architecture

### One surface

A new project automatically owns a root composition, a default content slot, a logical surface and a logical output. Import/drop assigns an independent source instance by default. Entering mapping exposes four natural corners and numeric controls. Advanced groups, ports and graph wiring are available when useful, without being prerequisites for playback.

### Arbitrary groups and continuous content

A group is an operational selection: three walls, curved panels, a room or disconnected screens. Optional adjacency/shared-edge/seam metadata expresses physical or UV continuity. A canvas layout maps composition coordinates to each surface and can contain explicit UV charts/seams; arbitrary geometry need not have a distortion-free single unfolding.

A routing preset binds stable source-port IDs to composition content slots. Semantic roles can suggest a match, but the persisted explicit binding wins. Never infer durable identity from list order or display names. A source with a single atlas can feed several cropped regions; a multi-port app can feed several independent canvases; all arrangements reuse the same binding model.

### Show operation

Cues hold Replace/Keep/Clear per destination, source instance policy, transport actions, app parameter snapshots and transition settings. Preparing a cue creates/prewarms the needed resources, reports readiness and predicts peak budgets. GO commits one validated scene change at a defined show-clock boundary. Common progress and deterministic seeds synchronize a transition across a group. Physically synchronized scanout still requires suitable output/hardware support.

Predefine timeout, missing-port, late-frame and interrupted-transition policy. Default repeated GO is deduplicated by command ID. Rapid new cues queue or deliberately replace the active transition according to a documented mode; avoid undefined partial state. A crossfade between two states of one app needs two renderable instances/states or a captured outgoing frame.

## Domain model and ownership

| Entity | Owns | Does not own |
| --- | --- | --- |
| Asset | Reusable source definition, identity, metadata, file/app reference and library organization | A mandatory shared playhead or a GPU texture |
| SourceInstance | Decoder/live receiver/app instance, transport, readiness and named ports | Physical projector calibration |
| Layer or Clip | Per-use transforms/effects, source-instance/port binding and timeline relation | A hidden copy of library organization |
| Composition | Ordered layers/groups, nested composition references, named output canvases and formats | A physical display index |
| Cue | Target assignments, actions, transitions and preparation policy | Surface/projector geometry |
| Surface | Logical content region, quad/mesh description and UV coordinates | A specific media type |
| SurfaceGroup and Layout | Operational grouping, optional adjacency and composition-to-surface layout | Automatic texture-count assumptions |
| ProjectionPatch | Surface-to-projector geometry, warp, mask, blend and correction | Source playback state |
| OutputTarget | Persistent desired mode, logical identity and a physical/network adapter binding | Ephemeral OS monitor ordering |

Use stable IDs for assets, instances, ports, entities and parameters. Keep canonical addresses stable through user-facing rename/reorder/folder moves. Inline children stay visible in the composition hierarchy. Offer reveal asset, find usages, make independent and promote to reusable actions.

Compositions compile to an acyclic dependency graph. Reject cycles before activation and report the involved objects. Reuse work only when instance/time/effect state is identical. Shared assets alone do not justify sharing decoder playheads. One managed app exposing several textures is normally one running instance with several consumers and one explicit lifecycle policy.

## Runtime and process architecture

The controller owns authored state and validates commands. It produces prepared immutable state for a native render worker. A minimal Windows supervisor can restart failed workers; the supervisor's duties do not force rendering into Windows Service Session 0. For physical outputs and local texture sharing, run in the appropriate logged-in graphics session. Remote-only means no editor window is required, not that every platform graphics/session prerequisite disappears.

The render worker owns the GPU device, render resources, frame selection and output presentation. Start with one device/worker. Source workers isolate expensive probing/decoding or risky SDK behavior when the measured fault/copy tradeoff warrants it. Simple GPU-native adapters may live with their GPU owner. Do not introduce a process per trivial layer.

UI close, reload, exception and network loss leave native playback running. Controller interruption must not invalidate the last applied render snapshot or the render-owned show-clock continuity. Worker restart reconstructs resources from valid state and records the unavoidable output interruption; it does not magically preserve GPU memory after device loss.

Keep message envelopes versioned and independent of STL layouts. Local controller/worker IPC can begin with Windows named pipes behind a transport interface; remote editor commands use WebSocket. Frame resources use explicit native sharing/ownership paths rather than serializing texture handles into public JSON. Headless diagnostics and portable-domain tests require no GPU or browser.

## Stack and the graphics decision gate

Use C++20 for the engine and portable model; SDL3 for platform/window/event and selected audio integration; bgfx as the first render candidate; Svelte, TypeScript and Vite for a static web editor; FFmpeg plus platform/HAP decode paths; libossia as the initial OSC/OSCQuery adapter candidate; SQLite for authored state; CMake/Ninja or native Visual Studio generators and pinned dependency manifests for builds.

M01 decides the renderer with real evidence: imported/owned D3D11 textures, native source/output sharing, GPU synchronization, multiple output windows, hardware-decoder surface import, resize/reconnect, worker lifecycle and measured copies/latency. Reject any option that requires maintaining a large private fork or cannot meet critical interop/recovery contracts. A bounded native D3D11 renderer behind the same small interface is the fallback. Domain and public protocols remain unchanged. Establish future Metal/Vulkan/GLES import/presentation requirements in the ADR without implementing speculative backends.

Ship adapter capabilities, not platform assumptions. Windows ARM64 must use native-compatible dependencies; x64 process emulation is separate evidence. Linux uses PipeWire direct video streams for frame sharing, with DMA-BUF negotiation and bounded fallback. macOS uses native Syphon/Metal paths. Vulkan/GLES support and codec acceleration on Pi are detected and qualified per board/driver rather than inferred from the ARM CPU.

## Rendering performance and timing

Render/audio deadline paths contain no filesystem/network work, probing, process launch, JSON serialization, runtime shader compilation, synchronous CPU readback or unbounded allocation/locking. Prepare resources and compile snapshots asynchronously; apply validated changes at a known frame boundary. Use ring/pool allocation, lifetime tracking, explicit GPU completion and bounded queues. Deadline-sensitive code may skip a late optional preview rather than block program output.

The graph culls unused branches, caches static results and shares exact matching instance outputs. Texture resolution follows use/quality policy; do not allocate every source at the maximum projector size. Preloading, active transitions and retained outgoing frames count against capacity. Per-GPU ownership and cross-GPU transfers must be visible when later scaling is introduced.

Distinguish CPU copies, GPU copies, uploads, readbacks, decode buffers and network encodes in metrics. A 3840x2160 RGBA8 frame is 33,177,600 bytes; at 60 fps one sequential transfer approaches 1.99 GB/s. GPU-resident composition is a practical goal, while universal zero-copy across all codecs/SDKs/platforms is not a valid promise.

Use a monotonic show clock with rational/int64 media timestamps and explicit clock-domain conversion. PTS selection handles VFR, B-frame reordering, loops, seek discontinuities and rate changes. Audio is a first-class timing consumer. Producer timestamps, host arrival time and presentation time are separate fields. Late/repeated/missing frames have explicit policy and counters. A software-coherent FrameSet is not proof of physically genlocked projectors.

Define color and alpha from the first compositor: source transfer/colorspace/range, working-space precision, premultiplied alpha, linear-light blend operations and output conversion. Use a qualified SDR path first, with high-precision intermediate formats where useful and supported. HDR is a separately declared capability. Preview encoding must be labeled and must not redefine calibrated program output.

## Media ingestion playback and scrubbing

Import uses background probing, metadata/thumbnails, cancellation and incremental hashing; it never blocks the render worker. Keep relative paths when possible, offline placeholders, relink suggestions, folder/tag/search organization and usage inspection. Large remote imports are resumable and verified before becoming ready. An asset can remain registered while unavailable.

Use FFmpeg demuxing/decoding for compatibility, with dedicated hardware surface and HAP paths. Report codec/profile/bit-depth/alpha, actual decoder, hardware/software status, decoded format and copy path. Qualify H.264/H.265, ProRes 422/4444, HAP/Alpha/Q/Q Alpha and supported newer variants, DXV variants, DNxHD/HR, CineForm, MJPEG, AV1 and common image/sequence formats. The corpus owns precise variants; importing a format does not imply fast random access or accelerated playback.

Long-GOP seeking starts at a valid keyframe and decodes forward. Indexed PTS/frame lookup, generation-tagged cancellable requests, nearby/reverse caches and optional show proxies keep dragging responsive. After dragging stops, resolve the requested original frame accurately. Preserve proxy/original timestamp mapping and test VFR/discontinuous media. A late old request must never replace a newer requested frame.

HAP should reach compressed GPU textures where supported. Query texture capabilities and provide an explicit slower decode/conversion path or target-specific proxy otherwise. DXV compatibility and equivalent-to-specialized-player performance are separate gates; budget a specialized path or a documented conversion workflow, and keep unfulfilled requirements visible.

Maintain a small redistributable synthetic media corpus plus optional licensed corpus manifests. Store exact hashes and expected PTS/color/alpha results. Keep large/user media out of source control; provide reproducible generation/fetch instructions and license provenance.

## Control and managed application protocols

See [docs/PROTOCOL_CONTRACTS.md](docs/PROTOCOL_CONTRACTS.md). Every application/domain parameter and structural command registers through one model. Types, defaults, ranges, units, access, persistence and descriptions drive UI inspectors, serialization metadata, OSCQuery and generated TypeScript contracts. Telemetry is exposed read-only. Session/UI state can live in an explicit separate namespace.

Use canonical stable-ID addresses and optional human-readable aliases. Domain commands validate finite numeric values, bounds, entity references and transaction invariants. Reliable command envelopes carry request IDs, expected revision and scheduled time when applicable. Accepted, prepared, applied and rejected outcomes are distinct. Duplicate reliable requests do not duplicate process launches or cue actions.

OSC UDP remains useful for interoperable controls. OSCQuery supplies the discoverable tree and supported extensions. A sequence of UDP messages is not automatically reliable/atomic; expose explicit grouped actions/transactions and a reliable remote command path. Resubscribe/reload structure after reconnect according to the actual OSCQuery semantics. High-rate telemetry is rate-limited and coalesced outside the render path.

Managed apps publish protocol version, package/app/instance/generation identity, OSCQuery endpoint, semantic output ports, transport descriptors, formats/color/alpha, readiness, health and lifecycle/multi-instance capabilities. Track launched processes by owned handles and attach external processes without assuming ownership. A connected parameter server is not proof that frames are ready.

A cooperating producer publishes a coherent named FrameSet with completion synchronization and common frame identity. Define texture ring lifetime and when reuse is allowed. Legacy unrelated senders have latest-per-port or complete-set-hold behavior and cannot claim atomic multi-texture capture without the necessary producer support.

Remote access defaults to local binding with explicit authenticated LAN enablement and pairing/role settings. Do not derive launchable executable paths from an untrusted discovered peer. These controls are part of a remotely controllable application, not a substitute for a usable local workflow.

## Editor behavior and development feedback

Use Library, Composition, Cues and Stage/Outputs as principal work areas with contextual inspectors. Prefer custom spatial editors for composition/mapping and schema-driven controls for ordinary parameters. Selection, focus, modifier keys, multi-selection, snapping, alignment, handles, numeric editing, keyboard nudging, undo and breadcrumbs must behave consistently.

Render live engine previews to the browser; overlay handles locally using known transform/revision/time metadata. Browser animation smoothness must not depend on waiting for an encoded frame. Remote preview latency/quality is measured separately from program output. Keep one edit owner plus read-only observers initially; resolve conflicting revisions explicitly.

Vite enables UI development updates while the native engine continues. Package static assets for production; no production dependency on Vite or a Node server. Shader development compiles asynchronously, caches pipelines and retains the last valid version on compilation errors. Production show edits can be staged/applied deliberately; do not add global native-code hot reload.

The design acceptance review uses actual tasks, screenshots and short recordings: first single-surface show, library reuse, nested editing, group cue launch, mapping and source failure. Avoid placeholder graph tools or generic dashboards replacing the core composition workflow.

## Mapping recovery and persistence

Maintain source pixels, composition coordinates, surface UV/geometry and projector-pixel spaces explicitly, with optional future world geometry. Start with accurate homography and four corners. Add editable meshes/Bezier control, masks, blend regions, test patterns, projector correction and versioned calibration profiles. Camera-assisted solvers later produce those same profiles.

An output's persistent identity and desired configuration survive the disappearance of its OS object. Match using several signals with confidence, debounce topology changes, rebuild only the affected presentation path where possible and keep missing outputs configured. If identical devices or changed paths make matching ambiguous, keep the target offline and expose a deliberate operator rebind. Never blindly use display enumeration index or move output onto the control screen.

Use SQLite transactional authored state with a consistent backup/snapshot API for Save As and export. Do not copy only the main database while a WAL contains newer data. Separate transient playhead/telemetry from authored state and explicit runtime resume checkpoints. Store media references and hashes, not GPU handles. Back up before migrations, reject incompatible future schemas without modification and retain the last valid saved project after disk-full/interrupted writes. Qualify local storage first; network-filesystem locking needs a separate support decision.

A watcher detects worker liveness and deadline loss, limits restart loops and reports recovery time. Per-source fallback, output blackout/freeze and show-clock policy are explicit. GPU-driver/hardware failure can interrupt several outputs; software isolation cannot guarantee uninterrupted optical output. Backup nodes and controlled switching are a later availability feature.

## Repository layout and build contracts

Create only modules justified by the current milestone. The intended layout is `apps/engine`, `apps/supervisor`, `libs/core`, `libs/domain`, `libs/control`, `libs/runtime`, `libs/render`, `libs/media`, `libs/mapping`, `libs/persistence`, platform/transport `adapters`, `ui`, `schemas`, `tests`, `benchmarks`, `packaging`, `tools` and `docs`. Folders are boundaries, not an instruction to populate empty frameworks.

M00 owns documented CMake presets and CI wrappers. Keep warning policies on project targets, not third-party sources. Use an explicit compiler/toolchain minimum verified by M00, pin dependency revisions/features, and keep generated outputs out of source unless the chosen toolchain requires committed artifacts. No floating Git branches or ad-hoc installed codec packs determine a build.

Provide diagnostic CLI output, structured errors, trace correlation IDs and version/capability reporting before deep UI work. Trace frame stages and queue/resource budgets with low-overhead instrumentation; detailed logs must not allocate or block in deadline paths.

## CI milestone commits and evidence

See [docs/CI_AND_RELEASE.md](docs/CI_AND_RELEASE.md) and [docs/ACCEPTANCE.md](docs/ACCEPTANCE.md). Hosted CI checks source/build/protocol/domain/UI behavior. Actual GPUs, external applications, physical outputs, projector reconnect and long-run capacity require distinct lab evidence. WARP/software rendering helps correctness; it cannot certify hardware sharing or performance.

Every milestone has four proposed commit slices below. Keep each commit coherent and validated; adjust size when discoveries justify it, retaining the ID mapping. A completion checkpoint records actual commands, source SHAs, CI run URLs, hardware profiles, acceptance results and open gaps. Tags point to final green commits. An implementation task can continue across milestones when authorized; routine commits do not require repeated confirmation.

The Windows release baseline is a clean install that works offline after installation, embeds the static editor, reports missing optional adapters, includes notices/build/source materials, and survives the qualified recovery suite. Do not publish a broad AAA or codec-support claim from one successful sample file.

## Milestone roadmap

| Milestone | Track | Prerequisites | Result |
| --- | --- | --- | --- |
| M00 Repository and CI foundation | windows-v1 | None | Make a clean checkout build and diagnose itself on Windows x64; establish portable core builds and honest completion evidence. |
| M01 Graphics and sharing feasibility gate | windows-v1 | M00 | Prove the graphics abstraction, native frame sharing, presentation, and resource ownership before product architecture depends on them. |
| M02 Domain model and parameter registry | windows-v1 | M01 | Define assets, instances, layers, compositions, cues, arbitrary surfaces and routing without GPU or UI object ownership. |
| M03 Control service and remote contract | windows-v1 | M02 | Make the engine controllable and inspectable before building the full editor, including reliable structural commands and OSCQuery. |
| M04 Compositing and first projector output | windows-v1 | M03 | Deliver the first useful mapping path: a default composition, layers, one or several named outputs, simple surfaces and real output windows. |
| M05 Media library and baseline playback | windows-v1 | M04 | Import and organize real content while keeping decoding and filesystem work off deadline-sensitive threads. |
| M06 Composition editor and remote workflow | windows-v1 | M05 | Make common editing actions fast, discoverable and consistent across the local and remote browser. |
| M07 Cues routing and transition bank | windows-v1 | M06 | Make launching simple or multi-output content one operation, with predictable preparation, timing and transitions. |
| M08 Managed apps and coherent multi-texture frames | windows-v1 | M07 | Deliver an explicit client protocol for discovery, process ownership, parameter control and named frame sets. |
| M09 Optimized codecs and accurate scrubbing | windows-v1 | M07, M08 | Meet the defined playback/scrubbing profiles and report the real decode path for every source. |
| M10 Network input and output integrations | windows-v1 | M08, M09 | Complete Spout, OMT and NDI send/receive paths with isolated queues, measured latency and explicit licensing decisions. |
| M11 Curved surfaces and advanced manual mapping | windows-v1 | M06, M07 | Extend simple quad mapping to arbitrary connected geometry while keeping content layout separate from projector calibration. |
| M12 Recovery persistence and long-run stability | windows-v1 | M09, M10, M11 | Qualify failure behavior already introduced in earlier milestones and make recovery observable and repeatable. |
| M13 Windows beta release qualification | windows-v1 | M12 | Ship an installable Windows x64 build whose supported behavior and limitations are backed by reproducible evidence. |
| M14 Native Windows ARM64 port | ports | M13 | Run the same Windows application natively on ARM64 and qualify adapters independently from x64 emulation. |
| M15 Linux x64 and PipeWire port | ports | M13 | Deliver native Linux output and frame sharing using the same project, command and composition contracts. |
| M16 macOS ARM64 and Syphon port | ports | M13 | Deliver a native Metal/Syphon implementation without changing the show model or remote contracts. |
| M17 Linux ARM64 and Raspberry Pi profile | ports | M15 | Run bounded installations and render-node workloads on qualified ARM64 boards with an honest hardware capability profile. |
| M18 Camera-assisted calibration extension | future | M13, M11 | Add automated calibration through the existing calibration-data interface without changing composition or cue semantics. |
| M19 Multiple GPUs and distributed render nodes | future | M13 | Scale execution placement and coordinated presentation while preserving project identity and deterministic show actions. |

## Milestone implementation and commit specifications

All items below are planned. Implementation evidence belongs in `docs/progress/Mxx.md`. Acceptance IDs resolve in [docs/ACCEPTANCE.md](docs/ACCEPTANCE.md).

### M00 Repository and CI foundation

Make a clean checkout build and diagnose itself on Windows x64; establish portable core builds and honest completion evidence.

Prerequisites: none. Primary paths: `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `vcpkg-configuration.json`, `apps/engine`, `libs/core`, `tools/ci`, `.github/workflows`, `docs/progress`.

Acceptance gates: ACC-030, ACC-031, ACC-032. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M00.C01** — `build(m00): establish pinned C++20 and dependency manifests`. Add MSVC x64 build, portable C++ core target, explicit feature options, dependency revisions and SPDX inventory; no floating dependencies or global -march=native.
- **M00.C02** — `feat(m00): add diagnostic CLI and portable test entry points`. Implement --version and --diagnose --json --no-gpu using actual build/platform data; establish CTest with a meaningful CLI/schema smoke contract and nonzero failure exits.
- **M00.C03** — `ci(m00): gate Windows builds and portable core checks`. Activate adapted CI templates; Debug/Release Windows build and CTest, Linux/macOS portable core smoke, plan validation, artifact logs and one stable quality-gate.
- **M00.C04** — `docs(m00): record reproducible bootstrap and milestone evidence`. Verify commands from a fresh checkout, record runner/toolchain versions and real CI URLs, create progress evidence and completion checkpoint after all required checks pass.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M01 Graphics and sharing feasibility gate

Prove the graphics abstraction, native frame sharing, presentation, and resource ownership before product architecture depends on them.

Prerequisites: M00. Primary paths: `experiments/graphics`, `libs/render`, `adapters/windows`, `tests/hardware`, `docs/adr`.

Acceptance gates: ACC-009, ACC-010, ACC-026. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M01.C01** — `feat(m01): render native D3D11 textures through the candidate backend`. Exercise bgfx D3D11 with a minimal internal render interface; verify owned and imported textures, formats, fences and resource lifetime without UI dependencies.
- **M01.C02** — `feat(m01): prove Spout receive and send interoperability`. Use real external sender/receiver processes, repeated resolution changes, producer restarts and stalled frames; count CPU readbacks and GPU copies separately.
- **M01.C03** — `test(m01): characterize multi-output and decoder texture paths`. Run physical two-output tests plus D3D11 hardware decode/import spike, paired-output synchronization and failure injection; produce machine-readable frame and memory metrics.
- **M01.C04** — `docs(m01): select the rendering backend from measured evidence`. Accept bgfx only if all critical interop/recovery gates pass without a private long-lived fork; otherwise record a bounded native-D3D11 implementation behind the same interface. Fix reference machines and initial performance thresholds.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M02 Domain model and parameter registry

Define assets, instances, layers, compositions, cues, arbitrary surfaces and routing without GPU or UI object ownership.

Prerequisites: M01. Primary paths: `libs/domain`, `libs/control`, `schemas`, `tests/domain`, `docs/adr`.

Acceptance gates: ACC-002, ACC-006, ACC-007, ACC-011, ACC-028. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M02.C01** — `feat(m02): model stable assets instances and multi-output compositions`. Use stable IDs and typed ports; independent placements get independent transports by default; explicit sharing reuses one live instance; reject dependency cycles.
- **M02.C02** — `feat(m02): model surface groups layouts and projection patches`. Support arbitrary surface counts, optional adjacency/seams, independent or continuous content layout and many-to-many surface/projector patches; no hard-coded wall/floor concepts.
- **M02.C03** — `feat(m02): add typed parameter registry and command transactions`. One registration path supplies validation, access modes, units, ranges, persistence metadata and API descriptors; commands cover structural edits and parameter changes with undo/redo.
- **M02.C04** — `test(m02): prove model invariants and capability-preserving round trips`. Verify stable addressing across rename/reorder, reference scope, source instance independence, transactional rollback, unsupported-adapter placeholders and cyclic-graph rejection.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M03 Control service and remote contract

Make the engine controllable and inspectable before building the full editor, including reliable structural commands and OSCQuery.

Prerequisites: M02. Primary paths: `apps/engine`, `libs/control`, `libs/protocol`, `adapters/osc`, `ui`, `schemas`, `tests/protocol`.

Acceptance gates: ACC-008, ACC-011, ACC-012, ACC-020, ACC-034. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M03.C01** — `feat(m03): expose versioned commands snapshots and subscriptions`. Implement authoritative control state, command IDs, revisions, accepted/applied/error responses, structural transactions and resumable full-snapshot recovery; use local named-pipe IPC plus documented messages.
- **M03.C02** — `feat(m03): expose complete OSC and OSCQuery trees`. Adapt libossia or a justified replacement; serve HOST_INFO and advertised extensions, exact-path LISTEN and structure updates; normalize network writes through the command validator.
- **M03.C03** — `feat(m03): add local web server and Svelte editor shell`. Serve packaged static UI, run Vite separately in development, generate TypeScript contract types, show real engine state and reconnect diagnostics; add a minimal encoded preview endpoint.
- **M03.C04** — `test(m03): verify control parity reconnection and bounded traffic`. Exercise create/edit/delete/save commands through remote APIs and OSC methods, duplicate command deduplication, conflicting revisions, subscriptions after reconnect, slow-client backpressure and malformed messages.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M04 Compositing and first projector output

Deliver the first useful mapping path: a default composition, layers, one or several named outputs, simple surfaces and real output windows.

Prerequisites: M03. Primary paths: `libs/render`, `libs/runtime`, `adapters/windows`, `tests/render`, `tests/hardware`.

Acceptance gates: ACC-001, ACC-003, ACC-005, ACC-010, ACC-017, ACC-021. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M04.C01** — `feat(m04): compile composition graphs into immutable render snapshots`. Render images/test patterns and nested compositions; cache reusable work, cull inactive branches and allocate transient textures from bounded pools.
- **M04.C02** — `feat(m04): present named outputs on persistent logical targets`. Separate desired output identity/configuration from detected monitors; create independent swapchains, output enable/blackout/freeze controls and a native output path independent of the browser.
- **M04.C03** — `feat(m04): map surfaces with quad patches masks and color rules`. Implement four-corner homography, crop/fit/fill, simple masks, compositing blend modes, consistent premultiplied alpha and defined input/output transfer functions.
- **M04.C04** — `test(m04): verify default mapping multi-output composition and UI loss`. Run one-surface golden path, one composition over three surface regions, one surface across two projector patches and several surfaces on one projector; kill/reload UI and compare outputs within defined color tolerances.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M05 Media library and baseline playback

Import and organize real content while keeping decoding and filesystem work off deadline-sensitive threads.

Prerequisites: M04. Primary paths: `libs/media`, `libs/assets`, `workers/media`, `ui/library`, `tests/media`.

Acceptance gates: ACC-006, ACC-007, ACC-015, ACC-018. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M05.C01** — `feat(m05): import organized assets with background probing`. Add folders/tags/search, drag import, thumbnails, metadata, relative paths, incremental hashes, offline placeholders, usage inspection and cancellable background jobs.
- **M05.C02** — `feat(m05): decode baseline video on the shared show clock`. Integrate pinned FFmpeg libraries, rational timestamps, video/audio synchronization, bounded decoded-frame queues, loops, play/pause/stop and explicit late-frame policies.
- **M05.C03** — `feat(m05): separate source definitions from playback instances`. Create independent clip transports from a shared asset, implement explicit shared instances, manage source/preload reference counts and expose real readiness/capacity state.
- **M05.C04** — `test(m05): verify playback timing import cancellation and relinking`. Use generated and legally redistributable CFR/VFR/audio/alpha/missing/corrupt fixtures, loop boundaries, independent playheads, shared-instance semantics and large-file paths.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M06 Composition editor and remote workflow

Make common editing actions fast, discoverable and consistent across the local and remote browser.

Prerequisites: M05. Primary paths: `ui`, `libs/protocol`, `tests/e2e`, `docs/ux`.

Acceptance gates: ACC-001, ACC-006, ACC-020, ACC-034. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M06.C01** — `feat(m06): implement composition canvas and natural transform tools`. Add accurate selection, multiselect, gizmos, pivot/scale/rotate, snapping, alignment, keyboard nudging, zoom/pan and contextual inspectors with consistent undo.
- **M06.C02** — `feat(m06): make nested ownership and asset reuse visible`. Add hierarchy/breadcrumbs, reveal-in-library, find-usages, make-independent and promote-to-reusable actions; inline instances remain reachable from the hierarchy.
- **M06.C03** — `feat(m06): complete remote preview and media import workflow`. Use engine-authoritative previews with timestamps/revisions and client-rendered overlays; support resumable uploads, engine-side file browsing, large imports and single-writer edit ownership.
- **M06.C04** — `test(m06): qualify editing interactions and live development updates`. Playwright covers drag/drop, handles, shortcuts, focus/text-input rules, undo, reconnect and upload interruption; record a short interaction review on the real composition canvas and verify Vite reload does not stop output.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M07 Cues routing and transition bank

Make launching simple or multi-output content one operation, with predictable preparation, timing and transitions.

Prerequisites: M06. Primary paths: `libs/show`, `libs/runtime`, `ui/cues`, `assets/transitions`, `tests/show`.

Acceptance gates: ACC-002, ACC-003, ACC-009, ACC-019, ACC-033. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M07.C01** — `feat(m07): add cue assignments and reusable routing presets`. Support Replace/Keep/Clear per destination, drag-to-create cues, semantic port matching with persisted overrides, independent surface actions and arbitrary groups.
- **M07.C02** — `feat(m07): stage prepare and atomically apply ready cues`. Prepare all required resources before GO, schedule one show-clock boundary, report accepted versus applied frame, enforce timeout/fallback policy and define re-trigger/interruption behavior.
- **M07.C03** — `feat(m07): add a curated editable shader transition bank`. Start with cut, dissolve, dip, wipe, luma and selected adapted shaders; version manifests, defaults, color/alpha behavior, license/attribution and deterministic seeds; compile off the render thread.
- **M07.C04** — `test(m07): verify grouped transitions partial changes and instance conflicts`. Validate one/three/curved surface sets, untouched destinations, synchronized clips, transition peak memory, parameter-state crossfades requiring duplicate app instances or frozen outgoing frames, and repeated GO deduplication.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M08 Managed apps and coherent multi-texture frames

Deliver an explicit client protocol for discovery, process ownership, parameter control and named frame sets.

Prerequisites: M07. Primary paths: `libs/clients`, `adapters/spout`, `protocol/client`, `examples/client`, `ui/clients`, `tests/clients`.

Acceptance gates: ACC-008, ACC-009, ACC-013, ACC-014. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M08.C01** — `feat(m08): publish versioned app and output manifests`. Define app/package/instance/generation IDs, semantic output roles, dimensions, color/alpha, transport endpoints and supported lifecycle/control capabilities; ship a small reference client.
- **M08.C02** — `feat(m08): supervise launched and attached application instances`. Track real process handles, working directory and controlled environment, readiness beyond OSC connection, ownership, reference counts, graceful shutdown, crash-loop limits and restart generations.
- **M08.C03** — `feat(m08): publish and consume synchronized named frame sets`. Specify resource lifetime, publication order/fences, common frame identity and generation changes; support complete-set hold versus latest-per-port modes without claiming atomic capture from unrelated legacy senders.
- **M08.C04** — `test(m08): exercise discovery readiness source failure and routing`. Use one-output and three-output reference apps, missing/stalled ports, resolution changes, multiple app instances, old/new generation races, parameter mirroring without echo loops and reconnect after process death.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M09 Optimized codecs and accurate scrubbing

Meet the defined playback/scrubbing profiles and report the real decode path for every source.

Prerequisites: M07, M08. Primary paths: `libs/media`, `adapters/decoders`, `workers/media`, `ui/media`, `tests/media`, `benchmarks`.

Acceptance gates: ACC-015, ACC-016, ACC-017, ACC-018, ACC-026. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M09.C01** — `perf(m09): implement hardware decode and HAP texture paths`. Add D3D11 hardware decode with supported format/profile queries, GPU color conversion and explicit fallback; use HAP reference decoding into compressed GPU textures with correct Q/alpha variants.
- **M09.C02** — `feat(m09): qualify ProRes DXV and professional format compatibility`. Exercise ProRes 422/4444, DXV variants, DNxHD/HR, CineForm, MJPEG, AV1 and common still/sequence formats; record import/playback/alpha/bit-depth/accelerated/scrub distinctions per fixture.
- **M09.C03** — `perf(m09): add indexed cancellable scrubbing and show proxies`. Use keyframe/PTS indices, generation-tagged requests, nearby/reverse caches and proxy-to-original mapping; settle accurately after dragging without stale seeks replacing newer frames.
- **M09.C04** — `test(m09): qualify codec workloads and disclose unsupported variants`. Run complete media corpus, exact settled-frame tests, rapid seek/reverse/loop cases and warm/cold throughput; optimize or document a bounded transcode path for DXV performance gaps, with unmet requirements still open.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M10 Network input and output integrations

Complete Spout, OMT and NDI send/receive paths with isolated queues, measured latency and explicit licensing decisions.

Prerequisites: M08, M09. Primary paths: `adapters/spout`, `adapters/omt`, `adapters/ndi`, `ui/io`, `tests/integrations`, `docs/licenses`.

Acceptance gates: ACC-008, ACC-010, ACC-015, ACC-028, ACC-030. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M10.C01** — `feat(m10): harden bidirectional Spout as a production adapter`. Finish sender/receiver selection, duplicate names, sender loss/reappearance, resolution/format changes, per-GPU affinity and persistent logical endpoint identity.
- **M10.C02** — `feat(m10): integrate OMT discovery receiving and output`. Build pinned official dependencies, negotiate frame/audio/color formats and timestamps, measure encode/decode cost and provide bounded queue/drop/recovery policies.
- **M10.C03** — `feat(m10): add the approved NDI integration and distribution path`. Resolve current SDK terms and application exception/vendor-permission requirements before distributing combined binaries; implement supported receive/send modes and package only the approved configuration. Keep this commit blocked if licensing is unresolved.
- **M10.C04** — `test(m10): qualify transport round trips and slow consumers`. Test each direction using independent peers, absent runtimes, bandwidth pressure, stalled receivers, rediscovery and alpha/color correctness; publish a tested capability matrix and dependency notices.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M11 Curved surfaces and advanced manual mapping

Extend simple quad mapping to arbitrary connected geometry while keeping content layout separate from projector calibration.

Prerequisites: M06, M07. Primary paths: `libs/mapping`, `libs/geometry`, `ui/stage`, `tests/mapping`.

Acceptance gates: ACC-002, ACC-003, ACC-004, ACC-005, ACC-017. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M11.C01** — `feat(m11): add mesh and Bezier surface editing`. Implement versioned tessellated geometry and UV charts with deterministic triangulation, selectable control points, constraints, numeric editing and reversibly refined detail.
- **M11.C02** — `feat(m11): add surface adjacency seams and continuous layouts`. Represent shared-edge correspondence and optional canvas continuity; allow local content to coexist with spanning content and reject invalid geometry with actionable diagnostics.
- **M11.C03** — `feat(m11): add projector blend masks and color correction`. Introduce overlap masks, blend/gamma profiles, per-projector color/LUT controls, test patterns and non-destructive calibration profiles independent of cues.
- **M11.C04** — `test(m11): verify curved multi-projector mapping and seam behavior`. Compare reference patterns on a curved mesh, three connected walls, a UV seam and overlapping projector patches; verify save/reopen and content replacement retain calibration exactly.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M12 Recovery persistence and long-run stability

Qualify failure behavior already introduced in earlier milestones and make recovery observable and repeatable.

Prerequisites: M09, M10, M11. Primary paths: `libs/runtime`, `libs/persistence`, `adapters/windows`, `apps/supervisor`, `tests/faults`, `tools/qualification`.

Acceptance gates: ACC-014, ACC-021, ACC-022, ACC-023, ACC-024, ACC-025, ACC-027. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M12.C01** — `feat(m12): recover logical outputs through display topology changes`. Match OS device paths/EDID/connector/GPU signals with confidence, debounce events, rebuild affected sinks, retain absent targets and never silently bind an ambiguous output to an operator display.
- **M12.C02** — `feat(m12): add transactional project persistence and crash recovery`. Use SQLite snapshots/journal for authored state, separate transient telemetry, explicit checkpoint/dirty state and atomic save-as/portable export; preserve relative media paths and migrate with backups.
- **M12.C03** — `feat(m12): supervise engine workers and contain failures`. Detect heartbeat/deadline failure, recreate render/device resources with bounded attempts, keep UI/control responsive and apply configured fallback/blackout; maintain last applied runtime revision and resumable source state.
- **M12.C04** — `test(m12): qualify power loss hotplug faults and soak behavior`. Run interrupted writes, migration failures, killed UI/controller/source/render workers, GPU device loss, 100 topology cycles, decoder corruption and a 72-hour qualified workload with resource/queue budgets.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M13 Windows beta release qualification

Ship an installable Windows x64 build whose supported behavior and limitations are backed by reproducible evidence.

Prerequisites: M12. Primary paths: `packaging/windows`, `docs/user`, `docs/releases`, `tools/qualification`, `.github/workflows`.

Acceptance gates: ACC-001, ACC-015, ACC-020, ACC-026, ACC-027, ACC-030, ACC-031. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M13.C01** — `build(m13): package standalone Windows binaries and dependencies`. Produce versioned ZIP/installable package, default configuration, embedded UI, dependency notices, corresponding-source/build materials and checksums; avoid undocumented runtime prerequisites.
- **M13.C02** — `test(m13): verify clean installation offline operation and upgrade`. Test non-developer machine, Unicode/long paths, no network access after installation, firewall/pairing behavior, project upgrade/rollback and runtime absence diagnostics.
- **M13.C03** — `perf(m13): certify reference workloads and resource budgets`. Publish exact source revision, hardware/driver/OS/media hashes, frame timings, seek latencies, capacity limits, network tests and recovery/soak results; unresolved mandatory Windows requirements block full beta qualification.
- **M13.C04** — `docs(m13): close Windows beta gates and prepare release artifacts`. Complete requirement traceability, operator guide, known limitations and changelog; tag only the final green commit. Build candidate artifacts; publish or push only within the active task authorization.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M14 Native Windows ARM64 port

Run the same Windows application natively on ARM64 and qualify adapters independently from x64 emulation.

Prerequisites: M13. Primary paths: `adapters/windows`, `cmake`, `packaging/windows`, `tests/hardware`, `.github/workflows`.

Acceptance gates: ACC-008, ACC-015, ACC-028, ACC-029, ACC-031. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M14.C01** — `build(m14): add native ARM64 dependency and toolchain builds`. Use current native Windows ARM runner/toolset, audit SIMD, calling conventions and third-party libraries; do not load x64 DLLs into an ARM64 process.
- **M14.C02** — `feat(m14): qualify native rendering decode and sharing capabilities`. Run D3D rendering and decode corpus on real ARM hardware; build/port Spout and OMT dependencies where supported and expose missing capabilities without losing project state.
- **M14.C03** — `test(m14): verify ARM64 playback recovery and package parity`. Run editor/control/mapping tests and physical-output/fault suite natively, with architecture identity captured in evidence and x64-emulation results labeled separately.
- **M14.C04** — `docs(m14): publish the tested Windows ARM64 feature matrix`. Add ARM64 artifacts, performance profiles and tested differences; keep unsupported adapters explicit and retain unfulfilled product requirements in the backlog.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M15 Linux x64 and PipeWire port

Deliver native Linux output and frame sharing using the same project, command and composition contracts.

Prerequisites: M13. Primary paths: `adapters/linux`, `adapters/pipewire`, `packaging/linux`, `tests/hardware`, `.github/workflows`.

Acceptance gates: ACC-008, ACC-015, ACC-022, ACC-028, ACC-032. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M15.C01** — `feat(m15): implement Linux presentation and decoder adapters`. Qualify Vulkan/GLES choices, Wayland/X11 installation behavior, GPU affinity and selected hardware-decoder paths; document kiosk/direct DRM/KMS as a distinct deployment mode where implemented.
- **M15.C02** — `feat(m15): integrate PipeWire receiving and publishing`. Expose direct app video streams, stable logical identities, format/modifier negotiation, synchronization and DMA-BUF import/export with bounded shared-memory fallback.
- **M15.C03** — `test(m15): qualify Linux topology and session recovery`. Test compositor/display changes, source reconnect, missing PipeWire/session services, multi-output placement and remote-only operation on fixed distro/driver profiles.
- **M15.C04** — `build(m15): package and qualify the Linux x64 release`. Run project round-trip and core parity suite, approved transport runtimes, clean installation and exact capability/feature matrix; publish source/build and dependency metadata.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M16 macOS ARM64 and Syphon port

Deliver a native Metal/Syphon implementation without changing the show model or remote contracts.

Prerequisites: M13. Primary paths: `adapters/macos`, `adapters/syphon`, `packaging/macos`, `tests/hardware`, `.github/workflows`.

Acceptance gates: ACC-008, ACC-015, ACC-022, ACC-028, ACC-032. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M16.C01** — `feat(m16): implement Metal presentation and VideoToolbox paths`. Integrate native resource import, color conversions, fullscreen outputs and timestamped decoding through the chosen renderer with explicit ownership and synchronization.
- **M16.C02** — `feat(m16): add bidirectional Syphon and managed-app lifecycle`. Expose named outputs through IOSurface/Metal-compatible paths, handle sender discovery/generation changes and implement macOS process/session behavior.
- **M16.C03** — `test(m16): qualify display reconnection media and UI independence`. Exercise physical display removal, app failure, multi-output frame sets, media corpus, project interchange, browser loss and native hardware identity.
- **M16.C04** — `build(m16): package and qualify the macOS ARM64 release`. Bundle required open dependencies and UI, define signing/notarization deployment procedure as available, run clean-machine tests and publish a capability/performance matrix.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M17 Linux ARM64 and Raspberry Pi profile

Run bounded installations and render-node workloads on qualified ARM64 boards with an honest hardware capability profile.

Prerequisites: M15. Primary paths: `adapters/linux`, `cmake`, `packaging/linux`, `tools/qualification`, `tests/hardware`.

Acceptance gates: ACC-008, ACC-015, ACC-026, ACC-028, ACC-029. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M17.C01** — `build(m17): add reproducible Linux ARM64 builds and board profiles`. Select named Pi/OS/driver/storage/thermal configurations; compile dependencies natively or cross-build with a pinned sysroot and keep ARM32 armhf as a separate optional target.
- **M17.C02** — `feat(m17): select board-appropriate decode shader and texture paths`. Query GPU compressed formats and decode capabilities, validate V4L2/platform paths where available, use precompiled effect variants and allow remote editor operation.
- **M17.C03** — `test(m17): qualify thermal storage network and output budgets`. Run sustained playback/seek/mapping/PipeWire/OMT cases on real boards, test throttling and reconnect, measure memory and preview encoding cost and avoid claiming desktop throughput.
- **M17.C04** — `docs(m17): publish ARM64 limits and decide the armhf extension`. Document qualified workloads, conversion presets and adapter availability. Add a separately scoped armhf milestone only if needed; official OMT AOT support cannot be assumed on ARM32.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M18 Camera-assisted calibration extension

Add automated calibration through the existing calibration-data interface without changing composition or cue semantics.

Prerequisites: M13, M11. Primary paths: `libs/calibration`, `adapters/cameras`, `ui/calibration`, `tests/calibration`.

Acceptance gates: ACC-004, ACC-005, ACC-017. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M18.C01** — `feat(m18): define capture sessions and calibration observations`. Version camera/projector intrinsics, patterns, observations and confidence; preserve manual calibration and reproducible captures.
- **M18.C02** — `feat(m18): implement a bounded structured-light solver`. Start with a documented supported geometry/camera setup, robust correspondence and reprojection error; solver outputs normal versioned patches.
- **M18.C03** — `feat(m18): expose calibration review and refinement tools`. Provide operator confidence/error visualization, rejected points and reversible manual refinement; require intentional application of captured results.
- **M18.C04** — `test(m18): qualify calibration accuracy against physical references`. Record repeatability, residuals, invalid captures, moved devices and physical seam/color tests before expanding supported setup classes.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.

### M19 Multiple GPUs and distributed render nodes

Scale execution placement and coordinated presentation while preserving project identity and deterministic show actions.

Prerequisites: M13. Primary paths: `libs/distributed`, `libs/runtime`, `apps/node`, `adapters/sync`, `tests/cluster`.

Acceptance gates: ACC-002, ACC-009, ACC-026, ACC-027, ACC-028. In addition to these named scenarios, every commit below must deliver its stated outcome.

- **M19.C01** — `feat(m19): partition render work by GPU and node ownership`. Place source instances, compositions and patches explicitly; account for texture/asset movement and avoid hidden cross-GPU readback.
- **M19.C02** — `feat(m19): replicate versioned assets and scheduled show state`. Use content-verified local assets, prepare barriers, future show-clock execution, deterministic seeds and generation/revision reconciliation.
- **M19.C03** — `feat(m19): add node health synchronization and controlled failover`. Separate clock synchronization from physical scanout/genlock, expose drift/readiness and define backup/failure ownership without promising seamless recovery from all hardware faults.
- **M19.C04** — `test(m19): qualify cluster timing capacity and failure behavior`. Run measured multi-node latency/drift tests, network partitions, late preparation, node loss and supported hardware synchronization; publish supported installation profiles.

Completion evidence: actual source revisions, applicable build/test results, scenario outcomes and hardware/SDK evidence for this milestone. A missing required gate keeps the milestone open.


## First task

Install this pack in the intended repository and use [CODEX_START.md](CODEX_START.md) to implement M00. Then run M01 on Windows hardware before building the full editor around a rendering abstraction. The subsequent critical path is the domain/control contract, first native output, media, editor, cues/apps/codecs, integrations/mapping and recovery/release qualification.

Primary-source references, action revision pins and source dates are in [docs/SOURCES.md](docs/SOURCES.md). Defaults and unresolved decisions are in [docs/DECISIONS.md](docs/DECISIONS.md). The machine-readable backlog remains the source of IDs and dependency relationships.
