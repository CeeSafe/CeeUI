// SPDX-License-Identifier: MIT
// metal.c's vfull: a quad over the whole target, for blur passes.

void main() {
    int vid = gl_VertexIndex;
    gl_Position = vec4((vid & 1) != 0 ? 1.0 : -1.0, (vid & 2) != 0 ? 1.0 : -1.0, 0.0, 1.0);
}
