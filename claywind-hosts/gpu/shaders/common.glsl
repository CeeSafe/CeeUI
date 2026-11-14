// SPDX-License-Identifier: MIT
// What every Vulkan shader shares: the instance layout (gpu.cee's FLOATS, metal.c's struct Inst) and the push
// constants. build.py puts "#version 450" and this file in front of each stage.

struct Inst {
    vec4 rect;
    vec4 radii;
    vec4 c0;
    vec4 c1;
    vec4 c2;
    vec4 c3;
    vec4 stops;
    vec4 params;
    vec4 clip;
    vec4 misc;
    vec4 extra;
    vec4 xf0;
    vec4 xf1;
};

// A clip shape (gpu.cee's SHAPE): its box and radii, the inverse of its transform (xf0: a b c d; xf1: e f, scale,
// transformed), and the shape it is nested in (link.x, its index plus 1, 0 for none).
struct ClipShape {
    vec4 shape;
    vec4 radii;
    vec4 xf0;
    vec4 xf1;
    vec4 link;
};

layout(std430, set = 0, binding = 0) readonly buffer Instances {
    Inst insts[];
};

layout(std430, set = 0, binding = 2) readonly buffer Clips {
    ClipShape clips[];
};

// target: the target's origin in the frame (xy) and its size (zw), in pixels; box: a blur pass's axis, radius,
// width and height.
layout(push_constant) uniform Push {
    vec4 target;
    ivec4 box;
} pc;

// Distance to a rounded rectangle (x, y, w, h) with radii top-left, top-right, bottom-right, bottom-left; y down.
float sdRR(vec2 p, vec4 rect, vec4 rad) {
    vec2 hs = rect.zw * 0.5;
    vec2 q = p - (rect.xy + hs);
    float r = q.x < 0.0 ? (q.y < 0.0 ? rad.x : rad.w) : (q.y < 0.0 ? rad.y : rad.z);
    vec2 d = abs(q) - hs + r;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - r;
}

// The share of a pixel inside an edge at distance d from its centre (exact for a straight edge).
float cover(float d) {
    return clamp(0.5 - d, 0.0, 1.0);
}

// The clip's coverage of frame pixel p: the product of every shape in its chain, each in its own space, as Raster's
// nested mask clips multiply. `index` is the instance's misc.z: a shape's index plus 1, or 0.
float clipCover(float index, vec2 p) {
    float k = 1.0;
    int i = int(index) - 1;
    for (int n = 0; n < 64 && i >= 0; n++) {
        ClipShape C = clips[i];
        bool xf = C.xf1.w != 0.0;
        vec2 q = xf ? vec2(C.xf0.x * p.x + C.xf0.z * p.y + C.xf1.x, C.xf0.y * p.x + C.xf0.w * p.y + C.xf1.y) : p;
        k *= cover(sdRR(q, C.shape, C.radii) * (xf ? C.xf1.z : 1.0));
        i = int(C.link.x) - 1;
    }
    return k;
}
