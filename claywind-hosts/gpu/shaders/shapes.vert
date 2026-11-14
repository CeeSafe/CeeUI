// SPDX-License-Identifier: MIT
// metal.c's vmain: an instance's quad, grown by its outset, clipped (when not transformed), transformed.

layout(location = 0) flat out uint vInst;
layout(location = 1) out vec2 vLocal;

void main() {
    uint iid = uint(gl_InstanceIndex);
    Inst I = insts[iid];
    float outset = I.misc.w + 1.0;
    vec4 r = vec4(I.rect.xy - outset, I.rect.xy + I.rect.zw + outset);
    bool xf = I.xf1.w != 0.0;
    if (!xf) {
        r = vec4(max(r.xy, I.clip.xy), min(r.zw, I.clip.zw));
        r.zw = max(r.zw, r.xy);
    }
    int vid = gl_VertexIndex;
    vec2 local = vec2((vid & 1) != 0 ? r.z : r.x, (vid & 2) != 0 ? r.w : r.y);
    vec2 s = xf ? vec2(I.xf0.x * local.x + I.xf0.z * local.y + I.xf1.x, I.xf0.y * local.x + I.xf0.w * local.y + I.xf1.y)
                : local;
    // Vulkan's clip space has y down, like the frame.
    gl_Position = vec4((s.x - pc.target.x) / pc.target.z * 2.0 - 1.0, (s.y - pc.target.y) / pc.target.w * 2.0 - 1.0,
                       0.0, 1.0);
    vInst = iid;
    vLocal = local;
}
