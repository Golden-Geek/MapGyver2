# ADR-021: Prepared native texture dimensions in the bgfx candidate

State: accepted for the M04 adapter experiment; renderer qualification remains open.

## Trigger and evidence

Background target creation needs to avoid another full-size native allocation on
the render owner. The pinned bgfx pointer override can borrow a prepared D3D11
texture, and its framebuffer backend derives dimensions/RTVs from that texture.
However, `Frame::sort()` clips the view against front-end framebuffer dimensions,
which were copied from the original texture reference. A realized 1x1 placeholder
therefore clipped a 257x131 target to one coloured pixel: measured maximum error
255 at the unchanged 2/255 tolerance. This was a real GPU failure, not a tolerance
adjustment. The source inspection and failed command are recorded in M04 progress.

## Decision

Keep the provisional bgfx renderer and add a narrow downstream C++ API,
`overrideInternal2D`, through a versioned vcpkg overlay. It borrows the native
texture and updates front-end width/height/nominal storage before framebuffer
attachment; the D3D11 backend refreshes native dimensions. It supports only the
measured single-owner D3D11 single-mip/layer/sample RGBA formats and refuses
unsupported modes, zero dimensions/pointers and already-attached handles.
The caller validates the actual native descriptor and keeps explicit ownership.

The upstream archive URL/hash and original dependency patch remain pinned. The
overlay is bgfx `1.129.8940-496#2`; upstream `#1` evidence remains historical.
Authored SDK changes are GPL-3.0-or-later alongside the upstream BSD/CC0 notices.
The installed notice contains both notices and the GPL license. No license
exception or proprietary runtime is introduced. Provenance is in
`docs/licenses/bgfx-native-import.json`.

## Alternatives and consequences

- The unmodified pointer override fails the resized-target golden because of
  front-end clipping.
- A full-size placeholder allocates the large resource again on the native
  owner and adds transient payload; it does not meet this preparation objective.
- The native D3D11 renderer fallback in ADR-003 remains available, but this
  measured metadata fix avoids replacing the compositor/presenter at this point.

The successful experiment creates the large target on a background device thread,
then realizes a 1x1 owner placeholder, imports it and creates a framebuffer while
old-scene frames continue. Owner SRV/RTV creation still allocates bounded SDK
views; staging, capacity checks and measured deadline evidence are required for
the production graph handoff. This decision does not claim complete graph/window
replacement, device-loss recovery, physical scanout or sustained performance.
