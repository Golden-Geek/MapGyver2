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
