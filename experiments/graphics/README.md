# M01 graphics experiments

Build with `python tools/ci/check.py --preset windows-graphics --configuration
Release`. Windows CI also compiles these experiments; hosted CI does not execute
GPU qualification. Portable builds leave them disabled.

Run `out/build/windows-graphics/experiments/graphics/Release/mapgyver-d3d11-baseline.exe
--json` explicitly on a Windows x64 GPU workstation. It rejects software adapters,
selects the hardware adapter with the largest dedicated VRAM and records its real
identity. Two devices exchange an NT shared texture with explicit keyed-mutex
ownership, an event query with a two-second completion bound, and exact pixel
validation for RGBA8/BGRA8 at 64, 128 and 256 pixels. Every generation recreates
the resources; the temporary NT handle is closed after import.

This is an offline diagnostic with allocations, CPU upload and staging readback.
Those operations are counted separately as API submissions, not hardware copy
engine/bus counters. Do not put it in a render deadline path or use it as a
zero-copy, Spout, throughput, color-management or physical-output qualification.
Its JSON explicitly records bgfx/Spout/output tests as not run. Missing support,
timeouts, abandoned mutexes, device errors or pixel mismatches fail the process.

The next experiment must exercise bgfx's pinned D3D11 import API and retain
borrowed external resources through bgfx/GPU retirement. The renderer remains
provisional. See the milestone progress document for actual source evidence.

## bgfx import and sampling

Build with `python tools/ci/check.py --preset windows-bgfx --configuration
Release` (or Debug). This optional feature resolves pinned bgfx and SDL3.
Windows CI compiles it; portable builds do not install GPU/window dependencies.

Run the real GPU contract explicitly:

```powershell
python tests/hardware/test_bgfx_interop.py --executable out/build/windows-bgfx/experiments/graphics/Release/mapgyver-bgfx-interop.exe --report-directory out/reports/bgfx-hardware-release
```

The contract verifies 12 owned/imported sampling cases with exact RGBA/BGRA
color and alpha, three sizes and distinct resource generations. A separate
process with `--hold-producer-key` must fail with actual WAIT_TIMEOUT, exit 1 and
no success JSON. Software adapters are rejected. Resource views are destroyed
on the single bgfx owner, GPU completion is checked, then borrowed native
resources are released. Native handles never cross the experimental public API.

bgfx is compiled without its multithreaded feature. Its frame() renders inline;
calling renderFrame() in that build is invalid and asserts before initialization.
All fatal callbacks, including DebugCheck, terminate with diagnostics rather
than enter a debugger dialog. Debug trace storage is bounded at 64 KiB and any
truncation is counted. No persistent shader cache/profiler is configured.

Only the validation phase performs staging copies/readback. API counts are not
hardware counters or a zero-copy claim. Shaders are embedded from verified,
licensed [upstream assets](shaders/README.md). This is functional texture
interoperability, not Spout, hardware decoding, physical output or performance
qualification. M01 remains open until those separate gates pass.

## Spout with an independent SDK peer

```powershell
python tools/ci/check.py --preset windows-spout --configuration Release
python tests/hardware/test_spout_interop.py --participant out/build/windows-spout/experiments/graphics/Release/mapgyver-spout-participant.exe --reference out/qualification/spout-reference-build/Release/mapgyver-spout-reference.exe --report-directory out/reports/spout-hardware-release
```

Repeat with the application Debug configuration and a different report directory.
The wrapper builds the app against registry SpoutDX 2.007.010#1, verifies the
[independent official 2.007.018 inputs](../../tests/hardware/spout-reference.json),
then builds that peer in a separate Release-only /MD project. It runs both CLI
version checks without initializing a GPU. Windows CI compiles both peers;
the GPU contract stays manual. Keep the two same-named SpoutDX.dll files in
their separate output directories. No downloaded SDK binary is committed.

The fixture clients are authored qualification tools linked to different real
SDK versions, rather than mocks. The app samples through bgfx before publishing
and after receiving; the reference client uses native D3D11. The harness verifies
actual DLL hashes, GPU LUIDs, source identity, exact pixels including alpha,
24 resize/format cases, held frames, graceful producer restart and real producer
and consumer mutex stalls. It uses unique sender names, hidden child processes,
bounded command/report queues and deadlines, and cleans up only its own PIDs.

Startup's ready event means the control participant initialized; frame readiness
requires verified data. SDK connected/SendTexture success alone are insufficient.
An outer nonblocking named-mutex lease and instance-only DisableFrameCount force
the inspected pinned SDK copy paths; no global Spout registry setting is changed.
Source CPU uploads, candidate draws, SDK GPU copy-path calls, validation GPU copies
and CPU readbacks are reported separately. These counts describe this controlled
non-keyed fixture, not arbitrary peers or physical transfer counters. This offline
diagnostic allocates and reads back; it is not the production render deadline path.
Cross-adapter sharing, crash/device-loss recovery, physical scanout, decoder import
and performance qualification remain separate gates.

