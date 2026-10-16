# MapGyver bgfx overlay

Copied from `ports/bgfx` at vcpkg baseline
`20ceb8adbf0504d4b0d5e4058a32f9d4fc896391` (vcpkg MIT port recipe), retaining
the exact upstream archive URL/SHA-512 and `fix-dependencies.patch`.
The upstream package is `1.129.8940-496#1`; this overlay is `#2`.

`native-2d-import.patch` adds the authored GPL-3.0-or-later C++ API
`overrideInternal2D(handle, nativePointer, width, height)`. The original upstream
BSD/CC0 notices remain. The installed notice also includes the contribution
notice and GPL license; no linking/license exception or proprietary runtime is
introduced. This modified library is part of the GPL MapGyver application.

The API requires a realized single-owner D3D11, single-layer/single-mip/single-
sample 2D RGBA8/16F/32F texture with no framebuffer attachment. It borrows caller
ownership and updates front/backend dimensions and nominal storage bookkeeping.
The caller must supply the actual native dimensions/format and retain ownership
through all GPU use and queued view destruction. Unsupported mode/shape, zero
pointer/dimensions or an attached handle returns zero. No C binding is added.

The extension avoids allocating a full-size placeholder on the render owner.
A background-prepared render target imports through a realized 1x1 placeholder;
framebuffer creation then sees its actual dimensions. Owner SRV/RTV creation is
still explicit, bounded SDK work and must be staged/measured. This is not a claim
of zero owner allocations or qualified deadline performance.
