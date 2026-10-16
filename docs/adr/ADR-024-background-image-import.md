# ADR-024: Background image import using Windows system codecs

State: accepted for the initial M05.C01 slice; M01/M04/M05 qualification stays open.

The existing immutable source handoff needs real file pixels. Use a portable,
IO-free import transaction to create a reusable `mapgyver.image` definition and
bind the sole root video input. Preserve authored stage/calibration. The direct
CLI path creates a window and centred layer fit only for a new default project.
Layer visual `fit` is an optional v1 extension with legacy `stretch`; earlier
required fields and unknown-field rejection remain strict, as in ADR-023.

Use Microsoft WIC PNG/JPEG CPU decoding and BCrypt SHA-256 privately in the
Windows adapter. WIC is an OS component, not a bundled proprietary runtime; no
new redistributable dependency or license exception is introduced. Newly authored
loader/tests remain GPL-3.0-or-later. Microsoft vendor selection is verified.
FFmpeg remains the planned video/audio path; adding another bundled still-image
codec is unnecessary for this measured baseline. Portable code sees immutable
pixels, locators and metadata, never COM or OS handles.

One preparation thread owns COM, codec objects and a content-hash pixel cache.
Actual file bytes are rehashed before reuse. Limits are 64 MiB encoded/file,
256 MiB read/request, 128 MiB canonical pixels/request, 256 active image definitions,
and 64 cache entries/256 MiB cached pixel payload. Reads use 64 KiB chunks and
conversion uses 64-row stripes, with cooperative cancellation between calls.
An active SDK/kernel call cannot be interrupted; the existing owned-process stop
guard remains. Capacities exclude SDK/OS internals and do not measure resident
memory. Active shared immutable pixels survive eviction. Initial decoding runs
while native placeholder frames/clock continue; failed edits retain the previous
scene and configured locator with an `image_*` reason. Per-source offline
placeholders, file watching and complete job/readiness reporting remain required.

The qualified format profile starts with static 8-bit SDR PNG/JPEG. Untagged
content assumes sRGB; declared sRGB and linear PNG gamma have explicit handling.
Supported WIC colour contexts transform to sRGB before linear premultiplication.
JPEG EXIF 1-8 transforms pixels before publication. Unsupported depth, transfer,
animation, PNG orientation and profile modes fail explicitly. CPU file reads,
decoded RGBA8 bytes and native GPU copies have separate counters. This does not
claim hardware image decoding, physical scanout or deadline qualification.

Owned generated fixtures and actual system-codec tests cover alpha/transfer,
hashes, all eight JPEG orientations, Unicode/long relative paths, request limits,
cache invalidation/eviction/retained ownership, cancellation and rejection.
Independent engine qualification covers relink, missing-file/undo, registry/OSC
fit edits and GPU intermediate captures at the existing 2/255 tolerance.

Primary API references reviewed for the authored adapter:

- [WIC decoding from a stream](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/wic/-wic-decoder-howto-createusingstream.md)
- [IWICColorTransform](https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nn-wincodec-iwiccolortransform)
- [IWICColorTransform::Initialize](https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwiccolortransform-initialize)
