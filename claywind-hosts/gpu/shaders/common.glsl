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
    vec4 clipShape;
    vec4 clipRadii;
    vec4 misc;
    vec4 extra;
    vec4 xf0;
    vec4 xf1;
    vec4 clipXf0;
    vec4 clipXf1;
    vec4 clip2Shape;
    vec4 clip2Radii;
    vec4 clip2Xf0;
    vec4 clip2Xf1;
};

layout(std430, set = 0, binding = 0) readonly buffer Instances {
    Inst insts[];
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

// The clip's coverage of frame pixel p: its rounded shape, in its own space when it is transformed.
float clipCover(Inst I, vec2 p) {
    bool xf = I.clipXf1.w != 0.0;
    if (!xf && !any(greaterThan(I.clipRadii, vec4(0.0)))) {
        return 1.0;
    }
    vec2 q = xf ? vec2(I.clipXf0.x * p.x + I.clipXf0.z * p.y + I.clipXf1.x, I.clipXf0.y * p.x + I.clipXf0.w * p.y + I.clipXf1.y)
                : p;
    float k = cover(sdRR(q, I.clipShape, I.clipRadii) * (xf ? I.clipXf1.z : 1.0));
    // The shape it is nested in, if any (scale above 0): the coverages multiply, as Raster's nested masks do.
    if (I.clip2Xf1.z > 0.0) {
        bool xf2 = I.clip2Xf1.w != 0.0;
        vec2 q2 = xf2 ? vec2(I.clip2Xf0.x * p.x + I.clip2Xf0.z * p.y + I.clip2Xf1.x,
                             I.clip2Xf0.y * p.x + I.clip2Xf0.w * p.y + I.clip2Xf1.y)
                      : p;
        k *= cover(sdRR(q2, I.clip2Shape, I.clip2Radii) * (xf2 ? I.clip2Xf1.z : 1.0));
    }
    return k;
}
