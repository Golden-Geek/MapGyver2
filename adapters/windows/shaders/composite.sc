$input v_texcoord0
// SPDX-License-Identifier: GPL-3.0-or-later
#include <bgfx_shader.sh>
SAMPLER2D(s_image, 0);
#include "resample.sh"
uniform vec4 u_layerVisual;
void main() {
    // Every source is already linear-light, premultiplied. Fixed-function
    // ONE / INV_SRC_ALPHA blending implements over without gamma-space math.
    vec4 colour = nearestPixel(gl_FragCoord.xy);
    colour.rgb = clamp(colour.rgb, vec3_splat(0.0), vec3_splat(1.0));
    gl_FragColor = colour * u_layerVisual.x;
}
