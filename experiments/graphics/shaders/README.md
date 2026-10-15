# Pinned experiment shaders

The two DX11 binaries and matching main shader sources are unmodified bgfx
examples, copied from the exact archive in provenance.json. LICENSE.bgfx and
the source copyright notices are retained. The full corresponding source,
includes and shader compiler sources are in that pinned archive; these files
are not newly authored MapGyver shaders and do not imply a license exception.

The vertex shader output hash matches the fragment shader input hash. The
fragment shader samples s_color without color conversion. The build verifies
SHA256s and embeds the binaries; no runtime disk read or shader compilation is
part of the experiment's draw. This pins vendor binaries rather than claiming
they were rebuilt bit-for-bit with a known historical D3D compiler. Production
shader build/source materials must be qualified in their own milestone.
