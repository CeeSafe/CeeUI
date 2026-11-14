/* SPDX-License-Identifier: MIT */
/*
 * ClayWind's Metal renderer (the GPU spike): the instances that gpu.cee's GpuBackend writes. Every shape is a quad
 * whose fragment shader computes coverage from a signed distance: rounded rectangles (fills, borders and rings as an
 * outer minus an inner one), blurred shadows, linear and radial gradients with up to four stops, and glyph and icon
 * masks from a coverage atlas that Raster itself rasterized. A transformed instance carries its affine matrix: the
 * quad is transformed, and its shape is computed in its own space.
 *
 * Layers (group opacity, blend modes, filters) are drawn first, each into its own texture the size of its box, in the
 * order they closed, so a layer holds the layers nested in it. A layer is then composited into its parent by one
 * instance that reads the parent's pixel (framebuffer fetch, Apple GPUs) and applies ThorVG's 8-bit formulas, as
 * raster_effects.cee does: the blend modes, grayscale, brightness and opacity. A blurred layer first goes through
 * ThorVG's box passes, two per axis.
 *
 * Objective-C through objc_msgSend; Metal, QuartzCore and CoreGraphics loaded with dlopen, so the package needs no
 * link flags (as CeeSafe's window runtime does). macOS on Apple silicon only; elsewhere vulkan.c implements the same
 * cw_gpu_* calls (a manifest cannot name a C file per system, so each file compiles to nothing on the others').
 */
#if defined(__APPLE__)
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void *id;
typedef void *SEL;
typedef struct {
    double width, height;
} cw_size;
typedef struct {
    unsigned long x, y, z;
} cw_origin;
typedef struct {
    unsigned long width, height, depth;
} cw_extent;
typedef struct {
    cw_origin origin;
    cw_extent size;
} cw_region;
typedef struct {
    double r, g, b, a;
} cw_clear;

