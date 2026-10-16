$input v_texcoord0
// SPDX-License-Identifier: GPL-3.0-or-later
#include <bgfx_shader.sh>
SAMPLER2D(s_image, 0);
#include "resample.sh"
uniform vec4 u_encoding;
void main() {
    vec4 colour = nearestPixel(gl_FragCoord.xy);
    vec3 straight = u_encoding.z > 0.5 ? colour.rgb : (colour.a > 0.0 ? colour.rgb / colour.a : vec3_splat(0.0));
    if (u_encoding.x > 0.5) {
        vec3 low = straight * 12.92;
        vec3 high = 1.055 * pow(max(straight, vec3_splat(0.0)), vec3_splat(1.0/2.4)) - 0.055;
        straight = mix(high, low, step(straight, vec3_splat(0.0031308)));
    }
    if (u_encoding.y > 0.5) straight *= colour.a;
    gl_FragColor = vec4(clamp(straight, vec3_splat(0.0), vec3_splat(1.0)), u_encoding.z > 0.5 ? 1.0 : colour.a);
}
