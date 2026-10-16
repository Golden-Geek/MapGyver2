# Active execution plan - MapGyver Windows beta

Current continuation: M03.C02 portable adapter code is
0680d44dbb62e6e03e5f2416d1afe445d9b14ff4, with clean Debug/Release 15/15
wrapper results retained in M03-C02-adapter-local.json. Next implementation:
Boost.Beast/Asio HTTP/UDP/WebSocket adapter polled by the existing control owner,
actual bound endpoints and independent socket clients, bounded peers/traffic/
deadlines. Default loopback and explicit external OSC editing-owner transfer;
do not advertise remote authorization, discovery or native applied/save outcomes.

## Scope and prerequisite evidence

- Authorized: implement M00-M13; product MapGyver; public Golden-Geek/MapGyver2;
  tested local commits pushed directly to main. No release/policy changes.
- M00 complete at 93be949919b6aedeccda365087f7b6b1781ce27f, green CI 38771335741;
  annotated milestone/m00 points there. M01 qualification and independent M02
  portable/M03 control implementation are in progress; M04-M19 remain planned.
- Follow backlog IDs/dependencies. Renderer stays provisional; do not qualify
  dependent milestones from missing physical/hardware evidence.

## Tested M01 slices

- C01: 0300f34e44cc7c611483ac13ca2af990716b010a; CI 38778592743 green.
  Clean Debug/Release: 12 exact owned/imported texture cases plus real keyed
  mutex timeout each. Actual COM/fence lifetime, no renderer fork.
- C02: 681888124220b0f7e7008f001b879b4974dd2265; CI 38788520324 green.
  Clean Debug/Release: 24 cross-SDK Spout/bgfx cases each, resize/format changes,
  producer restart, held frames and real producer/consumer mutex stalls.
  App SDK 2.007.010#1; independent official MD Release SDK 2.007.018 in its own
  project/DLL directory. Actual DLL hashes/GPU LUIDs checked. No global registry
  frame-count change. SDK bool success alone is not frame/copy evidence.
- C03 decoder: 9d0716eeee9868f259d05f145c95b5f6f61007f2; CI 38790658271 green.
  Clean native Debug/Release: 54 strict D3D11VA MPEG-2/NV12 frames each, three
  generations, GPU BT.601 conversion and bgfx sampling; software-reference max
  channel error 1 (fixed tolerance 2). Actual format refusal fails/no fallback.
  Fixture hashes, rational PTS, API submissions, RAM/VRAM and offline timing
  retained. Other codecs, full decode/show latency and performance unqualified.
- C03 worker faults: fe8c5ed7963b6cdd371ef2f4890bab64429417c6.
  Clean native Debug/Release pass both directions: own consumer death, six native
  publications without consumer, consumer reconnect, producer death while holding
  mutex (metadata vanished: disconnected/not-ready), fresh pair restart.
  No production supervisor/backoff or driver/device-loss claim.
- C03 frame sets: 8f58a75878afad425307efbf02f6c8fd6abb16c9.
  Clean native Debug/Release each pass 8 complete sets/40 exact member hashes,
  port counts 2/5/11/2, sizes 64/128/256/64, RGBA/BGRA. All members submit in one
  applied bgfx frame and retire after common GPU completion. Missing native
  member and actual stale GPU pixels fail specifically/no success JSON.
  Changed-sampler regressions all pass Debug/Release: C01, C02, decoder and
  worker faults. windows-decode/windows-spout wrappers pass 5/5 CTest/CLI checks.
- Unaltered native reports and command/negative summaries are in docs/progress;
  actual source hashes, CI URLs, evidence limits are in docs/progress/M01.md.

## Current baseline and next actions

- Worker/frame-set commits and clean evidence were pushed through 2f8b189.
  CI 38793706815 is running; plan passed. Decoder CI 38790658271 is green.
- Current independently tested C03 working tree: native SDL/bgfx multiple-window
  submission, resize/destruction/recreate and ownership metrics.
  Pinned upstream examples/22-windows shows swapchain destruction must be flushed
  before native window destruction; use the pinned API, not latest assumptions.
- Hidden-window API results must remain distinct from physical two-output and
  optical scanout evidence. Do not tag M01 or accept a renderer from them.
- Debug/Release trials pass 12 application frames/33 native window draws across
  create/resize/remove/recreate stages. Actual framebuffer handles return to zero
  and SDL retains only the original context window. All windows hidden; window
  pixels not read back. Both wrappers pass 5/5 CTest/CLI checks; C01 Debug lifetime
  and real timeout regression passes. Source is 2f8b189 plus dirty=true.
  Next: commit the tested window slice, clean-source reports, hosted evidence.
