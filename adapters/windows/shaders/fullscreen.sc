$input a_position, a_texcoord0
$output v_texcoord0
// SPDX-License-Identifier: GPL-3.0-or-later
#include <bgfx_shader.sh>
void main() {
    gl_Position = vec4(a_position, 1.0);
    v_texcoord0 = a_texcoord0;
}
