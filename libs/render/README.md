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
Nearest sampling uses pixel centres. Quad projection mapping and shared UV charts
are compiled below. Crop/fit/fill, rectangle/ellipse masks, opacity and
over/add/screen blends are implemented. HDR output, mesh warps and media decoding
remain subsequent work.

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
prepare independent swapchains and bounded linear mapped/held/presented textures.
Image/graph/window handoffs and four-corner mapping are implemented; encoded
preview, mesh warps and qualification gates stay open. This implementation
does not accept the renderer or complete M04.

`compile_mappings()` resolves stable-ID calibration boundaries into normalized
inverse homographies, retaining the authored reference resolution. Unit-UV quad
surface/chart topology must cover the full quad; unsupported mesh topology and
ambiguous chart ownership retain their objects and explicit reasons. Continuous
and independent quad charts map surface UV to composition UV. Patches share the
resolved composition node and render back-to-front in stable patch-ID order.
Per-output RGBA16F mapping targets blend linear premultiplied content before one
opaque sRGB display encoding. All three output textures count toward capacity.

Canonical float coefficients are shared by the native shader and CPU reference.
Coverage uses inclusive top/left and exclusive bottom/right edges, including
shared edges exactly on a pixel centre. Identity mappings retain exact unsigned
integer pixel-centre sampling. Other mappings use nearest texel loads inside
the calibrated quad/composition UV range and transparent coverage outside it.
The mapping-patch capacity is 16,384 before native hardware view/draw/handle limits.
Compilation and reference rendering allocate only on preparation/offline paths.

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
Composition-graph resource banks support canvas-size and topology changes, with
separate prepared-window replacement and quad mapping. Sustained deadline,
physical-output and device-recovery qualification remain open.

The downstream bgfx overlay in `ports/bgfx` supports a prepared D3D11 target
through a realized 1x1 placeholder and `overrideInternal2D` before attachment.
It updates front/backend dimensions; the original pointer-only call clipped
resized framebuffer views. The manual preparation probe checks a 257x131 target
and unchanged old-scene frames, plus invalid pointer/size/attachment refusal.
See ADR-021 and `docs/licenses/bgfx-native-import.json`. Owner native-view creation
is staged in the graph-bank path. Background native allocation, incremental owner
binding, frame-boundary swap and completion-based retirement preserve old frames
during preparation. The GPU probe resizes a canvas, adds a source/layer with a new
root node index, verifies freeze/unfreeze and cancels a partially bound graph.

Surface content settings crop composition UV and fit/fill the cropped chart
region to the logical surface aspect, measured from mean opposite local edges.
Fitting centres surface UV before chart mapping; uncovered fit pixels are black
on the physical output. Each patch masks original surface UV with a rectangle or
ellipse, independently of crop/fit. Hard rectangle edges are half-open. Feather
is a normalized inward fraction of mask bounds; inversion complements coverage.
Surface and patch opacity multiply all associated components together.

Composition layers and projector patches support linear over, add and screen,
all with over alpha. Foreground RGB clips to SDR 0..1 when consumed; add can
produce brighter intermediates before the next consumer/export. Named exports
retain their declared transfer/alpha encoding; physical outputs encode opaque
sRGB after composition over black. Layer opacity zero culls that visual branch.
Per-composition layer count is additionally bounded by the finite half-float
accumulation range. No HDR-output or multiply-mode support is claimed.

Optional authored settings resolve to explicit defaults when loading earlier v1
documents; previous required/unknown-field checks stay strict. The same registry
supplies UI/OSC/remote writes, validation and undo. See
[ADR-023](../../docs/adr/ADR-023-visual-settings-wire-extension.md).