- Window slice is now committed locally at c50228c31b646097e2d4a750359ab4642e9090a0.
  Clean native Debug/Release wrappers and API contracts pass; unaltered reports
  and commands are in docs/progress/M01-windows-*.json / M01-window-contracts.json.
  No window pixel/visible scanout claim. Keep M01.C03/C04 open; no tag.
- Next independent work while CI/hardware gates remain open: M02.C01 portable
  stable assets/instances/typed composition ports and cycle validation. This does
  not select a renderer, depend on native SDK types or qualify M02 ahead of M01.
  Preserve backlog dependency order for completion; continue authorized model
  implementation without substituting docs/tests for missing hardware evidence.
- CI 38793706815 is now green at 2f8b189 (worker/frame sets). Window code c50228c
  and evidence dcf804e are tested/committed locally, queued for the next push.
- Current M02.C01 first vertical slice is implemented: portable immutable
  definition snapshots, stable IDs, typed asset/composition ports, input slots,
  explicit local sharing declarations and iterative graph cycle validation.
  Debug/Release windows-local each pass 6/6 CTest. New code is under
  libs/domain with tests/domain/test_definitions.cpp and progress/M02.md.
  This slice has no live source/transport ownership yet; C01 remains partial.
  Next: commit/push the tested definitions slice with
  queued M01 work, collect hosted evidence, then implement scoped instance state.
  The chosen instance boundaries/next contracts are in libs/domain/README.md.
- Definitions code fe7f107c9ec219db92ceb2d976a615449b213049 is now pushed, together
  with native-window code/evidence. Clean local Debug/Release 6/6 reruns pass;
  CI 38797212248 portable Linux/macOS jobs passed, Windows is still running.
- Current M02.C01 instance slice is implemented under instances.hpp/cpp:
  independent nested scopes, explicit sharing, per-instance rational transport
  intent, keep-alive/last-reference retirement, live input cycle validation and
  configurable capacities. All structural failures are atomic candidate edits.
  Tests include 2,048-deep expansion/retirement and shared descendants.
  Final Debug/Release each pass 7/7 CTest, including nested keep-alive and ID
  collision rollback. Next: commit, clean-source/hosted evidence. No decoder readiness or
  SDK shutdown is claimed by these portable logical ownership tests.
- C01 ownership code 4d1efdb0b3f57c26e5a83438d5a2eb4d429e130c is now pushed.
  Clean local Debug/Release 7/7 reruns pass; hosted checks pending. Prior
  definitions/window CI 38797212248 is fully green at fe7f107.
- Current next slice M02.C02: arbitrary surfaces/groups/layouts and many-to-many
  projection patches. Surface content owns a unique logical placement lease to
  a video output of a composition instance; shared canvases use explicit shared
  instance leases. Calibration is a separate patch object from content binding.
  Continuous layouts require the same composition instance/output, with explicit
  UV chart/seam metadata; independent layouts allow distinct content. No physical
  continuity/unfolding or scanout synchronization is inferred from metadata.
  Keep desired output bindings (including unknown adapters) as configured data.
  Default logical project should own root composition/input slot, one surface,
  one logical output and a four-corner patch. Native binding/qualification stays
  separate. Use stable geometry/control-point IDs and portable finite numbers.
- C01 ownership CI 38800642307 is fully green at 4d1efdb on Windows/Linux/macOS,
  all seven contracts. C01 portable functional outcomes implemented; M02 remains
  unqualified behind M01 physical/device/performance gates.
- M02.C02 is now implemented: immutable stage snapshots, surfaces/groups,
  explicit UV charts/seams, independent/continuous layouts, many-to-many patches,
  separate canvas/desired-mode/calibration ownership and logical default project.
  Debug/Release each pass 8/8 CTest. Stage tests cover 1/3/37 surfaces and preserve
  calibration through content/mode changes. Test-only map-key lifetime crash was
  fixed (owned ID); no checks weakened. Unknown adapters/formats remain configured.
  Next: final review, commit/push C02, clean-source and hosted evidence, then
  M02.C03 typed registry/transaction/undo-redo with combined model validation.
- C02 committed/pushed as cc29c53207b94bb21904c36bd3d5bb9c67ab1ede.
  Clean Windows Debug/Release wrappers pass all eight contracts; exact commands,
  environment and parsed actual JUnit outcomes are in M02-C02-local.json.
  Hosted results pending. Next C03 must reconcile definition edits without
  resetting unchanged live IDs/transport, validate instance+stage references in
  one candidate, expose typed metadata/access/units/persistence through one
  registration path, and support atomic structural batches with undo/redo.
