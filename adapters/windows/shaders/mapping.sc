$input v_texcoord0
// SPDX-License-Identifier: GPL-3.0-or-later
#include <bgfx_shader.sh>
SAMPLER2D(s_image, 0);
#include "resample.sh"
uniform vec4 u_mappingRows[3];
uniform vec4 u_mappingEdges[4];
uniform vec4 u_mappingIdentity;
uniform vec4 u_surfaceRows[3];
uniform vec4 u_contentRows[3];
uniform vec4 u_crop;
uniform vec4 u_maskBounds;
uniform vec4 u_patchVisual;
void main() {
    vec3 pixel = vec3(gl_FragCoord.xy / u_resample.zw, 1.0);
    vec4 colour;
    if (u_mappingIdentity.x > 0.5) {
        colour = nearestPixel(gl_FragCoord.xy);
    } else {
        for (int i = 0; i < 4; ++i) {
            float coverage = dot(u_mappingEdges[i].xyz, pixel);
            if (coverage < 0.0 || (coverage == 0.0 && u_mappingEdges[i].w < 0.5)) discard;
        }
        float contentW = dot(u_contentRows[2].xyz, pixel);
        if (contentW == 0.0) discard;
        vec2 contentUv = vec2(dot(u_contentRows[0].xyz, pixel), dot(u_contentRows[1].xyz, pixel)) / contentW;
        if (contentUv.x < 0.0 || contentUv.y < 0.0 || contentUv.x >= 1.0 || contentUv.y >= 1.0) discard;
        float w = dot(u_mappingRows[2].xyz, pixel);
        if (w == 0.0) discard;
        vec2 uv = vec2(dot(u_mappingRows[0].xyz, pixel), dot(u_mappingRows[1].xyz, pixel)) / w;
        if (uv.x < 0.0 || uv.y < 0.0 || uv.x >= 1.0 || uv.y >= 1.0) discard;
        uv = u_crop.xy + uv * u_crop.zw;
        ivec2 source = ivec2(min(floor(uv * u_resample.xy), u_resample.xy - vec2_splat(1.0)));
        colour = texelFetch(s_image, source, 0);
    }
    float coverage = 1.0;
    if (u_patchVisual.y > 0.5) {
        float w = dot(u_surfaceRows[2].xyz, pixel);
        if (w == 0.0) discard;
        vec2 uv = vec2(dot(u_surfaceRows[0].xyz, pixel), dot(u_surfaceRows[1].xyz, pixel)) / w;
        vec2 q = (uv - u_maskBounds.xy) / u_maskBounds.zw;
        float distance;
        if (u_patchVisual.y < 1.5) distance = min(min(q.x, 1.0-q.x), min(q.y, 1.0-q.y));
        else distance = (1.0 - length(q * 2.0 - vec2_splat(1.0))) * 0.5;
        if (u_patchVisual.z > 0.0) coverage = clamp(distance / u_patchVisual.z, 0.0, 1.0);
        else if (u_patchVisual.y < 1.5) coverage = q.x >= 0.0 && q.x < 1.0 && q.y >= 0.0 && q.y < 1.0 ? 1.0 : 0.0;
        else coverage = distance >= 0.0 ? 1.0 : 0.0;
        if (u_patchVisual.w > 0.5) coverage = 1.0 - coverage;
    }
    colour.rgb = clamp(colour.rgb, vec3_splat(0.0), vec3_splat(1.0));
    gl_FragColor = colour * (coverage * u_patchVisual.x);
}
