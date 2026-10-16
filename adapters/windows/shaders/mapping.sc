$input v_texcoord0
// SPDX-License-Identifier: GPL-3.0-or-later
#include <bgfx_shader.sh>
SAMPLER2D(s_image, 0);
#include "resample.sh"
uniform vec4 u_mappingRows[3];
uniform vec4 u_mappingEdges[4];
uniform vec4 u_mappingIdentity;
void main() {
    if (u_mappingIdentity.x > 0.5) {
        gl_FragColor = nearestPixel(gl_FragCoord.xy);
    } else {
        vec3 pixel = vec3(gl_FragCoord.xy / u_resample.zw, 1.0);
        for (int i = 0; i < 4; ++i) {
            float coverage = dot(u_mappingEdges[i].xyz, pixel);
            if (coverage < 0.0 || (coverage == 0.0 && u_mappingEdges[i].w < 0.5)) discard;
        }
        float w = dot(u_mappingRows[2].xyz, pixel);
        if (w == 0.0) discard;
        vec2 uv = vec2(dot(u_mappingRows[0].xyz, pixel), dot(u_mappingRows[1].xyz, pixel)) / w;
        if (uv.x < 0.0 || uv.y < 0.0 || uv.x >= 1.0 || uv.y >= 1.0) discard;
        ivec2 source = ivec2(min(floor(uv * u_resample.xy), u_resample.xy - vec2_splat(1.0)));
        gl_FragColor = texelFetch(s_image, source, 0);
    }
}
