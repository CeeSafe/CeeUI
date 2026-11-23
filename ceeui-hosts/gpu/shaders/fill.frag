// SPDX-License-Identifier: MIT
// metal.c's ffill: a plain fill, the most common instance, in a smaller shader.

layout(location = 0) flat in uint vInst;
layout(location = 1) in vec2 vLocal;
layout(location = 0) out vec4 outColor;

void main() {
    Inst I = insts[vInst];
    vec2 p = gl_FragCoord.xy + pc.target.xy;
    if (p.x < I.clip.x || p.y < I.clip.y || p.x >= I.clip.z || p.y >= I.clip.w) {
        discard;
    }
    float k = I.misc.z > 0.0 ? clipCover(I.misc.z, p) : 1.0;
    outColor = I.c0 * (cover(sdRR(p, I.rect, I.radii)) * k);
}