- C02 CI 38808397792 native Linux/macOS portable and plan jobs passed; Windows
  native Debug/Release dependency builds are still running. Preserve that result
  instead of cancelling it with a routine documentation push. Evidence docs can
  be committed locally and pushed with the next tested implementation slice.
- C02 CI 38808397792 is now fully green (Windows Debug/Release, Linux/macOS,
  plan, quality-gate). Current C03 prerequisite reconciles edited definitions
  while preserving unchanged live scopes/transport, retaining external owners
  and keep-alive roots, and rejecting removed bound ports/capacity atomically.
  ID allocation context survives snapshot restore. Debug/Release pass all eight
  contracts, including 2,048-deep reconciliation on fdc879c plus dirty=true.
  Next: commit/push tested prerequisite, then central transactions/typed registry.
- Continue independent authorized work around unavailable hardware/legal gates;
  do not mark dependent milestones complete or fake missing hardware success.
- Reconciliation committed/pushed fd8cd6d6c69351979cef9f9320b80582dfe89da8;
  clean Debug/Release 8/8 evidence M02-C03-reconcile-local.json. Hosted
  CI 38812032068 portable Linux/macOS and plan passed; Windows still running.
- Current C03 transaction slice: ProjectController immutable combined state,
  typed structural/input/transport/ownership commands, final candidate validation,
  root lease invariant, authored revisions, bounded snapshot/history accounting,
  atomic rollback and undo/redo preserving IDs. Debug/Release 9/9 passed.
  Next: commit tested controller, then one typed registry path with metadata,
  access/validation/API descriptors and generated inventory checks. No native
  preparation/application/save completion is implied by authored revisions.
- Transactions committed locally as ac2e86a; clean Windows Debug/Release 9/9
  evidence retained in M02-C03-transactions-local.json. Waiting for source
  CI 38812032068 before pushing queued slices to avoid cancelling Windows builds.
- Current C03 registry slice registers current model fields and typed setters
  with stable addresses/type/default/range/unit/access/persistence/description/
  authored availability metadata, plus all typed command actions and undo/redo.
  Compound writes use one combined candidate. Derived bindings stay read-only;
  stale registry inventories reject even after undo restores an old pointer.
  Debug/Release 10/10 pass including registered reconciliation/geometry space tests.
  Initial dotted action IDs correctly failed validation and
  were fixed without changing the ID contract. Next: commit, clean/hosted evidence,
  then wire/type descriptors and capability-preserving round-trip coverage.
- Registry committed locally as 0fcfaa0a2e6d105f4a28eecb170770b79c5e5c38.
  Clean Windows Debug/Release 10/10 wrappers pass, dirty=false; evidence retained
  in M02-C03-registry-local.json. Source CI 38812032068 Windows Debug has now
  passed; Release/quality-gate pending. Push queued tested slices once it finishes.
  Next implementation: portable wire codecs/schema for typed descriptors and
  commands, capability-preserving model round trips with strict reference/ID/
  rational/topology/cycle/capacity validation, then M03 service integration.
  Do not claim an OSCQuery implementation from in-process inventory tests.
- CI 38812032068 is now fully green at fd8cd6d (Windows Debug/Release,
  Linux/macOS, plan/quality-gate; eight contracts). Queue is ready to push:
  ac2e86a transactions, 0fcfaa0 registry, cd6c273 clean-source evidence and this
  hosted evidence record. Local ten-contract Debug/Release evidence stays tied
  to exact code SHA 0fcfaa0, independent of later documentation commits.
- Queued work was pushed through 1345f17671599310dd123fd0952bfa0083f0b151;
  CI 38814598276 is confirmed live. Working tree started clean on continuation.
  Previous goal turn was progress (three tested implementation commits).
- Current M02.C03/C04 wire/round-trip slice: restore logical InstancePool graphs
  with recomputed/verified owner counts, stable-scope child sharing, typed ports,
  transport/cycle/capacity/orphan validation; allow validated restored ProjectState
  to enter ProjectController. Debug 10/10 passed; Release running. No native
  process, SDK or decoder state is restored. Next: strict bounded JSON parser,
  codecs sharing field registration with generated schemas, exact integer/rational
  transport representation, command/registry/project round-trip tests. SQLite
  durability and native resume remain M12/runtime work.
