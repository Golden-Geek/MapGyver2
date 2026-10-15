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