/* The calls gpu.cee makes. */
int32_t cw_gpu_init(uint32_t atlas_width, uint32_t atlas_height, uint32_t images);
int32_t cw_gpu_attach(int32_t kind, uint64_t nswindow, uint64_t nsview);
void cw_gpu_atlas(const uint8_t *pixels, uint32_t stride, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
void cw_gpu_image(const uint32_t *pixels, uint32_t stride, uint32_t w, uint32_t h, uint32_t u, uint32_t v);
int32_t cw_gpu_present(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                         const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count, uint32_t width, uint32_t height, float r, float g,
                         float b);
int32_t cw_gpu_render_to(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                           const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count, uint32_t width, uint32_t height, float r,
                           float g, float b, uint32_t *out);
int32_t cw_gpu_render_only(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                             const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count, uint32_t width, uint32_t height, float r,
                             float g, float b);
double cw_gpu_gpu_ms(void);
void cw_gpu_finish(void);

#define CW_FLOATS 52 /* gpu.cee's FLOATS: 13 float4s per instance */
#define CW_SHAPE 20  /* gpu.cee's SHAPE: 5 float4s per clip shape */
#define CW_RECORD 8  /* gpu.cee's RECORD: a layer's first instance, count, box x0 y0 x1 y1, box radii k0 k1 */
#define CW_KIND 36   /* where an instance keeps its kind (misc.x) */
#define CW_KIND_LAYER 6.0f
#define CW_MAX_LAYERS 256

/* The shader source, in parts that each fit a C string literal; cw_gpu_init joins them. */
static const char *const cw_shader_parts[] = {
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct Inst { float4 rect; float4 radii; float4 c0; float4 c1; float4 c2; float4 c3; float4 stops;\n"
    "              float4 params; float4 clip; float4 misc; float4 extra; float4 xf0; float4 xf1; };\n"
    /* A clip shape: its box and radii, the inverse of its transform (xf0: a b c d; xf1: e f, scale, transformed),
       and the shape it is nested in (link.x, its index plus 1, 0 for none). */
    "struct ClipShape { float4 shape; float4 radii; float4 xf0; float4 xf1; float4 link; };\n"
    "struct VOut { float4 position [[position]]; uint inst [[flat]]; float2 local; };\n"
    /* target: the target's origin in the frame (xy) and its size (zw), in pixels. */
    "vertex VOut vmain(uint vid [[vertex_id]], uint iid [[instance_id]], const device Inst *insts [[buffer(0)]],\n"
    "                  constant float4 &target [[buffer(1)]]) {\n"
    "    const device Inst &I = insts[iid];\n"
    "    float outset = I.misc.w + 1.0;\n"
    "    float4 r = float4(I.rect.xy - outset, I.rect.xy + I.rect.zw + outset);\n"
    "    bool xf = I.xf1.w != 0.0;\n"
    "    if (!xf) { r = float4(max(r.xy, I.clip.xy), min(r.zw, I.clip.zw)); r.zw = max(r.zw, r.xy); }\n"
    "    float2 local = float2((vid & 1) ? r.z : r.x, (vid & 2) ? r.w : r.y);\n"
    "    float2 s = xf ? float2(I.xf0.x * local.x + I.xf0.z * local.y + I.xf1.x,\n"
    "                           I.xf0.y * local.x + I.xf0.w * local.y + I.xf1.y) : local;\n"
    "    VOut o;\n"
    "    o.position = float4((s.x - target.x) / target.z * 2.0 - 1.0, 1.0 - (s.y - target.y) / target.w * 2.0, 0.0, 1.0);\n"
    "    o.inst = iid;\n"
    "    o.local = local;\n"
    "    return o;\n"
    "}\n"
    /* Distance to a rounded rectangle (x, y, w, h) with radii top-left, top-right, bottom-right, bottom-left; y down. */
    "static float sdRR(float2 p, float4 rect, float4 rad) {\n"
    "    float2 hs = rect.zw * 0.5;\n"
    "    float2 q = p - (rect.xy + hs);\n"
    "    float r = q.x < 0.0 ? (q.y < 0.0 ? rad.x : rad.w) : (q.y < 0.0 ? rad.y : rad.z);\n"
    "    float2 d = abs(q) - hs + r;\n"
    "    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - r;\n"
    "}\n"
    /* The share of a pixel inside an edge at distance d from its centre (exact for a straight edge). */
    "static float cover(float d) { return clamp(0.5 - d, 0.0, 1.0); }\n"
    /* The clip's coverage of frame pixel p: its rounded shape, in its own space when it is transformed. */
    /* The clip's coverage of frame pixel p: the product of every shape in its chain, each in its own space, as
       Raster's nested mask clips multiply. `index` is the instance's misc.z: a shape's index plus 1, or 0. */
    "static float clipCover(const device ClipShape *clips, float index, float2 p) {\n"
    "    float k = 1.0;\n"
    "    int i = int(index) - 1;\n"
    "    for (int n = 0; n < 64 && i >= 0; n++) {\n"
    "        const device ClipShape &C = clips[i];\n"
    "        bool xf = C.xf1.w != 0.0;\n"
    "        float2 q = xf ? float2(C.xf0.x * p.x + C.xf0.z * p.y + C.xf1.x, C.xf0.y * p.x + C.xf0.w * p.y + C.xf1.y) : p;\n"
    "        k *= cover(sdRR(q, C.shape, C.radii) * (xf ? C.xf1.z : 1.0));\n"
    "        i = int(C.link.x) - 1;\n"
    "    }\n"
    "    return k;\n"
    "}\n"
    "static float erfApprox(float x) {\n"
    "    float x2 = x * x;\n"
    "    float a = 0.147;\n"
    "    float v = sqrt(1.0 - exp(-x2 * (1.2732395 + a * x2) / (1.0 + a * x2)));\n"
    "    return x < 0.0 ? -v : v;\n"
    "}\n"
    /* GradientSpread: 0 pad, 1 reflect, 2 repeat (raster_fill.cee's spreadT). */
    "static float spreadT(float t, int spread) {\n"
    "    if (spread == 2) { return t - floor(t); }\n"
    "    if (spread == 1) { float m = t - floor(t / 2.0) * 2.0; return m > 1.0 ? 2.0 - m : m; }\n"
    "    return clamp(t, 0.0, 1.0);\n"
    "}\n"
    /* raster_fill.cee's gradientColor: straight colours mixed between the stops around u, then premultiplied. */
    "static float4 gradColor(const device Inst &I, float t) {\n"
    "    int n = int(I.extra.x);\n"
    "    float u = spreadT(t, int(I.extra.y));\n"
    "    float4 cs[4] = { I.c0, I.c1, I.c2, I.c3 };\n"
    "    float ps[4] = { I.stops.x, I.stops.y, I.stops.z, I.stops.w };\n"
    "    float4 a = cs[0]; float4 b = cs[n - 1]; float pa = ps[0]; float pb = ps[n - 1];\n"
    "    if (u <= ps[0]) { b = a; pb = pa; }\n"
    "    else if (u >= ps[n - 1]) { a = b; pa = pb; }\n"
    "    else { for (int k = 0; k < n - 1; k++) { if (u >= ps[k] && u <= ps[k + 1]) {\n"
    "        a = cs[k]; b = cs[k + 1]; pa = ps[k]; pb = ps[k + 1]; break; } } }\n"
    "    float span = pb - pa;\n",
    "    float f = span > 0.00001 ? (u - pa) / span : 0.0;\n"
    "    float4 c = mix(a, b, f);\n"
    "    return float4(c.rgb * c.a, c.a);\n"
    "}\n"
    /* raster_fill.cee's sampleImage: four texels, premultiplied, mixed bilinearly with the edges clamped, then
       ThorVG's tint (raster_effects.cee's tintPixel) and the opacity. params: the image's place in the image atlas
       (u, v) and its size; c0: the tint; extra.x: tinted; extra.y: the opacity. */
    "static float4 imageTexel(texture2d<float> images, float4 place, float2 c) {\n"
    "    float4 p = images.read(uint2(clamp(c, float2(0.0), place.zw - 1.0) + place.xy));\n"
    "    return float4(p.rgb * p.a, p.a);\n"
    "}\n"
    "static uint lerp8(uint s, uint d, uint a) {\n"
    "    int delta = (int(s) - int(d)) * int(a);\n"
    "    int st = delta >= 0 ? delta / 256 : -((-delta + 255) / 256);\n"
    "    return uint(clamp(int(d) + st, 0, 255));\n"
    "}\n"
    "static float4 imageColor(const device Inst &I, texture2d<float> images, float2 q) {\n"
    "    float2 uv = (q - I.rect.xy) / I.rect.zw * I.params.zw - 0.5;\n"
    "    float2 f0 = floor(uv);\n"
    "    float2 f = uv - f0;\n"
    "    float4 top = mix(imageTexel(images, I.params, f0), imageTexel(images, I.params, f0 + float2(1.0, 0.0)), f.x);\n"
    "    float4 bottom = mix(imageTexel(images, I.params, f0 + float2(0.0, 1.0)), imageTexel(images, I.params, f0 + 1.0), f.x);\n"
    "    float4 c = mix(top, bottom, f.y);\n"
    "    if (I.extra.x > 0.0 && c.a > 0.0) {\n"
    "        uint4 p = uint4(clamp(c * 255.0 + 0.5, 0.0, 255.0));\n"
    "        uint luma = (p.r * 54u + p.g * 182u + p.b * 19u) >> 8;\n"
    "        uint3 w = uint3(rint(I.c0.rgb * 255.0));\n"
    "        uint3 t = uint3(lerp8(w.r, 0u, luma), lerp8(w.g, 0u, luma), lerp8(w.b, 0u, luma));\n"
    "        uint4 m = uint4(lerp8(t.r, p.r, 254u), lerp8(t.g, p.g, 254u), lerp8(t.b, p.b, 254u), lerp8(255u, p.a, 254u));\n"
    "        c = float4((m * (p.a + 1u)) >> 8) / 255.0;\n"
    "    }\n"
    "    return c * I.extra.y;\n"
    "}\n"
    "fragment float4 fmain(VOut in [[stage_in]], const device Inst *insts [[buffer(0)]],\n"
    "                      constant float4 &target [[buffer(1)]], const device ClipShape *clips [[buffer(2)]],\n"
    "                      texture2d<float> atlas [[texture(0)]], texture2d<float> images [[texture(2)]]) {\n"
    "    const device Inst &I = insts[in.inst];\n"
    "    float2 p = in.position.xy + target.xy;\n"
    "    if (p.x < I.clip.x || p.y < I.clip.y || p.x >= I.clip.z || p.y >= I.clip.w) { discard_fragment(); }\n"
    "    float k = I.misc.z > 0.0 ? clipCover(clips, I.misc.z, p) : 1.0;\n"
    "    bool xf = I.xf1.w != 0.0;\n"
    "    float2 q = xf ? in.local : p;\n"
    "    float aa = xf ? I.xf1.z : 1.0;\n"
    "    int kind = int(I.misc.x);\n"
    "    float4 color;\n"
    "    if (kind == 5) {\n"
    "        float2 t = q - I.rect.xy + I.params.xy;\n"
    "        float a;\n"
    "        if (xf) {\n"
    "            constexpr sampler s(coord::pixel, filter::linear, address::clamp_to_zero);\n"
    "            a = atlas.sample(s, t).r;\n"
    "        } else {\n"
    "            a = atlas.read(uint2(t)).r;\n"
    "        }\n"
    "        color = I.c0 * a;\n"
    "    } else if (kind == 0) {\n"
    "        color = I.c0 * cover(sdRR(q, I.rect, I.radii) * aa);\n"
    "    } else if (kind == 1) {\n"
    "        float o = cover(sdRR(q, I.rect, I.radii) * aa);\n"
    "        float i = (I.params.z > 0.0 && I.params.w > 0.0) ? cover(sdRR(q, I.params, I.c1) * aa) : 0.0;\n"
    "        color = I.c0 * max(o - i, 0.0);\n"
    "    } else if (kind == 2) {\n"
    "        float d = sdRR(q, I.rect, I.radii);\n"
    "        float s = I.params.x;\n"
    "        float a = s > 0.0 ? 0.5 * (1.0 - erfApprox(d / (s * 1.41421356))) : cover(d * aa);\n"
    "        color = I.c0 * a;\n"
    "    } else if (kind == 7) {\n"
    "        color = imageColor(I, images, q) * cover(sdRR(q, I.rect, I.radii) * aa);\n"
    "    } else {\n"
    "        float t = kind == 3 ? dot(q - I.params.xy, I.params.zw) / I.extra.z\n"
    "                            : length(q - I.params.xy) / I.params.z;\n"
    "        color = gradColor(I, t) * cover(sdRR(q, I.rect, I.radii) * aa);\n"
    "    }\n"
    "    return color * k;\n"
    "}\n"
    /* A plain fill, the most common instance: fmain's kind 0 without a transform, in a smaller shader. */
    "fragment float4 ffill(VOut in [[stage_in]], const device Inst *insts [[buffer(0)]],\n"
    "                      constant float4 &target [[buffer(1)]], const device ClipShape *clips [[buffer(2)]]) {\n"
    "    const device Inst &I = insts[in.inst];\n"
    "    float2 p = in.position.xy + target.xy;\n"
    "    if (p.x < I.clip.x || p.y < I.clip.y || p.x >= I.clip.z || p.y >= I.clip.w) { discard_fragment(); }\n"
    "    float k = I.misc.z > 0.0 ? clipCover(clips, I.misc.z, p) : 1.0;\n"
    "    return I.c0 * (cover(sdRR(p, I.rect, I.radii)) * k);\n"
    "}\n"
    /* ---- layers: raster_effects.cee's 8-bit formulas, ThorVG's ---- */
    "static uint4 toU(float4 c) { return uint4(rint(clamp(c, 0.0, 1.0) * 255.0)); }\n"
    "static uint mul8(uint a, uint b) { return (a * b + 255u) >> 8; }\n"
    "static uint ab8(uint c, uint a) { return (c * (a + 1u)) >> 8; }\n"
    "static uint interp8(uint s, uint d, uint a) {\n"
    "    int delta = (int(s) - int(d)) * int(a);\n"
    "    int st = delta >= 0 ? delta / 256 : -((-delta + 255) / 256);\n"
    "    return uint(clamp(int(d) + st, 0, 255));\n"
    "}\n"
    "static uint packF(float x) { return uint(clamp(x * 255.0 + 0.5, 0.0, 255.0)); }\n"
    "static float chanF(uint c) { return float(c) / 255.0; }\n"
    /* Source-over of a premultiplied float colour onto an 8-bit pixel (raster_core.cee's over). */
    "static uint4 overF(uint4 d, float4 s) {\n"
    "    if (s.a >= 0.999 && all(d == 0u)) { return uint4(packF(s.r), packF(s.g), packF(s.b), packF(s.a)); }\n"
    "    float k = 1.0 - s.a;\n"
    "    return uint4(packF(s.r + chanF(d.r) * k), packF(s.g + chanF(d.g) * k), packF(s.b + chanF(d.b) * k),\n"
    "                 packF(s.a + chanF(d.a) * k));\n"
    "}\n"
    /* tintPixel: grayscale at ThorVG's tint intensity 254. */
    "static uint4 grayscale(uint4 p) {\n"
    "    uint luma = (p.r * 54u + p.g * 182u + p.b * 19u) >> 8;\n",
    "    uint t = interp8(255u, 0u, luma);\n"
    "    uint4 m = uint4(interp8(t, p.r, 254u), interp8(t, p.g, 254u), interp8(t, p.b, 254u), interp8(255u, p.a, 254u));\n"
    "    uint k = p.a + 1u;\n"
    "    return (m * k) >> 8;\n"
    "}\n"
    /* brightnessLayer: white (or black) at opacity k over the pixel, alpha kept. */
    "static uint4 brightness(uint4 p, float v, float k) {\n"
    "    float a = chanF(p.a);\n"
    "    if (a <= 0.0) { return p; }\n"
    "    return overF(p, float4(v * a * k, v * a * k, v * a * k, a * k));\n"
    "}\n"
    "static uint blendCh(int mode, uint s, uint d) {\n"
    "    if (mode == 12 || mode == 3) { return s > d ? s - d : d - s; }\n"
    "    if (mode == 13) { return uint(clamp(int(s + d) - int(2u * mul8(s, d)), 0, 255)); }\n"
    "    if (mode == 2) { return min(s + d, 255u); }\n"
    "    if (mode == 4) { return s + d - mul8(s, d); }\n"
    "    if (mode == 7) { return max(s, d); }\n"
    "    if (mode == 1) { return mul8(s, d); }\n"
    "    if (mode == 5) { if (d < 128u) { return min(2u * mul8(s, d), 255u); }\n"
    "                     return 255u - min(2u * mul8(255u - s, 255u - d), 255u); }\n"
    "    if (mode == 6) { return min(s, d); }\n"
    "    if (mode == 8) { if (d == 0u) { return 0u; } if (s == 255u) { return 255u; }\n"
    "                     return min(d * 255u / (255u - s), 255u); }\n"
    "    if (mode == 9) { if (d == 255u) { return 255u; } if (s == 0u) { return 0u; }\n"
    "                     return 255u - min((255u - d) * 255u / s, 255u); }\n"
    "    if (mode == 10) { if (s < 128u) { return min(2u * mul8(s, d), 255u); }\n"
    "                      return 255u - min(2u * mul8(255u - s, 255u - d), 255u); }\n"
    "    if (mode == 11) {\n"
    "        int si = int(s); int di = int(d);\n"
    "        if (s <= 127u) { return uint(di - ((255 - 2 * si) * di * (255 - di)) / 65025); }\n"
    "        int dd = d <= 64u ? 4 * di - (12 * di * di) / 255 + (16 * di * di * di) / 65025\n"
    "                          : int(precise::sqrt(float(d) / 255.0) * 255.0);\n"
    "        return uint(clamp(di + ((2 * si - 255) * (dd - di)) / 255, 0, 255));\n"
    "    }\n"
    "    return s;\n"
    "}\n"
    "static uint4 unpre8(uint4 p) {\n"
    "    if (p.a > 0u && p.a < 255u) { return uint4(min(p.rgb * 255u / p.a, uint3(255u)), p.a); }\n"
    "    return p;\n"
    "}\n"
    "static uint premixed(uint c1, uint c2, uint a) {\n"
    "    if (a == 255u) { return c1; } if (a == 0u) { return c2; }\n"
    "    return ab8(c1, a) + ab8(c2, 255u - a);\n"
    "}\n"
    "static int luma8(uint3 c) { return int((c.r * 54u + c.g * 182u + c.b * 19u) >> 8); }\n"
    "static uint3 saturate8(uint3 c, uint3 s3) {\n"
    "    uint s = max(s3.x, max(s3.y, s3.z)) - min(s3.x, min(s3.y, s3.z));\n"
    "    uint v[3] = { c.x, c.y, c.z };\n"
    "    int lo = 0; int mid = 1; int hi = 2; int t;\n"
    "    if (v[lo] > v[mid]) { t = lo; lo = mid; mid = t; }\n"
    "    if (v[mid] > v[hi]) { t = mid; mid = hi; hi = t; }\n"
    "    if (v[lo] > v[mid]) { t = lo; lo = mid; mid = t; }\n"
    "    uint minv = v[lo]; uint maxv = v[hi];\n"
    "    if (maxv > minv) { v[mid] = (((v[mid] - minv) * s) / (maxv - minv)) & 255u; v[hi] = s; }\n"
    "    else { v[mid] = 0u; v[hi] = 0u; }\n"
    "    v[lo] = 0u;\n"
    "    return uint3(v[0], v[1], v[2]);\n"
    "}\n"
    "static uint3 luminance8(uint3 c, int cl, int l) {\n"
    "    int d = l - cl;\n"
    "    int r = int(c.x) + d; int g = int(c.y) + d; int b = int(c.z) + d;\n",
    "    int minv = min(r, min(g, b)); int maxv = max(r, max(g, b));\n"
    "    if (minv < 0 && l - minv != 0) {\n"
    "        r = l + ((r - l) * l) / (l - minv); g = l + ((g - l) * l) / (l - minv); b = l + ((b - l) * l) / (l - minv);\n"
    "        maxv = max(r, max(g, b));\n"
    "    }\n"
    "    if (maxv > 255 && maxv - l != 0) {\n"
    "        r = l + ((r - l) * (255 - l)) / (maxv - l); g = l + ((g - l) * (255 - l)) / (maxv - l);\n"
    "        b = l + ((b - l) * (255 - l)) / (maxv - l);\n"
    "    }\n"
    "    return uint3(uint(r) & 255u, uint(g) & 255u, uint(b) & 255u);\n"
    "}\n"
    "static uint4 blendOp(int mode, uint4 s, uint4 dst) {\n"
    "    if (mode == 12 || mode == 3 || mode == 13 || mode == 2 || mode == 4 || mode == 7) {\n"
    "        return uint4(blendCh(mode, s.r, dst.r), blendCh(mode, s.g, dst.g), blendCh(mode, s.b, dst.b), 255u);\n"
    "    }\n"
    "    uint4 o = unpre8(dst);\n"
    "    uint3 c = uint3(blendCh(mode, s.r, o.r), blendCh(mode, s.g, o.g), blendCh(mode, s.b, o.b));\n"
    "    if (mode == 14) { c = saturate8(s.rgb, o.rgb); c = luminance8(c, luma8(c), luma8(o.rgb)); }\n"
    "    else if (mode == 15) { c = saturate8(o.rgb, s.rgb); c = luminance8(c, luma8(c), luma8(o.rgb)); }\n"
    "    else if (mode == 16) { c = luminance8(s.rgb, luma8(s.rgb), luma8(o.rgb)); }\n"
    "    else if (mode == 17) { c = luminance8(o.rgb, luma8(o.rgb), luma8(s.rgb)); }\n"
    "    return uint4(premixed(c.r, s.r, o.a), premixed(c.g, s.g, o.a), premixed(c.b, s.b, o.a), premixed(255u, s.a, o.a));\n"
    "}\n"
    /* A layer's pixel composited onto its parent's (raster_core.cee's popLayer and compositeLayerPixel). */
    "fragment float4 fcomposite(VOut in [[stage_in]], float4 dst [[color(0)]], const device Inst *insts [[buffer(0)]],\n"
    "                           constant float4 &target [[buffer(1)]], texture2d<float, access::read> layer [[texture(1)]]) {\n"
    "    const device Inst &I = insts[in.inst];\n"
    "    float2 p = in.position.xy + target.xy;\n"
    "    uint4 s = toU(layer.read(uint2(p - I.rect.xy)));\n"
    "    int f = int(I.params.z);\n"
    "    if (f == 2 && s.a != 0u) { s = grayscale(s); }\n"
    "    else if (f == 3) { s = brightness(s, 1.0, I.params.w); }\n"
    "    else if (f == 4) { s = brightness(s, 0.0, I.params.w); }\n"
    "    if (all(s == 0u)) { return dst; }\n"
    "    uint4 d = toU(dst);\n"
    "    float alpha = I.misc.y;\n"
    "    int mode = int(I.params.y);\n"
    "    uint4 res;\n"
    "    if (mode == 0) {\n"
    "        res = overF(d, float4(chanF(s.r) * alpha, chanF(s.g) * alpha, chanF(s.b) * alpha, chanF(s.a) * alpha));\n"
    "    } else {\n"
    "        uint4 u = unpre8(s);\n"
    "        uint4 b = blendOp(mode, u, d);\n"
    "        uint k = mul8(uint(clamp(alpha * 255.0 + 0.5, 0.0, 255.0)), s.a);\n"
    "        res = uint4(interp8(b.r, d.r, k), interp8(b.g, d.g, k), interp8(b.b, d.b, k), interp8(b.a, d.a, k));\n"
    "    }\n"
    "    return float4(res) / 255.0;\n"
    "}\n"
    /* ThorVG's box pass (raster_effects.cee's boxPass): the sum of 2d + 1 pixels along an axis, edges extended. */
    "struct FOut { float4 position [[position]]; };\n"
    "vertex FOut vfull(uint vid [[vertex_id]]) {\n"
    "    FOut o;\n"
    "    o.position = float4((vid & 1) ? 1.0 : -1.0, (vid & 2) ? -1.0 : 1.0, 0.0, 1.0);\n"
    "    return o;\n"
    "}\n"
    "fragment float4 fbox(FOut in [[stage_in]], texture2d<float, access::read> src [[texture(0)]],\n"
    "                     constant int4 &box [[buffer(0)]]) {\n",
    "    int2 p = int2(in.position.xy);\n"
    "    int d = box.y;\n"
    "    int end = (box.x == 0 ? box.z : box.w) - 1;\n"
    "    uint4 acc = uint4(0u);\n"
    "    for (int j = -d; j <= d; j++) {\n"
    "        int2 q = p;\n"
    "        if (box.x == 0) { q.x = clamp(p.x + j, 0, end); } else { q.y = clamp(p.y + j, 0, end); }\n"
    "        acc += toU(src.read(uint2(q)));\n"
    "    }\n"
    "    float iarr = 1.0 / float(d + d + 1);\n"
    "    return float4(uint4(float4(acc) * iarr)) / 255.0;\n"
    "}\n",
};

static struct {
    int loaded;
    id (*cls)(const char *);
    SEL (*sel)(const char *);
    void *send;
    void *(*pool_push)(void);
    void (*pool_pop)(void *);
    id (*create_device)(void);
    void *(*color_space_named)(const void *);
    const void **srgb_name;
} cw;

/* Textures for layers, reused between frames by size. */
#define CW_POOL 96
typedef struct {
    id texture;
    uint32_t width, height;
    int used;
} cw_pooled;

static struct {
    id device, queue;
    id fast;    /* the shaders with fast math: shapes */
    id precise; /* without: the layer formulas must give Raster's bytes exactly */
    id shapes_bgra, shapes_rgba;       /* shapes, hardware blending */
    id fills_bgra, fills_rgba;         /* plain fills, hardware blending */
    id composite_bgra, composite_rgba; /* layer composites, framebuffer fetch */
    id box;                            /* blur passes, RGBA8 */
    id atlas;
    id images; /* the image atlas: RGBA8, straight alpha */
    id layer, view, window;
    id offscreen;
    uint32_t offscreen_width, offscreen_height;
    id last; /* the last command buffer committed */
    double gpu_ms;
    cw_pooled pool[CW_POOL];
} m;

#define SEND0(ret) ((ret(*)(id, SEL))cw.send)
#define SEND(ret, ...) ((ret(*)(id, SEL, __VA_ARGS__))cw.send)

static id call(id o, const char *s) { return SEND0(id)(o, cw.sel(s)); }
static id nsstring(const char *text) {
    return SEND(id, const char *)(cw.cls("NSString"), cw.sel("stringWithUTF8String:"), text);
}

static void *symbol(void *handle, const char *name) { return dlsym(handle, name); }

static int load(void) {
    if (cw.loaded) {
        return cw.loaded == 1;
    }
    cw.loaded = -1;
    void *objc = dlopen("/usr/lib/libobjc.A.dylib", RTLD_NOW | RTLD_GLOBAL);
    void *metal = dlopen("/System/Library/Frameworks/Metal.framework/Metal", RTLD_NOW | RTLD_GLOBAL);
    void *quartz = dlopen("/System/Library/Frameworks/QuartzCore.framework/QuartzCore", RTLD_NOW | RTLD_GLOBAL);
    void *graphics = dlopen("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics", RTLD_NOW | RTLD_GLOBAL);
    if (objc == NULL || metal == NULL || quartz == NULL || graphics == NULL) {
        return 0;
    }
    void *p;
    p = symbol(objc, "objc_getClass");
    memcpy(&cw.cls, &p, sizeof p);
    p = symbol(objc, "sel_registerName");
    memcpy(&cw.sel, &p, sizeof p);
    cw.send = symbol(objc, "objc_msgSend");
    p = symbol(objc, "objc_autoreleasePoolPush");
    memcpy(&cw.pool_push, &p, sizeof p);
    p = symbol(objc, "objc_autoreleasePoolPop");
    memcpy(&cw.pool_pop, &p, sizeof p);
    p = symbol(metal, "MTLCreateSystemDefaultDevice");
    memcpy(&cw.create_device, &p, sizeof p);
    p = symbol(graphics, "CGColorSpaceCreateWithName");
    memcpy(&cw.color_space_named, &p, sizeof p);
    cw.srgb_name = (const void **)symbol(graphics, "kCGColorSpaceSRGB");
    if (cw.cls == NULL || cw.sel == NULL || cw.send == NULL || cw.pool_push == NULL || cw.pool_pop == NULL ||
        cw.create_device == NULL) {
        return 0;
    }
    cw.loaded = 1;
    return 1;
}

static void report(const char *what, id error) {
    const char *text = "";
    if (error != NULL) {
        text = SEND0(const char *)(call(error, "localizedDescription"), cw.sel("UTF8String"));
    }
    fprintf(stderr, "metal: %s: %s\n", what, text);
}

static id make_pipeline(id library, const char *vertex, const char *fragment, unsigned long format, int blending) {
    id error = NULL;
    id descriptor = call(call(cw.cls("MTLRenderPipelineDescriptor"), "alloc"), "init");
    SEND(void, id)(descriptor, cw.sel("setVertexFunction:"),
                   SEND(id, id)(library, cw.sel("newFunctionWithName:"), nsstring(vertex)));
    SEND(void, id)(descriptor, cw.sel("setFragmentFunction:"),
                   SEND(id, id)(library, cw.sel("newFunctionWithName:"), nsstring(fragment)));
    id attachment =
        SEND(id, unsigned long)(call(descriptor, "colorAttachments"), cw.sel("objectAtIndexedSubscript:"), 0ul);
    SEND(void, unsigned long)(attachment, cw.sel("setPixelFormat:"), format);
    if (blending) {
        SEND(void, signed char)(attachment, cw.sel("setBlendingEnabled:"), 1);
        /* Premultiplied source over: one, one minus source alpha (MTLBlendFactorOne 1, OneMinusSourceAlpha 5). */
        SEND(void, unsigned long)(attachment, cw.sel("setSourceRGBBlendFactor:"), 1ul);
        SEND(void, unsigned long)(attachment, cw.sel("setSourceAlphaBlendFactor:"), 1ul);
        SEND(void, unsigned long)(attachment, cw.sel("setDestinationRGBBlendFactor:"), 5ul);
        SEND(void, unsigned long)(attachment, cw.sel("setDestinationAlphaBlendFactor:"), 5ul);
    }
    id pipeline =
        SEND(id, id, id *)(m.device, cw.sel("newRenderPipelineStateWithDescriptor:error:"), descriptor, &error);
    if (pipeline == NULL) {
        report(fragment, error);
    }
    call(descriptor, "release");
    return pipeline;
}

/* The shaders compiled with or without fast math. */
static id compile(const char *source, int fast) {
    id options = call(call(cw.cls("MTLCompileOptions"), "alloc"), "init");
    SEND(void, signed char)(options, cw.sel("setFastMathEnabled:"), fast ? 1 : 0);
    id error = NULL;
    id library = SEND(id, id, id, id *)(m.device, cw.sel("newLibraryWithSource:options:error:"), nsstring(source),
                                        options, &error);
    call(options, "release");
    if (library == NULL) {
        report("the shaders", error);
    }
    return library;
}

int32_t cw_gpu_init(uint32_t atlas_width, uint32_t atlas_height, uint32_t images) {
    if (!load()) {
        return 1;
    }
    void *pool = cw.pool_push();
    m.device = cw.create_device();
    if (m.device == NULL) {
        cw.pool_pop(pool);
        return 2;
    }
    m.queue = call(m.device, "newCommandQueue");
    static char source[32768];
    source[0] = '\0';
    for (size_t k = 0; k < sizeof cw_shader_parts / sizeof cw_shader_parts[0]; k++) {
        strncat(source, cw_shader_parts[k], sizeof source - strlen(source) - 1);
    }
    m.fast = compile(source, 1);
    m.precise = compile(source, 0);
    if (m.fast == NULL || m.precise == NULL) {
        cw.pool_pop(pool);
        return 3;
    }
    m.shapes_bgra = make_pipeline(m.fast, "vmain", "fmain", 80ul, 1); /* MTLPixelFormatBGRA8Unorm */
    m.shapes_rgba = make_pipeline(m.fast, "vmain", "fmain", 70ul, 1); /* MTLPixelFormatRGBA8Unorm */
    m.fills_bgra = make_pipeline(m.fast, "vmain", "ffill", 80ul, 1);
    m.fills_rgba = make_pipeline(m.fast, "vmain", "ffill", 70ul, 1);
    m.composite_bgra = make_pipeline(m.precise, "vmain", "fcomposite", 80ul, 0);
    m.composite_rgba = make_pipeline(m.precise, "vmain", "fcomposite", 70ul, 0);
    m.box = make_pipeline(m.precise, "vfull", "fbox", 70ul, 0);
    /* The coverage atlas: R8Unorm (10), read and sampled by the fragment shader. */
    id descriptor = SEND(id, unsigned long, unsigned long, unsigned long, signed char)(
        cw.cls("MTLTextureDescriptor"), cw.sel("texture2DDescriptorWithPixelFormat:width:height:mipmapped:"), 10ul,
        (unsigned long)atlas_width, (unsigned long)atlas_height, 0);
    SEND(void, unsigned long)(descriptor, cw.sel("setUsage:"), 1ul); /* ShaderRead */
    m.atlas = SEND(id, id)(m.device, cw.sel("newTextureWithDescriptor:"), descriptor);
    /* The image atlas: RGBA8Unorm (70), read by the fragment shader. */
    descriptor = SEND(id, unsigned long, unsigned long, unsigned long, signed char)(
        cw.cls("MTLTextureDescriptor"), cw.sel("texture2DDescriptorWithPixelFormat:width:height:mipmapped:"), 70ul,
        (unsigned long)images, (unsigned long)images, 0);
    SEND(void, unsigned long)(descriptor, cw.sel("setUsage:"), 1ul); /* ShaderRead */
    m.images = SEND(id, id)(m.device, cw.sel("newTextureWithDescriptor:"), descriptor);
    cw.pool_pop(pool);
    return m.shapes_bgra != NULL && m.shapes_rgba != NULL && m.fills_bgra != NULL && m.fills_rgba != NULL &&
                   m.composite_bgra != NULL && m.composite_rgba != NULL &&
                   m.box != NULL && m.atlas != NULL && m.images != NULL
               ? 0
               : 4;
}

/* The window to draw into: WindowHandle's kind and handles; Metal takes AppKit's NSWindow and NSView (kind 1). */
int32_t cw_gpu_attach(int32_t kind, uint64_t nswindow, uint64_t nsview) {
    if (m.device == NULL || kind != 1) {
        return 1;
    }
    void *pool = cw.pool_push();
    m.window = (id)(uintptr_t)nswindow;
    m.view = (id)(uintptr_t)nsview;
    m.layer = call(call(cw.cls("CAMetalLayer"), "alloc"), "init");
    SEND(void, id)(m.layer, cw.sel("setDevice:"), m.device);
    SEND(void, unsigned long)(m.layer, cw.sel("setPixelFormat:"), 80ul);
    /* Not framebuffer-only: a layer composite reads the drawable's pixels. */
    SEND(void, signed char)(m.layer, cw.sel("setFramebufferOnly:"), 0);
    /* sRGB, as the window and Raster's pixels are: no conversion on the way to the display. */
    if (cw.color_space_named != NULL && cw.srgb_name != NULL) {
        SEND(void, void *)(m.layer, cw.sel("setColorspace:"), cw.color_space_named(*cw.srgb_name));
    }
    SEND(void, id)(m.view, cw.sel("setLayer:"), m.layer);
    SEND(void, signed char)(m.view, cw.sel("setWantsLayer:"), 1);
    cw.pool_pop(pool);
    return 0;
}

/* Waits for the GPU to finish the last frame, and records its time. */
void cw_gpu_finish(void) {
    if (m.last == NULL) {
        return;
    }
    call(m.last, "waitUntilCompleted");
    double start = SEND0(double)(m.last, cw.sel("GPUStartTime"));
    double end = SEND0(double)(m.last, cw.sel("GPUEndTime"));
    m.gpu_ms = (end - start) * 1000.0;
    call(m.last, "release");
    m.last = NULL;
}

double cw_gpu_gpu_ms(void) { return m.gpu_ms; }

/* Copies the atlas rows [y0, y1) and columns [x0, x1) from `pixels` (a `stride`-wide R8 image) to the texture. */
void cw_gpu_atlas(const uint8_t *pixels, uint32_t stride, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    if (m.atlas == NULL || x1 <= x0 || y1 <= y0) {
        return;
    }
    cw_gpu_finish(); /* the last frame may still read the texture */
    cw_region region = {{x0, y0, 0}, {x1 - x0, y1 - y0, 1}};
    SEND(void, cw_region, unsigned long, const void *, unsigned long)(
        m.atlas, cw.sel("replaceRegion:mipmapLevel:withBytes:bytesPerRow:"), region, 0ul,
        pixels + (size_t)y0 * stride + x0, (unsigned long)stride);
}

/* Copies a `w` × `h` image (rows `stride` pixels apart, RGBA8) to (u, v) in the image atlas. */
void cw_gpu_image(const uint32_t *pixels, uint32_t stride, uint32_t w, uint32_t h, uint32_t u, uint32_t v) {
    if (m.images == NULL || w == 0 || h == 0) {
        return;
    }
    cw_gpu_finish(); /* the last frame may still read the texture */
    cw_region region = {{u, v, 0}, {w, h, 1}};
    SEND(void, cw_region, unsigned long, const void *, unsigned long)(
        m.images, cw.sel("replaceRegion:mipmapLevel:withBytes:bytesPerRow:"), region, 0ul, pixels,
        (unsigned long)stride * 4ul);
}

/* An RGBA8 texture of this size from the pool, for a layer or a blur pass. */
static id pooled(uint32_t width, uint32_t height) {
    int free_slot = -1;
    for (int k = 0; k < CW_POOL; k++) {
        if (m.pool[k].texture != NULL && !m.pool[k].used && m.pool[k].width == width && m.pool[k].height == height) {
            m.pool[k].used = 1;
            return m.pool[k].texture;
        }
        if (m.pool[k].texture == NULL && free_slot < 0) {
            free_slot = k;
        }
    }
    if (free_slot < 0) { /* the pool is full: drop an unused texture of another size */
        for (int k = 0; k < CW_POOL; k++) {
            if (!m.pool[k].used) {
                call(m.pool[k].texture, "release");
                m.pool[k].texture = NULL;
                free_slot = k;
                break;
            }
        }
        if (free_slot < 0) {
            return NULL;
        }
    }
    id descriptor = SEND(id, unsigned long, unsigned long, unsigned long, signed char)(
        cw.cls("MTLTextureDescriptor"), cw.sel("texture2DDescriptorWithPixelFormat:width:height:mipmapped:"), 70ul,
        (unsigned long)width, (unsigned long)height, 0);
    SEND(void, unsigned long)(descriptor, cw.sel("setUsage:"), 4ul | 1ul); /* RenderTarget | ShaderRead */
    SEND(void, unsigned long)(descriptor, cw.sel("setStorageMode:"), 2ul); /* Private */
    m.pool[free_slot].texture = SEND(id, id)(m.device, cw.sel("newTextureWithDescriptor:"), descriptor);
    m.pool[free_slot].width = width;
    m.pool[free_slot].height = height;
    m.pool[free_slot].used = 1;
    return m.pool[free_slot].texture;
}

static id begin_pass(id buffer, id target, int clear, float r, float g, float b, float a) {
    id pass = call(cw.cls("MTLRenderPassDescriptor"), "renderPassDescriptor");
    id color = SEND(id, unsigned long)(call(pass, "colorAttachments"), cw.sel("objectAtIndexedSubscript:"), 0ul);
    SEND(void, id)(color, cw.sel("setTexture:"), target);
    SEND(void, unsigned long)(color, cw.sel("setLoadAction:"), clear ? 2ul : 0ul); /* Clear, DontCare */
    SEND(void, unsigned long)(color, cw.sel("setStoreAction:"), 1ul);              /* Store */
    cw_clear c = {r, g, b, a};
    SEND(void, cw_clear)(color, cw.sel("setClearColor:"), c);
    return SEND(id, id)(buffer, cw.sel("renderCommandEncoderWithDescriptor:"), pass);
}

static void draw_range(id encoder, id pipeline, uint32_t base, uint32_t count) {
    if (count == 0) {
        return;
    }
    SEND(void, id)(encoder, cw.sel("setRenderPipelineState:"), pipeline);
    /* A triangle strip of 4 vertices per instance (MTLPrimitiveTypeTriangleStrip 4); instance ids start at base. */
    SEND(void, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long)(
        encoder, cw.sel("drawPrimitives:vertexStart:vertexCount:instanceCount:baseInstance:"), 4ul, 0ul, 4ul,
        (unsigned long)count, (unsigned long)base);
}

/* How an instance is drawn: 0 by fmain, 1 by ffill (a plain fill), 2 as a layer composite. */
static int pipeline_class(const float *instance) {
    if (instance[CW_KIND] == CW_KIND_LAYER) {
        return 2;
    }
    return instance[CW_KIND] == 0.0f && instance[51] == 0.0f ? 1 : 0; /* kind 0, xf1.w 0 */
}

/* Draws instances [first, first + count) of `data` into the target of `encoder`, whose box in the frame is `box`:
   shapes and plain fills in runs, each with its pipeline, and each layer composite on its own with its texture. */
static void draw_target(id encoder, id shapes, id fills, id composite, id buffer, id clips, const float *data,
                        uint32_t first, uint32_t count, const float box[4], id *textures) {
    SEND(void, id, unsigned long, unsigned long)(encoder, cw.sel("setVertexBuffer:offset:atIndex:"), buffer, 0ul, 0ul);
    SEND(void, const void *, unsigned long, unsigned long)(encoder, cw.sel("setVertexBytes:length:atIndex:"), box,
                                                           4 * sizeof(float), 1ul);
    SEND(void, id, unsigned long, unsigned long)(encoder, cw.sel("setFragmentBuffer:offset:atIndex:"), buffer, 0ul,
                                                 0ul);
    SEND(void, id, unsigned long, unsigned long)(encoder, cw.sel("setFragmentBuffer:offset:atIndex:"), clips, 0ul, 2ul);
    SEND(void, const void *, unsigned long, unsigned long)(encoder, cw.sel("setFragmentBytes:length:atIndex:"), box,
                                                           4 * sizeof(float), 1ul);
    SEND(void, id, unsigned long)(encoder, cw.sel("setFragmentTexture:atIndex:"), m.atlas, 0ul);
    SEND(void, id, unsigned long)(encoder, cw.sel("setFragmentTexture:atIndex:"), m.images, 2ul);
    uint32_t k = first, end = first + count;
    while (k < end) {
        int kind = pipeline_class(data + (size_t)k * CW_FLOATS);
        if (kind == 2) {
            int layer = (int)data[(size_t)k * CW_FLOATS + 28]; /* params.x */
            if (layer >= 0 && layer < CW_MAX_LAYERS && textures[layer] != NULL) {
                SEND(void, id, unsigned long)(encoder, cw.sel("setFragmentTexture:atIndex:"), textures[layer], 1ul);
                draw_range(encoder, composite, k, 1);
            }
            k++;
            continue;
        }
        uint32_t run = k + 1;
        while (run < end && pipeline_class(data + (size_t)run * CW_FLOATS) == kind) {
            run++;
        }
        draw_range(encoder, kind == 1 ? fills : shapes, k, run - k);
        k = run;
    }
}

/* One box pass of radius d along `axis` (0 rows, 1 columns) from `source` into `target`, both w × h. */
static void box_pass(id buffer, id source, id target, int axis, int d, uint32_t w, uint32_t h) {
    id encoder = begin_pass(buffer, target, 0, 0, 0, 0, 0);
    int box[4] = {axis, d, (int)w, (int)h};
    SEND(void, id)(encoder, cw.sel("setRenderPipelineState:"), m.box);
    SEND(void, id, unsigned long)(encoder, cw.sel("setFragmentTexture:atIndex:"), source, 0ul);
    SEND(void, const void *, unsigned long, unsigned long)(encoder, cw.sel("setFragmentBytes:length:atIndex:"), box,
                                                           sizeof box, 0ul);
    SEND(void, unsigned long, unsigned long, unsigned long)(encoder, cw.sel("drawPrimitives:vertexStart:vertexCount:"),
                                                            4ul, 0ul, 4ul);
    call(encoder, "endEncoding");
}

/* A buffer holding `count` items of `floats` floats each (one zeroed item when there are none). */
static id buffer_of(const float *data, uint32_t count, uint32_t floats) {
    static const float empty[CW_FLOATS] = {0};
    if (count == 0) {
        data = empty;
        count = 1;
    }
    return SEND(id, const void *, unsigned long, unsigned long)(m.device, cw.sel("newBufferWithBytes:length:options:"),
                                                                data, (unsigned long)count * floats * sizeof(float),
                                                                0ul);
}

/* Encodes a whole frame: each layer in the order it closed, its blur, then the frame into `target`. */
static void encode(id target, int bgra, const float *frame, uint32_t count, const float *layers,
                   uint32_t layer_instances, const float *records, uint32_t layer_count, const float *clips,
                   uint32_t clip_count, uint32_t width,
                   uint32_t height, float r, float g, float b, id drawable) {
    for (int k = 0; k < CW_POOL; k++) {
        m.pool[k].used = 0;
    }
    id buffer = call(m.queue, "commandBuffer");
    id frame_data = buffer_of(frame, count, CW_FLOATS);
    id layer_data = buffer_of(layers, layer_instances, CW_FLOATS);
    id clip_data = buffer_of(clips, clip_count, CW_SHAPE);
    id textures[CW_MAX_LAYERS];
    uint32_t n = layer_count < CW_MAX_LAYERS ? layer_count : CW_MAX_LAYERS;
    for (uint32_t l = 0; l < n; l++) {
        const float *rec = records + (size_t)l * CW_RECORD;
        textures[l] = NULL;
        uint32_t x0 = (uint32_t)rec[2], y0 = (uint32_t)rec[3], x1 = (uint32_t)rec[4], y1 = (uint32_t)rec[5];
        if (x1 <= x0 || y1 <= y0) {
            continue;
        }
        uint32_t w = x1 - x0, h = y1 - y0;
        id texture = pooled(w, h);
        if (texture == NULL) {
            continue;
        }
        float box[4] = {(float)x0, (float)y0, (float)w, (float)h};
        id encoder = begin_pass(buffer, texture, 1, 0, 0, 0, 0);
        draw_target(encoder, m.shapes_rgba, m.fills_rgba, m.composite_rgba, layer_data, clip_data, layers, (uint32_t)rec[0],
                    (uint32_t)rec[1], box, textures);
        call(encoder, "endEncoding");
        int k0 = (int)rec[6], k1 = (int)rec[7];
        if (k0 > 0 || k1 > 0) {
            id other = pooled(w, h);
            if (other != NULL) {
                id from = texture, to = other;
                for (int axis = 0; axis < 2; axis++) {
                    for (int level = 0; level < 2; level++) {
                        int d = level == 0 ? k0 : k1;
                        if (d <= 0) {
                            continue;
                        }
                        box_pass(buffer, from, to, axis, d, w, h);
                        id t = from;
                        from = to;
                        to = t;
                    }
                }
                texture = from;
            }
        }
        textures[l] = texture;
    }
    float whole[4] = {0.0f, 0.0f, (float)width, (float)height};
    id encoder = begin_pass(buffer, target, 1, r, g, b, 1.0f);
    draw_target(encoder, bgra ? m.shapes_bgra : m.shapes_rgba, bgra ? m.fills_bgra : m.fills_rgba,
                bgra ? m.composite_bgra : m.composite_rgba, frame_data, clip_data, frame, 0, count, whole, textures);
    call(encoder, "endEncoding");
    if (drawable != NULL) {
        SEND(void, id)(buffer, cw.sel("presentDrawable:"), drawable);
    }
    call(buffer, "commit");
    call(frame_data, "release");
    call(layer_data, "release");
    call(clip_data, "release");
    m.last = call(buffer, "retain");
}

/* Draws the frame into the window's next drawable, `width` × `height` pixels, and presents it. */
int32_t cw_gpu_present(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                         const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count, uint32_t width, uint32_t height, float r, float g,
                         float b) {
    if (m.layer == NULL) {
        return 1;
    }
    cw_gpu_finish();
    void *pool = cw.pool_push();
    double scale = SEND0(double)(m.window, cw.sel("backingScaleFactor"));
    SEND(void, double)(m.layer, cw.sel("setContentsScale:"), scale);
    cw_size size = {(double)width, (double)height};
    SEND(void, cw_size)(m.layer, cw.sel("setDrawableSize:"), size);
    id drawable = call(m.layer, "nextDrawable");
    if (drawable == NULL) {
        cw.pool_pop(pool);
        return 2;
    }
    encode(call(drawable, "texture"), 1, frame, count, layers, layer_instances, records, layer_count, clips, clip_count, width, height,
           r, g, b, drawable);
    cw.pool_pop(pool);
    return 0;
}

/* Draws the frame into an offscreen texture and copies its pixels to `out` (RGBA bytes, Raster's order). */
int32_t cw_gpu_render_to(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                           const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count, uint32_t width, uint32_t height, float r,
                           float g, float b, uint32_t *out) {
    if (m.device == NULL) {
        return 1;
    }
    cw_gpu_finish();
    void *pool = cw.pool_push();
    if (m.offscreen == NULL || m.offscreen_width != width || m.offscreen_height != height) {
        if (m.offscreen != NULL) {
            call(m.offscreen, "release");
        }
        id descriptor = SEND(id, unsigned long, unsigned long, unsigned long, signed char)(
            cw.cls("MTLTextureDescriptor"), cw.sel("texture2DDescriptorWithPixelFormat:width:height:mipmapped:"), 70ul,
            (unsigned long)width, (unsigned long)height, 0);
        SEND(void, unsigned long)(descriptor, cw.sel("setUsage:"), 4ul | 1ul); /* RenderTarget | ShaderRead */
        SEND(void, unsigned long)(descriptor, cw.sel("setStorageMode:"), 0ul); /* Shared */
        m.offscreen = SEND(id, id)(m.device, cw.sel("newTextureWithDescriptor:"), descriptor);
        m.offscreen_width = width;
        m.offscreen_height = height;
    }
    encode(m.offscreen, 0, frame, count, layers, layer_instances, records, layer_count, clips, clip_count, width, height, r, g, b, NULL);
    cw_gpu_finish();
    if (out != NULL) {
        cw_region region = {{0, 0, 0}, {width, height, 1}};
        SEND(void, void *, unsigned long, cw_region, unsigned long)(
            m.offscreen, cw.sel("getBytes:bytesPerRow:fromRegion:mipmapLevel:"), out, (unsigned long)width * 4ul,
            region, 0ul);
    }
    cw.pool_pop(pool);
    return 0;
}

/* Draws the frame offscreen and waits for it, reading nothing back: for timing the GPU. */
int32_t cw_gpu_render_only(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                             const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count, uint32_t width, uint32_t height, float r,
                             float g, float b) {
    return cw_gpu_render_to(frame, count, layers, layer_instances, records, layer_count, clips, clip_count, width, height, r, g, b,
                              NULL);
}
#else
typedef int cw_metal_absent; /* not macOS: vulkan.c draws */
#endif
