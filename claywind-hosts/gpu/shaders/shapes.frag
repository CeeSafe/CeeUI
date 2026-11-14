// SPDX-License-Identifier: MIT
// metal.c's fmain: every kind of shape (fills, frames, shadows, gradients, masks, images), clipped.

layout(location = 0) flat in uint vInst;
layout(location = 1) in vec2 vLocal;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D atlas; // unnormalized coordinates, linear
layout(set = 0, binding = 3) uniform sampler2D images; // RGBA8, straight alpha; fetched, never sampled

float erfApprox(float x) {
    float x2 = x * x;
    float a = 0.147;
    float v = sqrt(1.0 - exp(-x2 * (1.2732395 + a * x2) / (1.0 + a * x2)));
    return x < 0.0 ? -v : v;
}

// GradientSpread: 0 pad, 1 reflect, 2 repeat (raster_fill.cee's spreadT).
float spreadT(float t, int spread) {
    if (spread == 2) {
        return t - floor(t);
    }
    if (spread == 1) {
        float m = t - floor(t / 2.0) * 2.0;
        return m > 1.0 ? 2.0 - m : m;
    }
    return clamp(t, 0.0, 1.0);
}

// raster_fill.cee's gradientColor: straight colours mixed between the stops around u, then premultiplied.
vec4 gradColor(Inst I, float t) {
    int n = int(I.extra.x);
    float u = spreadT(t, int(I.extra.y));
    vec4 cs[4] = vec4[4](I.c0, I.c1, I.c2, I.c3);
    float ps[4] = float[4](I.stops.x, I.stops.y, I.stops.z, I.stops.w);
    vec4 a = cs[0];
    vec4 b = cs[n - 1];
    float pa = ps[0];
    float pb = ps[n - 1];
    if (u <= ps[0]) {
        b = a;
        pb = pa;
    } else if (u >= ps[n - 1]) {
        a = b;
        pa = pb;
    } else {
        for (int k = 0; k < n - 1; k++) {
            if (u >= ps[k] && u <= ps[k + 1]) {
                a = cs[k];
                b = cs[k + 1];
                pa = ps[k];
                pb = ps[k + 1];
                break;
            }
        }
    }
    float span = pb - pa;
    float f = span > 0.00001 ? (u - pa) / span : 0.0;
    vec4 c = mix(a, b, f);
    return vec4(c.rgb * c.a, c.a);
}

// raster_fill.cee's sampleImage: four texels, premultiplied, mixed bilinearly with the edges clamped, then ThorVG's
// tint (raster_effects.cee's tintPixel) and the opacity. params: the image's place in the image atlas (u, v) and its
// size; c0: the tint; extra.x: tinted; extra.y: the opacity.
vec4 imageTexel(vec4 place, vec2 c) {
    vec4 p = texelFetch(images, ivec2(clamp(c, vec2(0.0), place.zw - 1.0) + place.xy), 0);
    return vec4(p.rgb * p.a, p.a);
}

uint lerp8(uint s, uint d, uint a) {
    int delta = (int(s) - int(d)) * int(a);
    int st = delta >= 0 ? delta / 256 : -((-delta + 255) / 256);
    return uint(clamp(int(d) + st, 0, 255));
}

vec4 imageColor(Inst I, vec2 q) {
    vec2 uv = (q - I.rect.xy) / I.rect.zw * I.params.zw - 0.5;
    vec2 f0 = floor(uv);
    vec2 f = uv - f0;
    vec4 top = mix(imageTexel(I.params, f0), imageTexel(I.params, f0 + vec2(1.0, 0.0)), f.x);
    vec4 bottom = mix(imageTexel(I.params, f0 + vec2(0.0, 1.0)), imageTexel(I.params, f0 + 1.0), f.x);
    vec4 c = mix(top, bottom, f.y);
    if (I.extra.x > 0.0 && c.a > 0.0) {
        uvec4 p = uvec4(clamp(c * 255.0 + 0.5, 0.0, 255.0));
        uint luma = (p.r * 54u + p.g * 182u + p.b * 19u) >> 8;
        uvec3 w = uvec3(round(I.c0.rgb * 255.0));
        uvec3 t = uvec3(lerp8(w.r, 0u, luma), lerp8(w.g, 0u, luma), lerp8(w.b, 0u, luma));
        uvec4 m = uvec4(lerp8(t.r, p.r, 254u), lerp8(t.g, p.g, 254u), lerp8(t.b, p.b, 254u), lerp8(255u, p.a, 254u));
        c = vec4((m * (p.a + 1u)) >> 8) / 255.0;
    }
    return c * I.extra.y;
}

void main() {
    Inst I = insts[vInst];
    vec2 p = gl_FragCoord.xy + pc.target.xy;
    if (p.x < I.clip.x || p.y < I.clip.y || p.x >= I.clip.z || p.y >= I.clip.w) {
        discard;
    }
    float k = I.misc.z > 0.0 ? clipCover(I.misc.z, p) : 1.0;
    bool xf = I.xf1.w != 0.0;
    vec2 q = xf ? vLocal : p;
    float aa = xf ? I.xf1.z : 1.0;
    int kind = int(I.misc.x);
    vec4 color;
    if (kind == 5) {
        vec2 t = q - I.rect.xy + I.params.xy;
        float a = xf ? textureLod(atlas, t, 0.0).r : texelFetch(atlas, ivec2(t), 0).r;
        color = I.c0 * a;
    } else if (kind == 0) {
        color = I.c0 * cover(sdRR(q, I.rect, I.radii) * aa);
    } else if (kind == 1) {
        float o = cover(sdRR(q, I.rect, I.radii) * aa);
        float i = (I.params.z > 0.0 && I.params.w > 0.0) ? cover(sdRR(q, I.params, I.c1) * aa) : 0.0;
        color = I.c0 * max(o - i, 0.0);
    } else if (kind == 2) {
        float d = sdRR(q, I.rect, I.radii);
        float s = I.params.x;
        float a = s > 0.0 ? 0.5 * (1.0 - erfApprox(d / (s * 1.41421356))) : cover(d * aa);
        color = I.c0 * a;
    } else if (kind == 7) {
        color = imageColor(I, q) * cover(sdRR(q, I.rect, I.radii) * aa);
    } else {
        float t = kind == 3 ? dot(q - I.params.xy, I.params.zw) / I.extra.z : length(q - I.params.xy) / I.params.z;
        color = gradColor(I, t) * cover(sdRR(q, I.rect, I.radii) * aa);
    }
    outColor = color * k;
}
