// SPDX-License-Identifier: GPL-3.0-or-later
uniform vec4 u_resample;
vec4 nearestPixel(vec2 coordinate) {
    // Integer pixel-centre mapping agrees with the portable reference even at
    // exact half-texel ties. Interpolated UVs can round to the adjacent texel.
    uvec2 pixel = uvec2(coordinate);
    uvec2 source = ((pixel * 2u + 1u) * uvec2(u_resample.xy)) / (uvec2(u_resample.zw) * 2u);
    return texelFetch(s_image, ivec2(source), 0);
}
