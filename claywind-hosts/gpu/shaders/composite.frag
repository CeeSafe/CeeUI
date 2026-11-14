// SPDX-License-Identifier: MIT
// metal.c's fcomposite: a layer's pixel composited onto its parent's, by raster_effects.cee's 8-bit formulas
// (ThorVG's). The parent's pixel comes from the input attachment that is also the colour attachment; a barrier
// before each composite makes the earlier draws visible to it.

layout(location = 0) flat in uint vInst;
layout(location = 1) in vec2 vLocal;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D layer;
layout(input_attachment_index = 0, set = 2, binding = 0) uniform subpassInput parent;

uvec4 toU(vec4 c) {
    return uvec4(clamp(c, 0.0, 1.0) * 255.0 + 0.5);
}

uint mul8(uint a, uint b) {
    return (a * b + 255u) >> 8;
}

uint ab8(uint c, uint a) {
    return (c * (a + 1u)) >> 8;
}

uint interp8(uint s, uint d, uint a) {
    int delta = (int(s) - int(d)) * int(a);
    int st = delta >= 0 ? delta / 256 : -((-delta + 255) / 256);
    return uint(clamp(int(d) + st, 0, 255));
}

uint packF(precise float x) {
    precise float v = x * 255.0 + 0.5;
    return uint(clamp(v, 0.0, 255.0));
}

float chanF(uint c) {
    return float(c) / 255.0;
}

// Source-over of a premultiplied float colour onto an 8-bit pixel (raster_core.cee's over).
uvec4 overF(uvec4 d, vec4 s) {
    if (s.a >= 0.999 && all(equal(d, uvec4(0u)))) {
        return uvec4(packF(s.r), packF(s.g), packF(s.b), packF(s.a));
    }
    precise float k = 1.0 - s.a;
    precise float r = s.r + chanF(d.r) * k;
    precise float g = s.g + chanF(d.g) * k;
    precise float b = s.b + chanF(d.b) * k;
    precise float a = s.a + chanF(d.a) * k;
    return uvec4(packF(r), packF(g), packF(b), packF(a));
}

// tintPixel: grayscale at ThorVG's tint intensity 254.
uvec4 grayscale(uvec4 p) {
    uint luma = (p.r * 54u + p.g * 182u + p.b * 19u) >> 8;
    uint t = interp8(255u, 0u, luma);
    uvec4 m = uvec4(interp8(t, p.r, 254u), interp8(t, p.g, 254u), interp8(t, p.b, 254u), interp8(255u, p.a, 254u));
    uint k = p.a + 1u;
    return (m * k) >> 8;
}

// brightnessLayer: white (or black) at opacity k over the pixel, alpha kept.
uvec4 brightness(uvec4 p, float v, float k) {
    precise float a = chanF(p.a);
    if (a <= 0.0) {
        return p;
    }
    precise float c = v * a * k;
    precise float ak = a * k;
    return overF(p, vec4(c, c, c, ak));
}

uint blendCh(int mode, uint s, uint d) {
    if (mode == 12 || mode == 3) {
        return s > d ? s - d : d - s;
    }
    if (mode == 13) {
        return uint(clamp(int(s + d) - int(2u * mul8(s, d)), 0, 255));
    }
    if (mode == 2) {
        return min(s + d, 255u);
    }
    if (mode == 4) {
        return s + d - mul8(s, d);
    }
    if (mode == 7) {
        return max(s, d);
    }
    if (mode == 1) {
        return mul8(s, d);
    }
    if (mode == 5) {
        if (d < 128u) {
            return min(2u * mul8(s, d), 255u);
        }
        return 255u - min(2u * mul8(255u - s, 255u - d), 255u);
    }
    if (mode == 6) {
        return min(s, d);
    }
    if (mode == 8) {
        if (d == 0u) {
            return 0u;
        }
        if (s == 255u) {
            return 255u;
        }
        return min(d * 255u / (255u - s), 255u);
    }
    if (mode == 9) {
        if (d == 255u) {
            return 255u;
        }
        if (s == 0u) {
            return 0u;
        }
        return 255u - min((255u - d) * 255u / s, 255u);
    }
    if (mode == 10) {
        if (s < 128u) {
            return min(2u * mul8(s, d), 255u);
        }
        return 255u - min(2u * mul8(255u - s, 255u - d), 255u);
    }
    if (mode == 11) {
        int si = int(s);
        int di = int(d);
        if (s <= 127u) {
            return uint(di - ((255 - 2 * si) * di * (255 - di)) / 65025);
        }
        precise float root = sqrt(float(d) / 255.0) * 255.0;
        int dd = d <= 64u ? 4 * di - (12 * di * di) / 255 + (16 * di * di * di) / 65025 : int(root);
        return uint(clamp(di + ((2 * si - 255) * (dd - di)) / 255, 0, 255));
    }
    return s;
}

uvec4 unpre8(uvec4 p) {
    if (p.a > 0u && p.a < 255u) {
        return uvec4(min(p.rgb * 255u / p.a, uvec3(255u)), p.a);
    }
    return p;
}

