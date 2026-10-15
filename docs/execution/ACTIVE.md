# Active execution plan ? MapGyver Windows beta

## Scope and prerequisite evidence

- Authorized: implement M00?M13; product MapGyver; public Golden-Geek/MapGyver2;
  tested local commits pushed directly to main. No release/policy changes.
- M00 complete at 93be949919b6aedeccda365087f7b6b1781ce27f, green CI 38771335741;
  annotated milestone/m00 points there. M01 in progress; M02?M19 planned.
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
- Continue independent authorized work around unavailable hardware/legal gates;
  do not mark dependent milestones complete or fake missing hardware success.

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
