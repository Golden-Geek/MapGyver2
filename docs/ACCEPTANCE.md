# Acceptance and qualification

Scenarios are observable contracts. Every implementation milestone selects the relevant IDs; its commit outcomes remain additional acceptance requirements. Record pass/fail/not-applicable/blocked with reason, source SHA and evidence class. A blocked test is not a pass.

## Initial performance profiles

These are planning targets, not measured capabilities. M01 names the reference hardware and ratifies workload/budget values before later performance claims. Changes need an ADR with traces and product impact.

| Profile | Proposed workload | Intended qualification |
| --- | --- | --- |
| W0 functional | One 1080p60 output, one video/image and one live source, basic composition/mapping | Clean-machine golden path and behavior |
| W1 Windows show | Two independent 1080p60 physical outputs, eight active 1080p sources with declared codecs/live inputs, group transitions and preview | Initial committed performance/recovery workload after M01 ratification |
| W2 high-end stretch | Four 4K60 outputs with declared 4K source/effect/transition counts | Capacity exploration; not a guaranteed v1 claim until qualified |
| L0 large project | 10,000 registered assets, 1,000 logical layers and 100 surfaces, with active work constrained by the selected show profile | Library/model/editor scaling and demand-driven resource use |
| A0 ARM board | Named Pi/OS/storage/thermal profile, initially one 1080p output with board-appropriate show-ready media | Sustained capacity and capability reporting; no desktop parity claim |

For a 60 Hz reference show, initial targets are p99 GPU render work at or below 12 ms and p99 CPU frame preparation at or below 2 ms on the agreed profile, with independently measured presentation/queue behavior. These are overlapping-stage metrics, not a simplistic sum proving end-to-end latency. Fix thresholds at M01 and record outliers/missed deadlines. Warm steady-state runs distinguish source-late repeats from compositor/output failures; do not remove inconvenient samples silently.

Measure cold/warm import/preparation, GO-to-applied-frame, intra/long-GOP scrub p50/p95/p99, settled-frame correctness, audio drift, preview latency, GPU/CPU copy counts, queue high-water marks and transition peak RAM/VRAM. Ratify seek-latency targets by codec/profile rather than assigning one universal number.

A 72-hour run stays inside declared queue/cache/memory budgets and shows no continuing growth after documented warmup/cache fill. Capture allocation/resource counts and trends, scheduled faults, drops/repeats, process restarts and recovery intervals. Run 100 physical topology cycles for the supported reconnection profile; separately record simulated device-loss tests.

## Scenario catalogue

### ACC-001 One simple surface

Import an image/video, drop it on the default composition, adjust a quad and play; no manual routing/container setup.

### ACC-002 Arbitrary surface sets

Operate one, three and many surfaces through the same unbounded collection model; no special two-output branches.

### ACC-003 Continuous and independent content

Span one canvas over connected surfaces, then change one destination while others keep their content and transport.

### ACC-004 Curved geometry

Render calibrated content on a curved/mesh surface with a stable UV chart and reversible point edits.

### ACC-005 Many-to-many projector patches

One surface spans multiple projectors and one projector covers multiple surfaces without duplicating content ownership.

### ACC-006 Library and nested reuse

Organize, search, relink and inspect asset usages; inline/nested content remains reachable and reusable references remain explicit.

### ACC-007 Instance ownership

Independent placements have independent playheads; explicitly shared instances reuse one source and stop only when their lifecycle policy allows.

### ACC-008 Transport directions

Receive and publish supported local/network transports through equivalent frame/port contracts; absent adapters retain configuration.

### ACC-009 Coherent frame sets

A cooperating multi-output producer publishes common frame identity and completion; grouped routing never mixes resource generations.

### ACC-010 Native output independence

Program output remains native and active without a browser; slow or absent output consumers cannot create unbounded queues.

### ACC-011 Complete parameter exposure

Registry inventory and OSCQuery tree agree on every application parameter and structural command, including access modes and unavailable states.

### ACC-012 Reliable state changes

Validate, deduplicate and atomically apply commands; distinguish accepted/applied revisions and resubscribe/resnapshot after reconnect.

### ACC-013 Managed-app protocol

Discover, launch/attach, control parameters and bind named outputs for one-output and multi-output reference clients.

### ACC-014 App failure ownership

Only owned processes follow shutdown/restart policy; simulate delayed readiness, process death, port stalls and restart generations.

### ACC-015 Codec and format matrix

Use a versioned corpus covering H.264/H.265, ProRes, HAP variants, DXV variants and other relevant media with explicit bit-depth/alpha/decode-path results.

### ACC-016 Scrub accuracy

Rapid obsolete seeks cancel; settled requests select the correct PTS/frame, including long-GOP, VFR, reverse, proxy and loop cases.

### ACC-017 Color alpha and blending

Golden/reference renders verify colorspace/range/transfer, premultiplied alpha, transition endpoints, masks, blend seams and correction within declared tolerances.

### ACC-018 Show clock and audio

Independent sources, audio and cue changes use one explicit rational/timestamp model; loop, pause, seek and clock changes have defined behavior.