- Wire slice implemented: same field registration encodes/decodes/generates
  control-model-v1 schema; project, all commands/parameter types and registry API
  descriptors export through it. Strict byte/event/depth parser rejects duplicate
  keys and invalid encoding; uint64/int64 decimal strings preserve exact values.
  Restored logical graphs validate scopes/counts/cycles/typed ports/orphans before
  root+stage acceptance. Tests pass 1/3/37 surfaces and 2,048-deep ownership.
  Debug/Release 11/11 passed; local jsonschema 4.26.0 independently validated
  schema and four exported document classes. Owner counts changed to fixed uint64
  to keep the wire format portable across pointer widths; final Debug/Release
  reruns pass all eleven contracts after that change.
  Next: commit, clean-source evidence and push after prior CI.
  M12 still owns SQLite persistence/checkpoint filtering/durable recovery; M03
  owns reliable envelope/service/subscriptions/OSCQuery/TypeScript exports.
- Prior CI 38814598276 is now fully green at 1345f17: Windows Debug/Release,
  Linux/macOS, plan and quality-gate; all ten pre-wire contracts. New wire and
  restoration code has local eleven-contract results, not hosted results yet.
- Wire/restoration committed as beca81ae2fca82b4d4fc64f1c8a87edf0297080b.
  Clean Windows Debug/Release 11/11 passed with exact SHA/dirty=false; retained
  M02-C04-local.json includes parsed JUnit and independent schema validator
  command/provenance/hashes. No new native runtime dependency was added.
  Next: push code/evidence, inspect hosted results, then implement independent
  M03.C01 reliable control envelopes, authored snapshots/subscriptions and Windows
  named-pipe service. M01 remains unqualified, so dependent milestone completion
  is still disallowed. Renderer application/frame and durable save outcomes must
  stay separate from authored acceptance; never fabricate an applied GPU frame.
- Wire/evidence pushed through 25de3514f73299374162929435713552bafe881e;
  CI 38817597603 confirmed live; Linux/macOS portable and plan jobs passed,
  Windows native jobs still running. Previous goal turn was progress.
- Current independent M03.C01 portable core: strict versioned envelopes,
  connection-bound service/client/session IDs, authored revision checks, one edit
  owner with generation-controlled transfer, bounded terminal reply cache with
  per-session sequence watermark retained beyond reply eviction, reconnect retention and bounded event
  journal/full-snapshot resync. Native applied/save statuses remain unavailable.
  Final Debug/Release 12/12 passed after fixing boundary revision_conflict
  classification, including byte-cache eviction/remote batch rollback tests.
  Next: final tests, commit, clean/hosted evidence; then real Windows named-pipe
  adapter/engine CLI with bounded framing, IO deadlines, disconnect/reconnect and
  actual separate-process integration tests. C01/M03 remain incomplete until then.
- Prior wire CI 38817597603 is fully green at 25de351: all Windows Debug/Release,
  Linux/macOS, plan and quality-gate jobs, eleven contracts. New M03 core has
  local twelve-contract evidence only until its own source CI completes.
- Core committed as b73b6be (full SHA in M03-C01-core-local.json).
  Clean Windows Debug/Release wrappers pass 12/12, dirty=false. Evidence explicitly
  classifies injected control-clock/in-process tests and does not claim IPC.
  Next: push code/evidence and implement the Windows named-pipe adapter plus
  --serve engine mode. Use private Windows SDK types, current-user ACL and remote
  rejection, bounded length-prefixed frames/IO deadlines, real separate-process
  tests including slow peers, partial frames, lost-ack reconnect and service
  restart. Preserve outstanding source CI rather than routine docs cancellation.
- Current M03.C01 native slice: engine --serve/--pipe-name, private Windows
  overlapped byte-pipe adapter, protected current-user DACL, remote rejection flag,
  first-instance ownership, 16 peers, bounded frames (1 MiB/4 MiB), five-second
  partial/handshake and two-second write deadlines, completed-IO cancellation
  before buffer reuse. Independent Python SDK clients exercise the actual engine.
  Debug 13/13 passed with 12 real IPC phases, including actual DACL readback,
  lost-ack/resume, readonly/revision checks, push notifications, partial frames,
  real buffer stalls, cancelled large reply replay, recycling/handle counts and
  owned process restart. First collision assertion expected Win32=5, but the full
  native instance pool returned 231; a separate controlled SDK probe confirms
  first-instance access denial with a spare slot and busy with a full pool.
  Source/SDK assertions were preserved with those explicit verified error classes.
  CLI invalid/unsupported tests added; final Debug/Release reruns next.
