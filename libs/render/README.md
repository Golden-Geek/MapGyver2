# Prepared composition rendering

`compile()` resolves active video branches from the root preview and surface
leases into an immutable, dependency-first plan. It preserves source-instance
scope, deduplicates explicitly shared instance/output pairs and leaves inactive
definitions untouched. Missing frames and unsupported canvas encodings retain
configured objects and report reasons; their visual result is transparent.

Prepared images use finite SDR linear-sRGB premultiplied float pixels. RGBA8
preparation decodes the declared transfer function before premultiplication.
Compositing uses back-to-front premultiplied over. Exports unpremultiply, encode
the named canvas transfer function and optionally premultiply its encoded RGB.
Nearest sampling uses pixel centres. Masks, geometry mapping, blend modes other
than over, HDR and media decoding are subsequent work.

Plans bound node/layer counts, dimensions, prepared-image bytes, compatible
RGBA16F target slots and total texture payload. Lifetime intervals prevent a
pass from overwriting a live dependency. Shared immutable image resources cache
by resource ID and generation; conflicting objects with the same identity reject
preparation. Empty branches share one transparent constant. These byte counts
describe texture payload and CPU uploads, not measured VRAM commitments or bus
traffic. Callers must not mutate storage through other aliases after preparation.

The provisional Windows adapter uses pinned bgfx/D3D11 and SDL3, prepared authored
shader binaries, RGBA32F uploads, RGBA16F internal targets and RGBA8 exports.
It verifies actual hardware identity and renderer capacities before resource
preparation. Dense view IDs count composition passes rather than source leaves.
SDK objects remain private to the adapter. `frame()` submits an already prepared
plan; resource creation, shader compilation and synchronous readback are outside
that function. The adapter provides offline qualification and the engine's
independent native owner. [Native window controls](../runtime/README.md)
prepare independent swapchains and bounded held/presented textures. Hot resource
handoff, nontrivial patch mapping and preview remain open. This implementation
does not accept the renderer or complete M04.

Build/check on the Windows laptop:

```powershell
python tools/ci/check.py --preset windows-render --configuration Debug --skip-bootstrap
python tools/ci/check.py --preset windows-render --configuration Release --skip-bootstrap
python tools/qualification/check_composition.py --probe out/build/windows-render/adapters/windows/render/Debug/mapgyver-composition-probe.exe --shaders out/build/windows-render/adapters/windows/render/generated/shader-build.json --report out/reports/composition-Debug.json
```

Use the Release executable/report for the Release hardware run. Hardware goldens
run explicitly, outside default CTest/hosted CI. `capture_for_test()` performs
bounded synchronous staging/query/readback solely for offline validation. GPU
copy submissions and CPU readback bytes are reported separately; no hardware
copy counter is inferred from a successful SDK call.

The Windows adapter now explicitly owns its D3D11 hardware device without the
`SINGLETHREADED` flag and supplies that device to bgfx. `PreparationDevice`
creates immutable RGBA32F image generations without using the immediate context.
Startup imports borrow these textures; native owners retain them through bgfx
shutdown. Every bgfx/immediate-context call still stays on the render owner.
The actual driver concurrent-creation/command-list capabilities are reported,
not inferred from device thread safety. Aggregate job/retained-byte bounds belong
to the preparation caller; one image is limited to 256 MiB and valid SDR pixels.

```powershell
python tools/qualification/check_preparation.py --probe out/build/windows-render/adapters/windows/render/Debug/mapgyver-preparation-probe.exe --report out/reports/preparation-Debug.json
```

That manual real-GPU experiment creates 128 image generations on a background
thread during 120 native frames, retains at most eight/1 MiB, verifies the active
output at tolerance 2/255 and bitwise checks a prepared float texture offline.
It also checks invalid payload rejection and owned-device lifetime after bgfx
shutdown. Background allocation alone does not qualify runtime handoff or budgets.

The adapter's fixed-layout image handoff now prepares an opaque generation on the
background device and copies it into existing DEFAULT-usage sampled textures on
the immediate-context owner. No handle, SRV, texture or query is allocated during
application. One preallocated event query supplies nonblocking completion; a busy
copy preserves caller ownership. Retired source textures/CPU plans return to the
preparation caller for disposal. Generations, native device, layout/draw capacity
and aggregate active/candidate payload are validated before any copy. A candidate
is limited to 256 resources/128 MiB; callers must also bound other queued jobs.
Each source update incurs a reported GPU-copy submission and logical payload
bytes, separately from its preparation CPU upload. Copy and draw commands use
the same immediate-context order. This does not imply synchronized scanout.

The manual probe additionally exercises two live image generations, actual copy
completion, retirement on a preparation thread, rejected old/foreign/oversized
candidates and six fixed-tolerance freeze/unfreeze/blackout goldens. The engine
now connects this API to authored source edits through bounded preparation/
retirement mailboxes; see [native qualification](../runtime/README.md).
Graph/window resource changes and sustained deadline/recovery qualification
remain open.

The downstream bgfx overlay in `ports/bgfx` supports a prepared D3D11 target
through a realized 1x1 placeholder and `overrideInternal2D` before attachment.
It updates front/backend dimensions; the original pointer-only call clipped
resized framebuffer views. The manual preparation probe checks a 257x131 target
and unchanged old-scene frames, plus invalid pointer/size/attachment refusal.
See ADR-021 and `docs/licenses/bgfx-native-import.json`. Owner native-view creation
still needs staged work; this experiment does not implement complete graph banks.