uint premixed(uint c1, uint c2, uint a) {
    if (a == 255u) {
        return c1;
    }
    if (a == 0u) {
        return c2;
    }
    return ab8(c1, a) + ab8(c2, 255u - a);
}

int luma8(uvec3 c) {
    return int((c.r * 54u + c.g * 182u + c.b * 19u) >> 8);
}

uvec3 saturate8(uvec3 c, uvec3 s3) {
    uint s = max(s3.x, max(s3.y, s3.z)) - min(s3.x, min(s3.y, s3.z));
    uint v[3] = uint[3](c.x, c.y, c.z);
    int lo = 0;
    int mid = 1;
    int hi = 2;
    int t;
    if (v[lo] > v[mid]) {
        t = lo;
        lo = mid;
        mid = t;
    }
    if (v[mid] > v[hi]) {
        t = mid;
        mid = hi;
        hi = t;
    }
    if (v[lo] > v[mid]) {
        t = lo;
        lo = mid;
        mid = t;
    }
    uint minv = v[lo];
    uint maxv = v[hi];
    if (maxv > minv) {
        v[mid] = (((v[mid] - minv) * s) / (maxv - minv)) & 255u;
        v[hi] = s;
    } else {
        v[mid] = 0u;
        v[hi] = 0u;
    }
    v[lo] = 0u;
    return uvec3(v[0], v[1], v[2]);
}

uvec3 luminance8(uvec3 c, int cl, int l) {
    int d = l - cl;
    int r = int(c.x) + d;
    int g = int(c.y) + d;
    int b = int(c.z) + d;
    int minv = min(r, min(g, b));
    int maxv = max(r, max(g, b));
    if (minv < 0 && l - minv != 0) {
        r = l + ((r - l) * l) / (l - minv);
        g = l + ((g - l) * l) / (l - minv);
        b = l + ((b - l) * l) / (l - minv);
        maxv = max(r, max(g, b));
    }
    if (maxv > 255 && maxv - l != 0) {
        r = l + ((r - l) * (255 - l)) / (maxv - l);
        g = l + ((g - l) * (255 - l)) / (maxv - l);
        b = l + ((b - l) * (255 - l)) / (maxv - l);
    }
    return uvec3(uint(r) & 255u, uint(g) & 255u, uint(b) & 255u);
}

uvec4 blendOp(int mode, uvec4 s, uvec4 dst) {
    if (mode == 12 || mode == 3 || mode == 13 || mode == 2 || mode == 4 || mode == 7) {
        return uvec4(blendCh(mode, s.r, dst.r), blendCh(mode, s.g, dst.g), blendCh(mode, s.b, dst.b), 255u);
    }
    uvec4 o = unpre8(dst);
    uvec3 c = uvec3(blendCh(mode, s.r, o.r), blendCh(mode, s.g, o.g), blendCh(mode, s.b, o.b));
    if (mode == 14) {
        c = saturate8(s.rgb, o.rgb);
        c = luminance8(c, luma8(c), luma8(o.rgb));
    } else if (mode == 15) {
        c = saturate8(o.rgb, s.rgb);
        c = luminance8(c, luma8(c), luma8(o.rgb));
    } else if (mode == 16) {
        c = luminance8(s.rgb, luma8(s.rgb), luma8(o.rgb));
    } else if (mode == 17) {
        c = luminance8(o.rgb, luma8(o.rgb), luma8(s.rgb));
    }
    return uvec4(premixed(c.r, s.r, o.a), premixed(c.g, s.g, o.a), premixed(c.b, s.b, o.a), premixed(255u, s.a, o.a));
}

void main() {
    Inst I = insts[vInst];
    vec2 p = gl_FragCoord.xy + pc.target.xy;
    vec4 dst = subpassLoad(parent);
    uvec4 s = toU(texelFetch(layer, ivec2(p - I.rect.xy), 0));
    int f = int(I.params.z);
    if (f == 2 && s.a != 0u) {
        s = grayscale(s);
    } else if (f == 3) {
        s = brightness(s, 1.0, I.params.w);
    } else if (f == 4) {
        s = brightness(s, 0.0, I.params.w);
    }
    if (all(equal(s, uvec4(0u)))) {
        outColor = dst;
        return;
    }
    uvec4 d = toU(dst);
    float alpha = I.misc.y;
    int mode = int(I.params.y);
    uvec4 res;
    if (mode == 0) {
        precise vec4 sf = vec4(chanF(s.r) * alpha, chanF(s.g) * alpha, chanF(s.b) * alpha, chanF(s.a) * alpha);
        res = overF(d, sf);
    } else {
        uvec4 u = unpre8(s);
        uvec4 b = blendOp(mode, u, d);
        uint k = mul8(uint(clamp(alpha * 255.0 + 0.5, 0.0, 255.0)), s.a);
        res = uvec4(interp8(b.r, d.r, k), interp8(b.g, d.g, k), interp8(b.b, d.b, k), interp8(b.a, d.a, k));
    }
    outColor = vec4(res) / 255.0;
}