- Prior core CI 38820881149 is fully green at 9ac7a11, all native Windows
  Debug/Release and Linux/macOS portable jobs plus plan/quality-gate (12 contracts).
  Native pipe slice final local Debug/Release wrappers pass 13 contracts and
  12 independent real IPC phases. Native cancellation verifies completed events
  before releasing buffers; never treats a cancellation request as completion.
  Next: final review/commit, clean source/build/IPC evidence, push, then M03.C02
  OSC/OSCQuery discovery and network adapters with the same validated service.
- Native engine/pipe code committed as 551c322ab6b99b4be3c9ce3d662d28f98e35131b.
  Clean Windows Debug/Release 13/13 wrappers and twelve real IPC phases each pass
  with exact source SHA/dirty=false; retained M03-C01-pipe-local.json includes
  environments/JUnit/native reports and binary/fixture hashes. Next: push tested
  code/evidence, inspect source CI, then M03.C02 official OSCQuery/OSC contracts,
  adapter choice/provenance and real network interoperability. Native applied
  frames remain unreported until the render worker is connected; no milestone tag.
- Native pipe/evidence pushed through a3a8ca94baacb8fe49e890cd9a6252121fa4f87c;
  CI 38824975794 confirmed live. Portable Linux/macOS and plan have passed;
  Windows jobs are still running. Previous goal turn was implementation progress.
- Current M03.C02 portable slice: bounded OSC big-endian/padded codec, nested
  bundle/timetag preservation and strict numeric/UTF-8/type parsing; immutable
  registry-backed OSCQuery metadata/tree/value/action mapping and change detection.
  ADR-019 records reviewed libossia sources and direct-registry choice. No new
  runtime dependency or network endpoint yet. OSC golden/corruption tests passed;
  first tree test timed out on an unused invalid iterator across temporary
  inventories. That test-only mistake is removed; timeout/assertions unchanged.
  Rebuild/final validation next, then actual HTTP/UDP/WebSocket transport and
  independent client tests. Do not advertise an unimplemented streaming service.
- C02 codec/tree/subscription/ingress final Debug/Release wrappers pass 15/15,
  including existing real pipe tests. Exact subscriptions and all parameter/
  structural metadata/values route through the authoritative model; standalone
  projection defaults LISTEN/PATH_CHANGED flags false. Ingress has an explicitly
  granted editing role and atomic homogeneous bundles; unsupported scheduling and
  mixed grouping reject. No new socket/runtime dependency exists yet.
- Prior native source CI 38824975794 is fully green at a3a8ca9: Windows 13
  contracts/12 pipe phases; Linux/macOS portable 12; plan/quality-gate. Next:
  commit portable C02, clean-source evidence, then actual Boost.Beast/Asio
  HTTP/WebSocket/UDP adapter with provenance and independent client qualification.

## Build/hardware commands

- python tools/ci/check.py --preset windows-spout --configuration <Debug|Release> --skip-bootstrap
  Builds app + verified separate /MD Release reference peer; non-GPU tests only.
- python tools/ci/check.py --preset windows-decode --configuration <Debug|Release> --skip-bootstrap
  Builds FFmpeg avcodec/core probe and all bgfx experiments; non-GPU tests only.
- Manual tests: tests/hardware/test_bgfx_interop.py, test_spout_interop.py,
  test_decoder_import.py, test_spout_worker_failure.py, test_frame_sets.py.
  Use --help for exact executable arguments and fresh out/reports directories.

## Decisions and open gates

- Local VS2026/v145; hosted Windows VS2022/v143 (ADR-018). Actual versions per
  wrapper report. Registry 20ceb8adbf0504d4b0d5e4058a32f9d4fc896391. Standard port
  patches only. Source/dependency notice inventories are under docs/licenses.
- Laptop only: Win11 x64 10.0.26300, NVIDIA RTX 5080 Laptop GPU, driver
  32.0.15.9201 plus Intel integrated. Physical two independent outputs/projectors
  and hotplug qualification are unavailable. Keep those gates open.
- Counters describe explicit API submissions/controlled SDK paths, never hardware
  bus/copy/decoder-engine counts. Offline readback/allocation/waits are not show
  deadline work. Fixed fixture capacities do not limit product surface counts.
- Device-loss test is not run; do not force a global GPU TDR that disrupts unrelated
  apps on this laptop. Production worker supervision/recovery is still future work.
- M01.C04 backend decision and exact sustained workload budgets stay open.
  NDI legal/distribution path remains unresolved (ADR-011), blocking M13 coverage.