### ACC-019 Transition bank

Every shipped shader has a manifest, defaults, attribution, endpoint behavior, deterministic grouping and validated GPU variants.

### ACC-020 Local and remote editor

Same UI can inspect/edit through documented command contracts, upload/relink content and recover after loss of connection.

### ACC-021 Editor failure

Close/reload/crash the browser during active playback; output and show clock continue with no dependency on editor frame callbacks.

### ACC-022 Display reconnection

Repeat unplug/replug and topology changes; retain calibration and desired modes, restore confident matches and leave ambiguous matches offline.

### ACC-023 GPU and worker recovery

Inject device loss and worker hangs/death; record bounded retry, fallback behavior, lost-output interval and surviving control responsiveness.

### ACC-024 Save integrity

Interrupt save/export, exhaust disk space, lose referenced media and reopen; preserve the last valid authored project and report unsaved changes.

### ACC-025 Project migrations

Upgrade old fixtures, reject unsupported future formats without damage, back up before migration and verify no silent parameter/ID loss.

### ACC-026 Performance budgets

Measure fixed qualified workloads with machine/media hashes, warm/cold separation, latency percentiles, missed deadlines, RAM/VRAM and transition peaks.

### ACC-027 Long-run operation

Complete 72-hour qualified playback with scheduled faults and bounded memory/queue/resource use; classify all repeats/drops and recovery events.

### ACC-028 Capabilities and portability

Missing GPU features, runtimes and adapters are discoverable in UI/API and do not delete saved objects or silently change output.

### ACC-029 Native ARM identity

Compile and run native ARM builds and dependency probes; record architecture, SIMD choices and any separately labeled emulation results.

### ACC-030 GPL and dependency provenance

Public-source reproducibility, SPDX inventory, corresponding build/source materials and approved optional-runtime distribution; NDI approval remains a real gate.

### ACC-031 CI and evidence integrity

Required checks fail closed, never pass on zero tests, and associate reports with exact source revisions; software rendering is not labeled hardware qualification.

### ACC-032 Portable core builds

Windows/Linux/macOS build and test domain/control/persistence contracts without platform GPU/UI headers; native platform ports have separate gates.

### ACC-033 Cue ownership and routing

Replace/Keep/Clear semantics, group readiness and transition interruption preserve unaffected destinations; dual-state app transitions have an explicit resource strategy.

### ACC-034 Development feedback

Vite interface updates and asynchronous shader development can refresh previews while engine output continues; production builds contain no development server.

## Requirement traceability

| Requirement | Milestones | Acceptance |
| --- | --- | --- |
| R01 High performance and scaling | M01, M09, M12, M13, M19 | ACC-026, ACC-027 |
| R02 Elegant reactive editor and media explorer | M03, M05, M06 | ACC-001, ACC-006, ACC-020, ACC-034 |
| R03 Complete OSC and OSCQuery control | M02, M03, M08 | ACC-011, ACC-012 |
| R04 Simple to advanced projection mapping | M04, M11, M18 | ACC-004, ACC-005, ACC-017 |
| R05 Compositions and reusable nested content | M02, M04, M06 | ACC-006, ACC-007 |
| R06 Local and network receiving | M08, M10, M15, M16 | ACC-008, ACC-009 |
| R07 Professional codec playback and scrubbing | M05, M09 | ACC-015, ACC-016, ACC-018 |
| R08 Managed applications and dedicated protocol | M08 | ACC-013, ACC-014 |
| R09 Arbitrary connected surfaces and multi-output sources | M02, M07, M08, M11 | ACC-002, ACC-003, ACC-009, ACC-033 |
| R10 Local remote and headless operation | M03, M04, M06, M12 | ACC-010, ACC-020, ACC-021 |
| R11 Stability and projector recovery | M04, M12, M13 | ACC-022, ACC-023, ACC-024, ACC-025, ACC-027 |
| R12 Spout Syphon PipeWire NDI and OMT output | M10, M15, M16, M17 | ACC-008, ACC-010, ACC-028 |
| R13 Required desktop architectures and optional ARM boards | M00, M14, M15, M16, M17 | ACC-028, ACC-029, ACC-032 |
| R14 GPL source availability and extensibility | M00, M02, M10, M13 | ACC-030 |
| R15 Curated and custom shader transitions | M07, M09 | ACC-017, ACC-019 |
| R16 CI and milestone commits | M00, M13 | ACC-031 |


## Evidence record

Each report includes source/build/dependency identity, test profile and media hashes, exact commands, pass/fail/blocked states, collected metrics, environment/hardware, artifact paths/run URLs and reviewer notes where a physical/operator judgment is required. Screenshots/recordings support interaction review but cannot certify timing or data-race correctness.

For media/color tests, define tolerances and reference-generation methods before comparisons. Retain exact settled PTS/frame expectations. For unsupported features, verify that state survives save/load and API/UI access returns a documented capability reason. Negative tests cover malformed parameters, cycles, interrupted writes, old generations and denied/unowned lifecycle actions.
