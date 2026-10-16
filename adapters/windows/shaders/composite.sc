$input v_texcoord0
// SPDX-License-Identifier: GPL-3.0-or-later
#include <bgfx_shader.sh>
SAMPLER2D(s_image, 0);
void main() {
    // Every source is already linear-light, premultiplied. Fixed-function
    // ONE / INV_SRC_ALPHA blending implements over without gamma-space math.
    gl_FragColor = texture2D(s_image, v_texcoord0);
}
