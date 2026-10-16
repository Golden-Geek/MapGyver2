$input v_texcoord0
// SPDX-License-Identifier: GPL-3.0-or-later
#include <bgfx_shader.sh>
SAMPLER2D(s_image, 0);
#include "resample.sh"
uniform vec4 u_layerVisual;
void main() {
    // Every source is already linear-light, premultiplied. Fixed-function
    // ONE / INV_SRC_ALPHA blending implements over without gamma-space math.
    vec4 colour;
    if (u_layerVisual.y < 0.5) colour = nearestPixel(gl_FragCoord.xy);
    else {
        uvec2 pixel = uvec2(gl_FragCoord.xy), sourceSize = uvec2(u_resample.xy), destination = uvec2(u_resample.zw);
        uvec2 source = ((pixel*2u+1u)*sourceSize)/(destination*2u);
        bool wide = sourceSize.x*destination.y > sourceSize.y*destination.x;
        if ((u_layerVisual.y < 1.5 && wide) || (u_layerVisual.y > 1.5 && !wide)) {
            int numerator = (int(pixel.y*2u+1u)-int(destination.y))*int(sourceSize.x)+int(sourceSize.y*destination.x);
            if (numerator < 0 || uint(numerator) >= sourceSize.y*destination.x*2u) discard;
            source.y = uint(numerator)/(destination.x*2u);
        } else {
            int numerator = (int(pixel.x*2u+1u)-int(destination.x))*int(sourceSize.y)+int(sourceSize.x*destination.y);
            if (numerator < 0 || uint(numerator) >= sourceSize.x*destination.y*2u) discard;
            source.x = uint(numerator)/(destination.y*2u);
        }
        colour = texelFetch(s_image, ivec2(source), 0);
    }
    colour.rgb = clamp(colour.rgb, vec3_splat(0.0), vec3_splat(1.0));
    gl_FragColor = colour * u_layerVisual.x;
}
