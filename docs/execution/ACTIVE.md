# Active execution plan ? MapGyver Windows beta

## Scope and baseline

- Authorized: M00?M13; product MapGyver; public Golden-Geek/MapGyver2; tested
  local commits pushed directly to main. No public release or policy changes.
- Planning baseline: 1d686c1. M00 complete at 93be949919b6aedeccda365087f7b6b1781ce27f,
  green CI 38771335741; annotated milestone/m00 points to that commit.
- Follow docs/backlog.json dependencies and preserve slice IDs. M01 is in
  progress; M02?M19 are planned. Renderer remains provisional.

## Completed evidence

- M01.C01: 0300f34e44cc7c611483ac13ca2af990716b010a, green CI 38778592743.
  Clean native Debug/Release each passed 12 owned/imported exact pixel/alpha
  cases and a real held-key WAIT_TIMEOUT. Retained reports in docs/progress.
- M01.C02 prerequisite: 6cdc3d13b869c5c4546d722e7dd483dc177e0ef6, green CI
  38781640097. Independent official SpoutDX 2.007.018 inputs are checksum/PE
  verified. Source licenses and the application 2.007.010#1 port were inspected.

## Current slice: M01.C02

Implementation is tested and ready for its code commit:

1. Application uses private single-owner bgfx sampling before Spout send/after
   receive. SDK/native handles stay in Windows adapters. Reference peer uses
   independently pinned official SpoutDX 2.007.018 and native D3D11.
2. Reference peer is /MD Release only, in a separate project/DLL directory from
   application 2.007.010#1 Debug/Release. Wrapper and Windows CI compile both.
   Fixture inputs remain ignored under out/qualification; no SDK binaries in Git.
3. SDK SendTexture success can mean no copy; ReceiveTexture connected can mean
   no new data. Test actual pixels/markers and generations. Outer nonblocking
   recursive non-keyed mutex + instance DisableFrameCount force inspected SDK
   copy paths. Never change the global Spout frame-count registry setting.
4. Debug/Release integrated wrappers pass all five CTest contracts, verified
   fixture preparation, peer build and CLI checks. Both GPU trials pass 24
   resize/format cases across both directions, held frames, graceful restart,
   actual producer/consumer stalls, clean teardown. Same RTX 5080 Laptop GPU.
   C01 regression passes Debug/Release, including genuine WAIT_TIMEOUT.
   Current reports correctly identify baseline 6cdc3d1 plus sourceDirty=true.
5. Supplemental Spout/GL dependency notices are recorded in docs/licenses.
   Windows SDK import libraries are compilation prerequisites, not app content.

Next: commit/push M01.C02; rebuild both configurations and reference peer from
that clean commit, rerun the manual GPU contracts, retain exact reports, collect
hosted CI and make the evidence documentation commit. Do not tag M01 yet.
Then implement the independently testable M01.C03 decoder/import/recovery
experiments; keep unavailable physical two-output requirements open.

## Commands

- python tools/ci/check.py --preset windows-spout --configuration Debug --skip-bootstrap
- Repeat with Release; wrapper builds the reference peer in Release for both.
- python tests/hardware/test_spout_interop.py --participant out/build/windows-spout/experiments/graphics/<configuration>/mapgyver-spout-participant.exe --reference out/qualification/spout-reference-build/Release/mapgyver-spout-reference.exe --report-directory out/reports/<fresh-directory>
- python tests/hardware/test_bgfx_interop.py --executable out/build/windows-spout/experiments/graphics/<configuration>/mapgyver-bgfx-interop.exe --report-directory out/reports/<fresh-directory>

## Decisions and open gates

- Local VS2026/v145; hosted Windows VS2022/v143 (ADR-018). Actual SDK/tool
  versions are in each wrapper report. Registry baseline:
  20ceb8adbf0504d4b0d5e4058a32f9d4fc896391. No private renderer fork.
- Only laptop hardware is available: Win11 x64 10.0.26300, NVIDIA RTX 5080
  Laptop GPU, driver 32.0.15.9201 plus Intel integrated graphics. Two physical
  outputs/projectors and hotplug qualification remain external gates.
- API submission/path counts are not hardware bus/copy-engine counters. CPU
  uploads, rendering, SDK copies and validation readbacks are separate.
- Crash/device-loss, hardware decode/import, physical paired scanout and fixed
  performance workloads/budgets remain unqualified. Continue useful independent
  work without marking dependent milestones complete.
- NDI distribution path remains unresolved (ADR-011); required gaps block M13.