## Native decoder texture import

```powershell
python tools/ci/check.py --preset windows-decode --configuration Release
python tests/hardware/test_decoder_import.py --executable out/build/windows-decode/experiments/graphics/Release/mapgyver-decoder-probe.exe --report-directory out/reports/decoder-hardware-release
```

Repeat with Debug and a fresh report directory. Windows CI compiles the pinned
FFmpeg avcodec/core dependency and probe, and runs only its non-GPU version CLI.
The native hardware contract is manual. No external codec pack is needed.

The probe creates bounded synthetic MPEG-2 packets and a software decode reference.
Hardware decode strictly selects AV_PIX_FMT_D3D11; all software fallback formats
are refused. Each real NV12 decoder texture must belong to bgfx's actual D3D11
device, carry D3D11_BIND_DECODER, and expose a valid array slice. The native video
processor converts BT.601 limited-range NV12 to full BGRA8, then bgfx samples it.
The decoded AVFrame remains retained until conversion/rendering/validation GPU
use completes, so the decoder cannot reuse its slice early. Three independently
created decoder generations (128, 256, 128) each verify 18 rational PTS identities
and every pixel against the software reference. The two-code-value tolerance is
fixed for color-conversion rounding; it is not an arbitrary visual comparison.

The report includes actual encoded fixture SHA256/bytes, native resource slices,
applied bgfx frames, separate conversion/draw/validation-copy/readback calls,
process RAM and DXGI local/nonlocal memory snapshots. First/warm timing percentiles
are offline CPU wall times for conversion/render/validation, including allocation,
GPU waits and readback. Receive-frame call time excludes packet submission; these
are not full decode, show deadline, throughput or physical scanout latency metrics.
No hardware bus/decoder-engine counter is invented. The negative test refuses the
offered hardware pixel format and requires the real FFmpeg error, exit 1 and no
success JSON. Support/failure remains explicit.

This establishes one MPEG-2/NV12 path on the tested GPU. It does not qualify H.264,
HEVC, AV1, 10-bit/HDR, seeks, dynamic in-stream resize, sustained workloads,
device-loss recovery or physical multi-output presentation. M01 stays open.

## Owned SDK/GPU worker process faults

```powershell
python tests/hardware/test_spout_worker_failure.py --participant out/build/windows-spout/experiments/graphics/Release/mapgyver-spout-participant.exe --reference out/qualification/spout-reference-build/Release/mapgyver-spout-reference.exe --report-directory out/reports/spout-worker-failure-release
```

Repeat with the app Debug build and a fresh directory. The test terminates only
Popen handles it created. In each real SDK direction it kills the consumer,
requires six successful native publications without a consumer, starts a fresh
consumer and verifies pixels. It then kills the producer while it holds the
sharing mutex. The receiver must report disconnected/not-ready when metadata
disappears, or fail explicitly for a real abandoned mutex; stale data cannot
claim successful new content. A bounded fresh pair restart validates new pixels.
Process/session identities and timings are recorded with transcripts and actual
exit codes. No browser or preview participates. Test cleanup never discovers or
terminates user processes by name.

This is a qualification harness with one restart per injected failure, not the
production supervisor, backoff policy or a physical-output outage measurement.
Device/driver loss is not injected. Those gates remain open.

## Coherent native frame sets

```powershell
python tools/ci/check.py --preset windows-bgfx --configuration Release
python tests/hardware/test_frame_sets.py --executable out/build/windows-bgfx/experiments/graphics/Release/mapgyver-frame-set-probe.exe --report-directory out/reports/frame-sets-release
```

Any bgfx-enabled profile builds this probe. Repeat with Debug. A cooperating
native producer completes every independent source texture before sampling.
The private adapter submits all required members in one applied bgfx frame,
holds their COM resources until a common GPU completion and retires the group
together. Four generations vary port counts (2/5/11/2), sizes (64/128/256/64)
and RGBA/BGRA formats. Eight complete sets verify 40 member pixel/alpha cases
against independent Python hashes, with common source/frame/resource identity.
The fixed 12-member offline fixture capacity is not a product output limit.

The negative cases supply a missing native member and actual stale pixels in
one member's GPU texture. They require specific errors, exit 1 and no complete
success report. Software metadata alone is insufficient: validation follows
the real GPU sampling and common completion. API uploads/draws/validation copies
and readbacks are separate, and actual DXGI memory snapshots are recorded.

This proves cooperating same-device data/application-frame coherence. It does
not declare independent legacy senders coherent, implement production routing
or hold-last policy, present multiple physical windows, or qualify synchronized
optical scanout. Physical output, driver-loss and performance gates remain open.
