/* SPDX-License-Identifier: MIT */
/*
 * ClayWind's Direct3D 12 renderer, Windows's default (CLAYWIND_GPU=vulkan chooses vulkan.c instead, and it is the
 * fallback when Direct3D 12 cannot start). The cw_gpu_* calls of metal.c and vulkan.c: the same instances, layer
 * records and clip shapes, drawn by the same shaders in HLSL, compiled when the program starts by Windows's own
 * d3dcompiler_47.dll (the layer formulas with IEEE strictness, so they give Raster's bytes):
 *
 * - each layer, in the order it closed, into its own texture, then its blur's box passes;
 * - the frame, into the window's swap chain or an offscreen texture that is read back.
 *
 * Direct3D 12 has no framebuffer fetch on every GPU, so before each layer composite the region it covers is copied
 * from the target into a scratch texture, which the composite reads as the parent's pixels.
 *
 * d3d12.dll, dxgi.dll and d3dcompiler_47.dll are loaded with LoadLibrary, so the package needs no link flags. The
 * device is the high-performance adapter (a discrete GPU, as vulkan.c chooses), else the default one, else WARP, Windows's software rasterizer (CLAYWIND_D3D12_WARP=1 asks for
 * it). Windows only; elsewhere this file compiles to nothing (a manifest cannot name a C file per system).
 */
#if defined(_WIN32)
#define COBJMACROS
#include <initguid.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* The calls gpu.cee and main.cee make. */
int32_t cw_gpu_init(uint32_t atlas_width, uint32_t atlas_height);
int32_t cw_gpu_attach(int32_t kind, uint64_t first, uint64_t second);
void cw_gpu_atlas(const uint8_t *pixels, uint32_t stride, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
int32_t cw_gpu_present(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                       const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count,
                       uint32_t width, uint32_t height, float r, float g, float b);
int32_t cw_gpu_render_to(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                         const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count,
                         uint32_t width, uint32_t height, float r, float g, float b, uint32_t *out);
int32_t cw_gpu_render_only(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                           const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count,
                           uint32_t width, uint32_t height, float r, float g, float b);
double cw_gpu_gpu_ms(void);
void cw_gpu_finish(void);

/* vulkan.c's, under their Windows names. */
int32_t cw_vk_init(uint32_t atlas_width, uint32_t atlas_height);
int32_t cw_vk_attach(int32_t kind, uint64_t first, uint64_t second);
void cw_vk_atlas(const uint8_t *pixels, uint32_t stride, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
int32_t cw_vk_present(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                      const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count,
                      uint32_t width, uint32_t height, float r, float g, float b);
int32_t cw_vk_render_to(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                        const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count,
                        uint32_t width, uint32_t height, float r, float g, float b, uint32_t *out);
int32_t cw_vk_render_only(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                          const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count,
                          uint32_t width, uint32_t height, float r, float g, float b);
double cw_vk_gpu_ms(void);
void cw_vk_finish(void);

#define CW_FLOATS 52 /* gpu.cee's FLOATS: 13 float4s per instance */
#define CW_SHAPE 20  /* gpu.cee's SHAPE: 5 float4s per clip shape */
#define CW_RECORD 8  /* gpu.cee's RECORD */
#define CW_KIND 36   /* misc.x */
#define CW_KIND_LAYER 6.0f
#define CW_MAX_LAYERS 256
#define CW_POOL 96
#define CW_RTVS 256
#define CW_SRVS 4096
#define CW_FORMAT DXGI_FORMAT_R8G8B8A8_UNORM

/* The shaders: metal.c's, in HLSL 5.1. */
static const char *const cw_hlsl_parts[] = {
    "struct Inst { float4 rect; float4 radii; float4 c0; float4 c1; float4 c2; float4 c3; float4 stops;\n"
    "              float4 params; float4 clip; float4 misc; float4 extra; float4 xf0; float4 xf1; };\n"
    "struct ClipShape { float4 shape; float4 radii; float4 xf0; float4 xf1; float4 link; };\n"
    "StructuredBuffer<Inst> insts : register(t0);\n"
    "StructuredBuffer<ClipShape> clips : register(t1);\n"
    "Texture2D<float> atlas : register(t2);\n"
    "Texture2D<float4> layerTex : register(t3);\n"
    "Texture2D<float4> dstTex : register(t4);\n"
    "SamplerState lin : register(s0);\n"
    /* target: the target's origin in the frame (xy) and size (zw); box: a blur pass's axis, radius, width, height. */
    "cbuffer Push : register(b0) { float4 target; int4 box; };\n"
    "struct VOut { float4 position : SV_Position; nointerpolation uint inst : INST; float2 local : LOCAL; };\n"
    "VOut vmain(uint vid : SV_VertexID, uint iid : SV_InstanceID) {\n"
    "    Inst I = insts[iid];\n"
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
    "float sdRR(float2 p, float4 rect, float4 rad) {\n"
    "    float2 hs = rect.zw * 0.5;\n"
    "    float2 q = p - (rect.xy + hs);\n"
    "    float r = q.x < 0.0 ? (q.y < 0.0 ? rad.x : rad.w) : (q.y < 0.0 ? rad.y : rad.z);\n"
    "    float2 d = abs(q) - hs + r;\n"
    "    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - r;\n"
    "}\n"
    "float cover(float d) { return clamp(0.5 - d, 0.0, 1.0); }\n"
    "float clipCover(float index, float2 p) {\n"
    "    float k = 1.0;\n"
    "    int i = int(index) - 1;\n"
    "    [loop] for (int n = 0; n < 64 && i >= 0; n++) {\n"
    "        ClipShape C = clips[i];\n"
    "        bool xf = C.xf1.w != 0.0;\n"
    "        float2 q = xf ? float2(C.xf0.x * p.x + C.xf0.z * p.y + C.xf1.x, C.xf0.y * p.x + C.xf0.w * p.y + C.xf1.y) : p;\n"
    "        k *= cover(sdRR(q, C.shape, C.radii) * (xf ? C.xf1.z : 1.0));\n"
    "        i = int(C.link.x) - 1;\n"
    "    }\n"
    "    return k;\n"
    "}\n"
    "float erfApprox(float x) {\n"
    "    float x2 = x * x;\n"
    "    float a = 0.147;\n"
    "    float v = sqrt(1.0 - exp(-x2 * (1.2732395 + a * x2) / (1.0 + a * x2)));\n"
    "    return x < 0.0 ? -v : v;\n"
    "}\n"
    "float spreadT(float t, int spread) {\n"
    "    if (spread == 2) { return t - floor(t); }\n"
    "    if (spread == 1) { float m = t - floor(t / 2.0) * 2.0; return m > 1.0 ? 2.0 - m : m; }\n"
    "    return clamp(t, 0.0, 1.0);\n"
    "}\n",
    /* The stop pair around u, without indexing local arrays (WARP got those wrong): stops are in order, so the
       pair is the first whose end is at or past u. */
    "float4 gradColor(Inst I, float t) {\n"
    "    int n = int(I.extra.x);\n"
    "    float u = spreadT(t, int(I.extra.y));\n"
    "    float4 lastC = n >= 4 ? I.c3 : n == 3 ? I.c2 : n == 2 ? I.c1 : I.c0;\n"
    "    float lastP = n >= 4 ? I.stops.w : n == 3 ? I.stops.z : n == 2 ? I.stops.y : I.stops.x;\n"
    "    float4 a = I.c0; float4 b = I.c0; float pa = I.stops.x; float pb = I.stops.x;\n"
    "    if (u <= I.stops.x) { }\n"
    "    else if (u >= lastP) { a = lastC; b = lastC; pa = lastP; pb = lastP; }\n"
    "    else if (u <= I.stops.y) { b = I.c1; pb = I.stops.y; }\n"
    "    else if (u <= I.stops.z) { a = I.c1; b = I.c2; pa = I.stops.y; pb = I.stops.z; }\n"
    "    else { a = I.c2; b = I.c3; pa = I.stops.z; pb = I.stops.w; }\n"
    "    float span = pb - pa;\n"
    "    float f = span > 0.00001 ? (u - pa) / span : 0.0;\n"
    "    float4 c = lerp(a, b, f);\n"
    "    return float4(c.rgb * c.a, c.a);\n"
    "}\n"
    "float4 fmain(VOut v) : SV_Target {\n"
    "    Inst I = insts[v.inst];\n"
    "    float2 p = v.position.xy + target.xy;\n"
    "    if (p.x < I.clip.x || p.y < I.clip.y || p.x >= I.clip.z || p.y >= I.clip.w) { discard; }\n"
    "    float k = 1.0;\n"
    "    [branch] if (I.misc.z > 0.0) { k = clipCover(I.misc.z, p); }\n"
    "    bool xf = I.xf1.w != 0.0;\n"
    "    float2 q = xf ? v.local : p;\n"
    "    float aa = xf ? I.xf1.z : 1.0;\n"
    "    int kind = int(I.misc.x);\n"
    "    float4 color;\n"
    "    if (kind == 5) {\n"
    "        float2 t = q - I.rect.xy + I.params.xy;\n"
    "        float a;\n"
    "        if (xf) { uint aw, ah; atlas.GetDimensions(aw, ah); a = atlas.SampleLevel(lin, t / float2(aw, ah), 0); }\n"
    "        else { a = atlas.Load(int3(int2(t), 0)); }\n"
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
    "    } else {\n"
    "        float t = kind == 3 ? dot(q - I.params.xy, I.params.zw) / I.extra.z : length(q - I.params.xy) / I.params.z;\n"
    "        color = gradColor(I, t) * cover(sdRR(q, I.rect, I.radii) * aa);\n"
    "    }\n"
    "    return color * k;\n"
    "}\n"
    /* Plain fills, the most common instance, in three smaller shaders: without a clip chain, with a chain of one
       shape, and with a longer one. Their values (and the one clip shape's) come from the vertex shader as flat
       attributes, and only the last has the chain's loop: per-pixel buffer reads, and the loop's presence alone,
       each made fills two to three times dearer on a GTX 850M. */
    "struct FOut { float4 position : SV_Position; nointerpolation float4 rect : RECT;\n"
    "              nointerpolation float4 radii : RADII; nointerpolation float4 color : COLOR;\n"
    "              nointerpolation float4 clip : CLIP; nointerpolation float chain : CHAIN; };\n"
    "FOut vfill(uint vid : SV_VertexID, uint iid : SV_InstanceID) {\n"
    "    VOut v = vmain(vid, iid);\n"
    "    Inst I = insts[iid];\n"
    "    FOut o;\n"
    "    o.position = v.position; o.rect = I.rect; o.radii = I.radii; o.color = I.c0; o.clip = I.clip;\n"
    "    o.chain = I.misc.z;\n"
    "    return o;\n"
    "}\n"
    "float4 ffill(FOut v) : SV_Target {\n"
    "    float2 p = v.position.xy + target.xy;\n"
    "    if (p.x < v.clip.x || p.y < v.clip.y || p.x >= v.clip.z || p.y >= v.clip.w) { discard; }\n"
    "    return v.color * cover(sdRR(p, v.rect, v.radii));\n"
    "}\n"
    "struct COut { FOut f; nointerpolation float4 shape : SHAPE; nointerpolation float4 sradii : SRADII;\n"
    "              nointerpolation float4 sxf0 : SXF0; nointerpolation float4 sxf1 : SXF1; };\n"
    "COut vfillClip(uint vid : SV_VertexID, uint iid : SV_InstanceID) {\n"
    "    COut o;\n"
    "    o.f = vfill(vid, iid);\n"
    "    ClipShape C = clips[int(o.f.chain) - 1];\n"
    "    o.shape = C.shape; o.sradii = C.radii; o.sxf0 = C.xf0; o.sxf1 = C.xf1;\n"
    "    return o;\n"
    "}\n"
    "float4 ffillClip(COut c) : SV_Target {\n"
    "    FOut v = c.f;\n"
    "    float2 p = v.position.xy + target.xy;\n"
    "    if (p.x < v.clip.x || p.y < v.clip.y || p.x >= v.clip.z || p.y >= v.clip.w) { discard; }\n"
    "    bool xf = c.sxf1.w != 0.0;\n"
    "    float2 q = xf ? float2(c.sxf0.x * p.x + c.sxf0.z * p.y + c.sxf1.x, c.sxf0.y * p.x + c.sxf0.w * p.y + c.sxf1.y) : p;\n"
    "    float k = cover(sdRR(q, c.shape, c.sradii) * (xf ? c.sxf1.z : 1.0));\n"
    "    return v.color * (cover(sdRR(p, v.rect, v.radii)) * k);\n"
    "}\n"
    "float4 ffillClipped(FOut v) : SV_Target {\n"
    "    float2 p = v.position.xy + target.xy;\n"
    "    if (p.x < v.clip.x || p.y < v.clip.y || p.x >= v.clip.z || p.y >= v.clip.w) { discard; }\n"
    "    return v.color * (cover(sdRR(p, v.rect, v.radii)) * clipCover(v.chain, p));\n"
    "}\n",
    /* ---- layers: raster_effects.cee's 8-bit formulas, ThorVG's ---- */
    "uint4 toU(float4 c) { return (uint4)(saturate(c) * 255.0 + 0.5); }\n"
    "uint mul8(uint a, uint b) { return (a * b + 255u) >> 8; }\n"
    "uint ab8(uint c, uint a) { return (c * (a + 1u)) >> 8; }\n"
    "uint interp8(uint s, uint d, uint a) {\n"
    "    int delta = (int(s) - int(d)) * int(a);\n"
    "    int st = delta >= 0 ? delta / 256 : -((-delta + 255) / 256);\n"
    "    return uint(clamp(int(d) + st, 0, 255));\n"
    "}\n"
    "uint packF(float x) { precise float v = x * 255.0 + 0.5; return uint(clamp(v, 0.0, 255.0)); }\n"
    "float chanF(uint c) { return float(c) / 255.0; }\n"
    "uint4 overF(uint4 d, float4 s) {\n"
    "    if (s.a >= 0.999 && all(d == 0u)) { return uint4(packF(s.r), packF(s.g), packF(s.b), packF(s.a)); }\n"
    "    precise float k = 1.0 - s.a;\n"
    "    precise float r = s.r + chanF(d.r) * k; precise float g = s.g + chanF(d.g) * k;\n"
    "    precise float b = s.b + chanF(d.b) * k; precise float a = s.a + chanF(d.a) * k;\n"
    "    return uint4(packF(r), packF(g), packF(b), packF(a));\n"
    "}\n"
    "uint4 grayscaleU(uint4 p) {\n"
    "    uint luma = (p.r * 54u + p.g * 182u + p.b * 19u) >> 8;\n"
    "    uint t = interp8(255u, 0u, luma);\n"
    "    uint4 m = uint4(interp8(t, p.r, 254u), interp8(t, p.g, 254u), interp8(t, p.b, 254u), interp8(255u, p.a, 254u));\n"
    "    return (m * (p.a + 1u)) >> 8;\n"
    "}\n"
    "uint4 brightnessU(uint4 p, float v, float k) {\n"
    "    precise float a = chanF(p.a);\n"
    "    if (a <= 0.0) { return p; }\n"
    "    precise float c = v * a * k; precise float ak = a * k;\n"
    "    return overF(p, float4(c, c, c, ak));\n"
    "}\n"
    "uint blendCh(int mode, uint s, uint d) {\n"
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
    "        precise float root = sqrt(float(d) / 255.0) * 255.0;\n"
    "        int dd = d <= 64u ? 4 * di - (12 * di * di) / 255 + (16 * di * di * di) / 65025 : int(root);\n"
    "        return uint(clamp(di + ((2 * si - 255) * (dd - di)) / 255, 0, 255));\n"
    "    }\n"
    "    return s;\n"
    "}\n",
    "uint4 unpre8(uint4 p) {\n"
    "    if (p.a > 0u && p.a < 255u) { return uint4(min(p.rgb * 255u / p.a, uint3(255u, 255u, 255u)), p.a); }\n"
    "    return p;\n"
    "}\n"
    "uint premixed(uint c1, uint c2, uint a) {\n"
    "    if (a == 255u) { return c1; } if (a == 0u) { return c2; }\n"
    "    return ab8(c1, a) + ab8(c2, 255u - a);\n"
    "}\n"
    "int luma8(uint3 c) { return int((c.r * 54u + c.g * 182u + c.b * 19u) >> 8); }\n"
    "uint3 saturate8(uint3 c, uint3 s3) {\n"
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
    "uint3 luminance8(uint3 c, int cl, int l) {\n"
    "    int d = l - cl;\n"
    "    int r = int(c.x) + d; int g = int(c.y) + d; int b = int(c.z) + d;\n"
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
    "uint4 blendOp(int mode, uint4 s, uint4 dst) {\n"
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
    "}\n",
    /* A layer's pixel composited onto its parent's: the parent's pixels are the scratch copy of the region. */
    "float4 fcomposite(VOut v) : SV_Target {\n"
    "    Inst I = insts[v.inst];\n"
    "    float2 p = v.position.xy + target.xy;\n"
    /* the quad's outset band lies beyond the scratch copy: leave those pixels alone */
    "    int2 lp = int2(p - I.rect.xy);\n"
    "    if (lp.x < 0 || lp.y < 0 || lp.x >= int(I.rect.z) || lp.y >= int(I.rect.w)) { discard; }\n"
    "    float4 dst = dstTex.Load(int3(lp, 0));\n"
    "    uint4 s = toU(layerTex.Load(int3(lp, 0)));\n"
    "    int f = int(I.params.z);\n"
    "    if (f == 2 && s.a != 0u) { s = grayscaleU(s); }\n"
    "    else if (f == 3) { s = brightnessU(s, 1.0, I.params.w); }\n"
    "    else if (f == 4) { s = brightnessU(s, 0.0, I.params.w); }\n"
    "    if (all(s == 0u)) { return dst; }\n"
    "    uint4 d = toU(dst);\n"
    "    float alpha = I.misc.y;\n"
    "    int mode = int(I.params.y);\n"
    "    uint4 res;\n"
    "    if (mode == 0) {\n"
    "        precise float4 sf = float4(chanF(s.r) * alpha, chanF(s.g) * alpha, chanF(s.b) * alpha, chanF(s.a) * alpha);\n"
    "        res = overF(d, sf);\n"
    "    } else {\n"
    "        uint4 u = unpre8(s);\n"
    "        uint4 b = blendOp(mode, u, d);\n"
    "        uint k = mul8(uint(clamp(alpha * 255.0 + 0.5, 0.0, 255.0)), s.a);\n"
    "        res = uint4(interp8(b.r, d.r, k), interp8(b.g, d.g, k), interp8(b.b, d.b, k), interp8(b.a, d.a, k));\n"
    "    }\n"
    "    return float4(res) / 255.0;\n"
    "}\n"
    /* ThorVG's box pass (raster_effects.cee's boxPass) from layerTex. */
    "float4 vfull(uint vid : SV_VertexID) : SV_Position {\n"
    "    return float4((vid & 1) ? 1.0 : -1.0, (vid & 2) ? -1.0 : 1.0, 0.0, 1.0);\n"
    "}\n"
    "float4 fbox(float4 position : SV_Position) : SV_Target {\n"
    "    int2 p = int2(position.xy);\n"
    "    int d = box.y;\n"
    "    int end = (box.x == 0 ? box.z : box.w) - 1;\n"
    "    uint4 acc = uint4(0u, 0u, 0u, 0u);\n"
    "    [loop] for (int j = -d; j <= d; j++) {\n"
    "        int2 q = p;\n"
    "        if (box.x == 0) { q.x = clamp(p.x + j, 0, end); } else { q.y = clamp(p.y + j, 0, end); }\n"
    "        acc += toU(layerTex.Load(int3(q, 0)));\n"
    "    }\n"
    "    precise float iarr = 1.0 / float(d + d + 1);\n"
    "    precise float4 v = float4(acc) * iarr;\n"
    "    return float4((uint4)v) / 255.0;\n"
    "}\n",
};

/* ---- the loader ---------------------------------------------------------------------------------------------- */

typedef HRESULT(WINAPI *cw_create_factory)(UINT, REFIID, void **);

static struct {
    PFN_D3D12_CREATE_DEVICE create_device;
    PFN_D3D12_SERIALIZE_ROOT_SIGNATURE serialize_root;
    cw_create_factory create_factory;
    pD3DCompile compile;
} fn;

static int load(void) {
    HMODULE d3d = LoadLibraryA("d3d12.dll");
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    if (d3d == NULL || dxgi == NULL || compiler == NULL) {
        return 0;
    }
    FARPROC p;
    p = GetProcAddress(d3d, "D3D12CreateDevice");
    memcpy(&fn.create_device, &p, sizeof p);
    p = GetProcAddress(d3d, "D3D12SerializeRootSignature");
    memcpy(&fn.serialize_root, &p, sizeof p);
    p = GetProcAddress(dxgi, "CreateDXGIFactory2");
    memcpy(&fn.create_factory, &p, sizeof p);
    p = GetProcAddress(compiler, "D3DCompile");
    memcpy(&fn.compile, &p, sizeof p);
    return fn.create_device != NULL && fn.serialize_root != NULL && fn.create_factory != NULL && fn.compile != NULL;
}

/* ---- state -------------------------------------------------------------------------------------------------- */

typedef struct {
    ID3D12Resource *resource;
    void *mapped;
    UINT64 size;
} cw_buffer;

typedef struct {
    ID3D12Resource *resource;
    D3D12_RESOURCE_STATES state;
    uint32_t width, height;
    UINT rtv; /* its slot in the RTV heap */
    int used;
} cw_texture;

static struct {
    int ready;
    int trace;
    IDXGIFactory4 *factory;
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *list;
    ID3D12Fence *fence;
    HANDLE event;
    UINT64 fence_value;
    int pending;
    UINT64 timestamp_frequency;
    ID3D12QueryHeap *queries;
    cw_buffer timestamps;
    ID3D12RootSignature *root;
    ID3D12PipelineState *shapes, *fills, *clip_fills, *clipped_fills, *composite, *box;
    const float *frame_clips; /* this frame's clip shapes, CW_SHAPE floats each */
    uint32_t frame_clip_count;
    ID3D12DescriptorHeap *rtv_heap, *srv_heap;
    UINT rtv_size, srv_size, rtv_next, srv_next;
    ID3D12Resource *atlas;
    D3D12_RESOURCE_STATES atlas_state;
    uint32_t atlas_width, atlas_height;
    int atlas_dirty;
    uint32_t ax0, ay0, ax1, ay1;
    cw_buffer staging, instances, clips, readback;
    cw_texture pool[CW_POOL];
    cw_texture offscreen;
    IDXGISwapChain3 *swapchain;
    cw_texture swap[2];
    uint32_t swap_width, swap_height;
    double gpu_ms;
} d;

static void trace(const char *what, long long value) {
    if (d.trace) {
        fprintf(stderr, "d3d12: %s %lld\n", what, value);
        fflush(stderr);
    }
}

/* A descriptor heap's first handles. These methods return a structure, which COM returns through a pointer after
   `this` whatever the compiler; C declarations of them differ between headers, so they are called by that ABI. */
typedef D3D12_CPU_DESCRIPTOR_HANDLE *(STDMETHODCALLTYPE *cw_cpu_start)(ID3D12DescriptorHeap *,
                                                                       D3D12_CPU_DESCRIPTOR_HANDLE *);
typedef D3D12_GPU_DESCRIPTOR_HANDLE *(STDMETHODCALLTYPE *cw_gpu_start)(ID3D12DescriptorHeap *,
                                                                       D3D12_GPU_DESCRIPTOR_HANDLE *);

static D3D12_CPU_DESCRIPTOR_HANDLE heap_cpu_start(ID3D12DescriptorHeap *heap) {
    cw_cpu_start call;
    memcpy(&call, &heap->lpVtbl->GetCPUDescriptorHandleForHeapStart, sizeof call);
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    call(heap, &h);
    return h;
}

static D3D12_GPU_DESCRIPTOR_HANDLE heap_gpu_start(ID3D12DescriptorHeap *heap) {
    cw_gpu_start call;
    memcpy(&call, &heap->lpVtbl->GetGPUDescriptorHandleForHeapStart, sizeof call);
    D3D12_GPU_DESCRIPTOR_HANDLE h;
    call(heap, &h);
    return h;
}

static D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle(UINT slot) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = heap_cpu_start(d.rtv_heap);
    h.ptr += (SIZE_T)slot * d.rtv_size;
    return h;
}

static D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu(UINT slot) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = heap_cpu_start(d.srv_heap);
    h.ptr += (SIZE_T)slot * d.srv_size;
    return h;
}

static D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu(UINT slot) {
    D3D12_GPU_DESCRIPTOR_HANDLE h = heap_gpu_start(d.srv_heap);
    h.ptr += (UINT64)slot * d.srv_size;
    return h;
}

/* A buffer in `heap` (UPLOAD or READBACK), mapped, holding at least `size` bytes. Between frames only. */
static int ensure_buffer(cw_buffer *b, UINT64 size, D3D12_HEAP_TYPE heap) {
    if (b->resource != NULL && b->size >= size) {
        return 1;
    }
    if (b->resource != NULL) {
        ID3D12Resource_Release(b->resource);
        memset(b, 0, sizeof *b);
    }
    UINT64 rounded = 65536;
    while (rounded < size) {
        rounded *= 2;
    }
    D3D12_HEAP_PROPERTIES props = {heap, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
    D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, rounded, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                                D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
    D3D12_RESOURCE_STATES state =
        heap == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST;
    if (FAILED(ID3D12Device_CreateCommittedResource(d.device, &props, D3D12_HEAP_FLAG_NONE, &desc, state, NULL,
                                                    &IID_ID3D12Resource, (void **)&b->resource))) {
        return 0;
    }
    b->size = rounded;
    D3D12_RANGE none = {0, 0};
    if (FAILED(ID3D12Resource_Map(b->resource, 0, heap == D3D12_HEAP_TYPE_UPLOAD ? &none : NULL, &b->mapped))) {
        return 0;
    }
    return 1;
}

static ID3D12Resource *make_texture(uint32_t width, uint32_t height, DXGI_FORMAT format, int target,
                                    D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES props = {D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN,
                                   1, 1};
    D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                                0,
                                width,
                                height,
                                1,
                                1,
                                format,
                                {1, 0},
                                D3D12_TEXTURE_LAYOUT_UNKNOWN,
                                target ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE};
    ID3D12Resource *r = NULL;
    if (FAILED(ID3D12Device_CreateCommittedResource(d.device, &props, D3D12_HEAP_FLAG_NONE, &desc, state, NULL,
                                                    &IID_ID3D12Resource, (void **)&r))) {
        return NULL;
    }
    return r;
}

static D3D12_TEXTURE_COPY_LOCATION subresource(ID3D12Resource *r) {
    D3D12_TEXTURE_COPY_LOCATION l;
    memset(&l, 0, sizeof l);
    l.pResource = r;
    l.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    l.SubresourceIndex = 0;
    return l;
}

static void transition(ID3D12Resource *r, D3D12_RESOURCE_STATES *state, D3D12_RESOURCE_STATES to) {
    if (*state == to) {
        return;
    }
    D3D12_RESOURCE_BARRIER b;
    memset(&b, 0, sizeof b);
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = *state;
    b.Transition.StateAfter = to;
    ID3D12GraphicsCommandList_ResourceBarrier(d.list, 1, &b);
    *state = to;
}

/* An RGBA8 texture of this size from the pool, for a layer, a blur pass or a composite's scratch copy. */
static cw_texture *pooled(uint32_t width, uint32_t height) {
    int free_slot = -1;
    for (int k = 0; k < CW_POOL; k++) {
        cw_texture *t = &d.pool[k];
        if (t->resource != NULL && !t->used && t->width == width && t->height == height) {
            t->used = 1;
            return t;
        }
        if (t->resource == NULL && free_slot < 0) {
            free_slot = k;
        }
    }
    if (free_slot < 0) {
        for (int k = 0; k < CW_POOL; k++) {
            if (!d.pool[k].used) {
                ID3D12Resource_Release(d.pool[k].resource);
                d.pool[k].resource = NULL;
                free_slot = k;
                break;
            }
        }
        if (free_slot < 0) {
            return NULL;
        }
    }
    cw_texture *t = &d.pool[free_slot];
    UINT rtv = t->rtv != 0 ? t->rtv : d.rtv_next++; /* a slot keeps its RTV slot */
    t->resource = make_texture(width, height, CW_FORMAT, 1, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (t->resource == NULL || rtv >= CW_RTVS) {
        return NULL;
    }
    t->state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    t->rtv = rtv;
    ID3D12Device_CreateRenderTargetView(d.device, t->resource, NULL, rtv_handle(rtv));
    t->width = width;
    t->height = height;
    t->used = 1;
    return t;
}

/* A shader-visible SRV of `r` for this frame. */
static D3D12_GPU_DESCRIPTOR_HANDLE srv_of(ID3D12Resource *r, DXGI_FORMAT format) {
    UINT slot = 1 + (d.srv_next++ - 1) % (CW_SRVS - 1); /* slot 0 is the atlas */
    D3D12_SHADER_RESOURCE_VIEW_DESC v;
    memset(&v, 0, sizeof v);
    v.Format = format;
    v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    v.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(d.device, r, &v, srv_cpu(slot));
    return srv_gpu(slot);
}

/* ---- setup -------------------------------------------------------------------------------------------------- */

static ID3DBlob *compile(const char *source, const char *entry, const char *profile, UINT flags) {
    ID3DBlob *code = NULL, *errors = NULL;
    HRESULT hr = fn.compile(source, strlen(source), "claywind.hlsl", NULL, NULL, entry, profile, flags, 0, &code,
                            &errors);
    if (FAILED(hr)) {
        fprintf(stderr, "d3d12: %s: %s\n", entry,
                errors != NULL ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "failed");
        if (errors != NULL) {
            ID3D10Blob_Release(errors);
        }
        return NULL;
    }
    if (errors != NULL) {
        ID3D10Blob_Release(errors);
    }
    return code;
}

static ID3D12PipelineState *make_pipeline(ID3DBlob *vs, ID3DBlob *ps, int blending) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;
    memset(&desc, 0, sizeof desc);
    desc.pRootSignature = d.root;
    desc.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(vs);
    desc.VS.BytecodeLength = ID3D10Blob_GetBufferSize(vs);
    desc.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(ps);
    desc.PS.BytecodeLength = ID3D10Blob_GetBufferSize(ps);
    D3D12_RENDER_TARGET_BLEND_DESC *rt = &desc.BlendState.RenderTarget[0];
    rt->BlendEnable = blending ? TRUE : FALSE;
    /* Premultiplied source over: one, one minus source alpha. */
    rt->SrcBlend = D3D12_BLEND_ONE;
    rt->DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt->BlendOp = D3D12_BLEND_OP_ADD;
    rt->SrcBlendAlpha = D3D12_BLEND_ONE;
    rt->DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rt->BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt->LogicOp = D3D12_LOGIC_OP_NOOP;
    rt->RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = CW_FORMAT;
    desc.SampleDesc.Count = 1;
    ID3D12PipelineState *pso = NULL;
    if (FAILED(ID3D12Device_CreateGraphicsPipelineState(d.device, &desc, &IID_ID3D12PipelineState, (void **)&pso))) {
        fprintf(stderr, "d3d12: a pipeline failed\n");
        return NULL;
    }
    return pso;
}

static int make_root_signature(void) {
    D3D12_DESCRIPTOR_RANGE ranges[3];
    for (int k = 0; k < 3; k++) {
        ranges[k].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[k].NumDescriptors = 1;
        ranges[k].BaseShaderRegister = 2 + (UINT)k; /* t2 atlas, t3 layer, t4 scratch */
        ranges[k].RegisterSpace = 0;
        ranges[k].OffsetInDescriptorsFromTableStart = 0;
    }
    D3D12_ROOT_PARAMETER params[6];
    memset(params, 0, sizeof params);
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = 8;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; /* t0: the instances */
    params[1].Descriptor.ShaderRegister = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; /* t1: the clip shapes */
    params[2].Descriptor.ShaderRegister = 1;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (int k = 0; k < 3; k++) {
        params[3 + k].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[3 + k].DescriptorTable.NumDescriptorRanges = 1;
        params[3 + k].DescriptorTable.pDescriptorRanges = &ranges[k];
        params[3 + k].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    /* The atlas under a transform: linear, transparent beyond its edge. */
    D3D12_STATIC_SAMPLER_DESC sampler;
    memset(&sampler, 0, sizeof sampler);
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MaxLOD = 0.0f;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc = {6, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob *blob = NULL, *errors = NULL;
    if (FAILED(fn.serialize_root(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors))) {
        fprintf(stderr, "d3d12: the root signature: %s\n",
                errors != NULL ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "failed");
        return 0;
    }
    HRESULT hr = ID3D12Device_CreateRootSignature(d.device, 0, ID3D10Blob_GetBufferPointer(blob),
                                                  ID3D10Blob_GetBufferSize(blob), &IID_ID3D12RootSignature,
                                                  (void **)&d.root);
    ID3D10Blob_Release(blob);
    return SUCCEEDED(hr);
}

static int create_device(void) {
    if (FAILED(fn.create_factory(0, &IID_IDXGIFactory4, (void **)&d.factory))) {
        return 0;
    }
    const char *warp = getenv("CLAYWIND_D3D12_WARP");
    if (warp == NULL || warp[0] != '1') {
        IDXGIFactory6 *six = NULL;
        if (SUCCEEDED(IDXGIFactory4_QueryInterface(d.factory, &IID_IDXGIFactory6, (void **)&six))) {
            IDXGIAdapter1 *fast = NULL;
            HRESULT found = IDXGIFactory6_EnumAdapterByGpuPreference(six, 0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                     &IID_IDXGIAdapter1, (void **)&fast);
            IDXGIFactory6_Release(six);
            if (SUCCEEDED(found)) {
                HRESULT hr = fn.create_device((IUnknown *)fast, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device,
                                              (void **)&d.device);
                IDXGIAdapter1_Release(fast);
                if (SUCCEEDED(hr)) {
                    trace("device: the high-performance adapter", 0);
                    return 1;
                }
            }
        }
        if (SUCCEEDED(fn.create_device(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&d.device))) {
            trace("device: the default adapter", 0);
            return 1;
        }
    }
    IDXGIAdapter *adapter = NULL;
    if (FAILED(IDXGIFactory4_EnumWarpAdapter(d.factory, &IID_IDXGIAdapter, (void **)&adapter))) {
        return 0;
    }
    HRESULT hr = fn.create_device((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&d.device);
    IDXGIAdapter_Release(adapter);
    trace("device: WARP", hr);
    return SUCCEEDED(hr);
}

static int32_t d3d_init(uint32_t atlas_width, uint32_t atlas_height) {
    d.trace = getenv("CW_TRACE") != NULL;
    if (!load()) {
        return 1;
    }
    if (!create_device()) {
        return 2;
    }
    D3D12_COMMAND_QUEUE_DESC qdesc = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
    if (FAILED(ID3D12Device_CreateCommandQueue(d.device, &qdesc, &IID_ID3D12CommandQueue, (void **)&d.queue)) ||
        FAILED(ID3D12Device_CreateCommandAllocator(d.device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   &IID_ID3D12CommandAllocator, (void **)&d.allocator)) ||
        FAILED(ID3D12Device_CreateCommandList(d.device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, d.allocator, NULL,
                                              &IID_ID3D12GraphicsCommandList, (void **)&d.list)) ||
        FAILED(ID3D12Device_CreateFence(d.device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&d.fence))) {
        return 3;
    }
    ID3D12GraphicsCommandList_Close(d.list);
    d.event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (SUCCEEDED(ID3D12CommandQueue_GetTimestampFrequency(d.queue, &d.timestamp_frequency))) {
        D3D12_QUERY_HEAP_DESC qh = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2, 0};
        if (FAILED(ID3D12Device_CreateQueryHeap(d.device, &qh, &IID_ID3D12QueryHeap, (void **)&d.queries)) ||
            !ensure_buffer(&d.timestamps, 16, D3D12_HEAP_TYPE_READBACK)) {
            d.queries = NULL;
        }
    }
    D3D12_DESCRIPTOR_HEAP_DESC rh = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, CW_RTVS, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC sh = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, CW_SRVS,
                                     D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    if (FAILED(ID3D12Device_CreateDescriptorHeap(d.device, &rh, &IID_ID3D12DescriptorHeap, (void **)&d.rtv_heap)) ||
        FAILED(ID3D12Device_CreateDescriptorHeap(d.device, &sh, &IID_ID3D12DescriptorHeap, (void **)&d.srv_heap))) {
        return 3;
    }
    d.rtv_size = ID3D12Device_GetDescriptorHandleIncrementSize(d.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    d.srv_size = ID3D12Device_GetDescriptorHandleIncrementSize(d.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    d.rtv_next = 3; /* 0 and 1: the swap chain's buffers; 2: the offscreen texture */
    if (!make_root_signature()) {
        return 4;
    }
    static char source[40000];
    source[0] = '\0';
    for (size_t k = 0; k < sizeof cw_hlsl_parts / sizeof cw_hlsl_parts[0]; k++) {
        strncat(source, cw_hlsl_parts[k], sizeof source - strlen(source) - 1);
    }
    UINT fast = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    UINT strict = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_IEEE_STRICTNESS;
    ID3DBlob *vmain = compile(source, "vmain", "vs_5_1", fast);
    ID3DBlob *fmain = compile(source, "fmain", "ps_5_1", fast);
    ID3DBlob *vfill = compile(source, "vfill", "vs_5_1", fast);
    ID3DBlob *ffill = compile(source, "ffill", "ps_5_1", fast);
    ID3DBlob *vclip = compile(source, "vfillClip", "vs_5_1", fast);
    ID3DBlob *fclip = compile(source, "ffillClip", "ps_5_1", fast);
    ID3DBlob *fclipped = compile(source, "ffillClipped", "ps_5_1", fast);
    ID3DBlob *fcomposite = compile(source, "fcomposite", "ps_5_1", strict);
    ID3DBlob *vfull = compile(source, "vfull", "vs_5_1", strict);
    ID3DBlob *fbox = compile(source, "fbox", "ps_5_1", strict);
    if (vmain == NULL || fmain == NULL || vfill == NULL || ffill == NULL || vclip == NULL || fclip == NULL || fclipped == NULL || fcomposite == NULL || vfull == NULL || fbox == NULL) {
        return 5;
    }
    d.shapes = make_pipeline(vmain, fmain, 1);
    d.fills = make_pipeline(vfill, ffill, 1);
    d.clip_fills = make_pipeline(vclip, fclip, 1);
    d.clipped_fills = make_pipeline(vfill, fclipped, 1);
    d.composite = make_pipeline(vmain, fcomposite, 0);
    d.box = make_pipeline(vfull, fbox, 0);
    if (d.shapes == NULL || d.fills == NULL || d.clip_fills == NULL || d.clipped_fills == NULL || d.composite == NULL || d.box == NULL) {
        return 6;
    }
    d.atlas_width = atlas_width;
    d.atlas_height = atlas_height;
    d.atlas = make_texture(atlas_width, atlas_height, DXGI_FORMAT_R8_UNORM, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    if (d.atlas == NULL || !ensure_buffer(&d.staging, (UINT64)atlas_width * atlas_height, D3D12_HEAP_TYPE_UPLOAD)) {
        return 7;
    }
    d.atlas_state = D3D12_RESOURCE_STATE_COPY_DEST;
    memset(d.staging.mapped, 0, (size_t)atlas_width * atlas_height);
    d.atlas_dirty = 1; /* the first frame clears it */
    d.ax0 = 0;
    d.ay0 = 0;
    d.ax1 = atlas_width;
    d.ay1 = atlas_height;
    d.ready = 1;
    return 0;
}

static int32_t d3d_attach(int32_t kind, uint64_t first, uint64_t second) {
    (void)second;
    if (!d.ready || kind != 2) {
        return 1;
    }
    HWND hwnd = (HWND)(uintptr_t)first;
    RECT client;
    GetClientRect(hwnd, &client);
    DXGI_SWAP_CHAIN_DESC1 desc;
    memset(&desc, 0, sizeof desc);
    desc.Width = (UINT)(client.right - client.left);
    desc.Height = (UINT)(client.bottom - client.top);
    desc.Format = CW_FORMAT;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    IDXGISwapChain1 *one = NULL;
    HRESULT hr = IDXGIFactory4_CreateSwapChainForHwnd(d.factory, (IUnknown *)d.queue, hwnd, &desc, NULL, NULL, &one);
    trace("swap chain created", hr);
    if (FAILED(hr)) {
        return 2;
    }
    hr = IDXGISwapChain1_QueryInterface(one, &IID_IDXGISwapChain3, (void **)&d.swapchain);
    IDXGISwapChain1_Release(one);
    if (FAILED(hr)) {
        return 3;
    }
    IDXGIFactory4_MakeWindowAssociation(d.factory, hwnd, DXGI_MWA_NO_ALT_ENTER);
    d.swap_width = 0; /* the first frame sizes the buffers */
    return 0;
}

/* The swap chain's buffers at `width` × `height`, and their RTVs (slots 0 and 1). */
static int ensure_swapchain(uint32_t width, uint32_t height) {
    if (d.swap_width == width && d.swap_height == height && d.swap[0].resource != NULL) {
        return 1;
    }
    for (int k = 0; k < 2; k++) {
        if (d.swap[k].resource != NULL) {
            ID3D12Resource_Release(d.swap[k].resource);
            d.swap[k].resource = NULL;
        }
    }
    HRESULT hr = IDXGISwapChain3_ResizeBuffers(d.swapchain, 2, width, height, CW_FORMAT, 0);
    trace("swap chain resized", hr);
    if (FAILED(hr)) {
        return 0;
    }
    for (UINT k = 0; k < 2; k++) {
        if (FAILED(IDXGISwapChain3_GetBuffer(d.swapchain, k, &IID_ID3D12Resource, (void **)&d.swap[k].resource))) {
            return 0;
        }
        d.swap[k].state = D3D12_RESOURCE_STATE_PRESENT;
        d.swap[k].rtv = k;
        d.swap[k].width = width;
        d.swap[k].height = height;
        ID3D12Device_CreateRenderTargetView(d.device, d.swap[k].resource, NULL, rtv_handle(k));
    }
    d.swap_width = width;
    d.swap_height = height;
    return 1;
}

/* ---- frames ------------------------------------------------------------------------------------------------- */

static void d3d_finish(void) {
    if (!d.pending) {
        return;
    }
    if (ID3D12Fence_GetCompletedValue(d.fence) < d.fence_value) {
        ID3D12Fence_SetEventOnCompletion(d.fence, d.fence_value, d.event);
        WaitForSingleObject(d.event, INFINITE);
    }
    d.pending = 0;
    if (d.queries != NULL && d.timestamp_frequency > 0) {
        const UINT64 *ticks = (const UINT64 *)d.timestamps.mapped;
        d.gpu_ms = (double)(ticks[1] - ticks[0]) * 1000.0 / (double)d.timestamp_frequency;
    }
}

static void d3d_atlas(const uint8_t *pixels, uint32_t stride, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    if (!d.ready || x1 <= x0 || y1 <= y0) {
        return;
    }
    d3d_finish(); /* the last frame may still copy from the staging buffer */
    uint8_t *staging = (uint8_t *)d.staging.mapped;
    for (uint32_t y = y0; y < y1; y++) {
        memcpy(staging + (size_t)y * d.atlas_width + x0, pixels + (size_t)y * stride + x0, x1 - x0);
    }
    if (!d.atlas_dirty) {
        d.ax0 = x0;
        d.ay0 = y0;
        d.ax1 = x1;
        d.ay1 = y1;
    } else {
        d.ax0 = d.ax0 < x0 ? d.ax0 : x0;
        d.ay0 = d.ay0 < y0 ? d.ay0 : y0;
        d.ax1 = d.ax1 > x1 ? d.ax1 : x1;
        d.ay1 = d.ay1 > y1 ? d.ay1 : y1;
    }
    d.atlas_dirty = 1;
}

typedef struct {
    float target[4];
    int32_t box[4];
} cw_push;

static int pipeline_class(const float *instance) {
    if (instance[CW_KIND] == CW_KIND_LAYER) {
        return 2;
    }
    if (instance[CW_KIND] != 0.0f || instance[51] != 0.0f) { /* not a fill, or transformed (xf1.w) */
        return 0;
    }
    int chain = (int)instance[CW_KIND + 2]; /* misc.z: its clip chain, the first shape's index plus one */
    if (chain <= 0) {
        return 1;
    }
    if (chain <= (int)d.frame_clip_count && d.frame_clips[(size_t)(chain - 1) * CW_SHAPE + 16] == 0.0f) {
        return 3; /* one shape: its link is 0 */
    }
    return 4;
}

static void begin_target(cw_texture *t, const float box[4], int clear, float r, float g, float b, float a) {
    transition(t->resource, &t->state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_handle(t->rtv);
    ID3D12GraphicsCommandList_OMSetRenderTargets(d.list, 1, &rtv, FALSE, NULL);
    if (clear) {
        float color[4] = {r, g, b, a};
        ID3D12GraphicsCommandList_ClearRenderTargetView(d.list, rtv, color, 0, NULL);
    }
    D3D12_VIEWPORT viewport = {0.0f, 0.0f, (float)t->width, (float)t->height, 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, (LONG)t->width, (LONG)t->height};
    ID3D12GraphicsCommandList_RSSetViewports(d.list, 1, &viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(d.list, 1, &scissor);
    cw_push push = {{box[0], box[1], box[2], box[3]}, {0, 0, 0, 0}};
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(d.list, 0, 8, &push, 0);
}

/* Draws `count` instances of `data` (which the instance buffer holds from `base`) into `t`, whose box in the frame
   is `box`: shapes and fills in runs, each layer composite on its own after a scratch copy of what it covers. */
static void draw_target(cw_texture *t, const float *data, uint32_t base, uint32_t count, const float box[4],
                        cw_texture **textures, D3D12_GPU_VIRTUAL_ADDRESS instances, D3D12_GPU_DESCRIPTOR_HANDLE atlas) {
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(d.list, 3, atlas);
    ID3D12PipelineState *bound = NULL;
    uint32_t k = 0;
    while (k < count) {
        const float *instance = data + (size_t)k * CW_FLOATS;
        int kind = pipeline_class(instance);
        if (kind == 2) {
            int layer = (int)instance[28]; /* params.x */
            if (layer >= 0 && layer < CW_MAX_LAYERS && textures[layer] != NULL) {
                cw_texture *src = textures[layer];
                /* The region of the target the layer covers, copied to a scratch texture as the parent's pixels. */
                int32_t lx = (int32_t)(instance[0] - box[0]), ly = (int32_t)(instance[1] - box[1]);
                int32_t lw = (int32_t)instance[2], lh = (int32_t)instance[3];
                cw_texture *scratch = lw > 0 && lh > 0 ? pooled((uint32_t)lw, (uint32_t)lh) : NULL;
                if (scratch != NULL) {
                    int32_t x0 = lx < 0 ? 0 : lx, y0 = ly < 0 ? 0 : ly;
                    int32_t x1 = lx + lw > (int32_t)t->width ? (int32_t)t->width : lx + lw;
                    int32_t y1 = ly + lh > (int32_t)t->height ? (int32_t)t->height : ly + lh;
                    if (x1 > x0 && y1 > y0) {
                        transition(t->resource, &t->state, D3D12_RESOURCE_STATE_COPY_SOURCE);
                        transition(scratch->resource, &scratch->state, D3D12_RESOURCE_STATE_COPY_DEST);
                        D3D12_TEXTURE_COPY_LOCATION to = subresource(scratch->resource);
                        D3D12_TEXTURE_COPY_LOCATION from = subresource(t->resource);
                        D3D12_BOX region = {(UINT)x0, (UINT)y0, 0, (UINT)x1, (UINT)y1, 1};
                        ID3D12GraphicsCommandList_CopyTextureRegion(d.list, &to, (UINT)(x0 - lx), (UINT)(y0 - ly), 0,
                                                                    &from, &region);
                        transition(t->resource, &t->state, D3D12_RESOURCE_STATE_RENDER_TARGET);
                    }
                    transition(scratch->resource, &scratch->state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    transition(src->resource, &src->state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(d.list, 4, srv_of(src->resource, CW_FORMAT));
                    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(d.list, 5,
                                                                             srv_of(scratch->resource, CW_FORMAT));
                    if (bound != d.composite) {
                        ID3D12GraphicsCommandList_SetPipelineState(d.list, d.composite);
                        bound = d.composite;
                    }
                    ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(
                        d.list, 1, instances + (UINT64)(base + k) * CW_FLOATS * sizeof(float));
                    ID3D12GraphicsCommandList_DrawInstanced(d.list, 4, 1, 0, 0);
                }
            }
            k++;
            continue;
        }
        uint32_t run = k + 1;
        while (run < count && pipeline_class(data + (size_t)run * CW_FLOATS) == kind) {
            run++;
        }
        ID3D12PipelineState *p = kind == 1   ? d.fills
                                 : kind == 3 ? d.clip_fills
                                 : kind == 4 ? d.clipped_fills
                                             : d.shapes;
        if (bound != p) {
            ID3D12GraphicsCommandList_SetPipelineState(d.list, p);
            bound = p;
        }
        /* SV_InstanceID starts at 0 whatever the start instance, so the instances' view starts at the run. */
        ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(
            d.list, 1, instances + (UINT64)(base + k) * CW_FLOATS * sizeof(float));
        ID3D12GraphicsCommandList_DrawInstanced(d.list, 4, run - k, 0, 0);
        k = run;
    }
}

static void box_pass(cw_texture *from, cw_texture *to, int axis, int dist, uint32_t w, uint32_t h) {
    transition(from->resource, &from->state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    float box[4] = {0.0f, 0.0f, (float)w, (float)h};
    begin_target(to, box, 0, 0, 0, 0, 0);
    cw_push push = {{0.0f, 0.0f, (float)w, (float)h}, {axis, dist, (int32_t)w, (int32_t)h}};
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(d.list, 0, 8, &push, 0);
    ID3D12GraphicsCommandList_SetPipelineState(d.list, d.box);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(d.list, 4, srv_of(from->resource, CW_FORMAT));
    ID3D12GraphicsCommandList_DrawInstanced(d.list, 4, 1, 0, 0);
}

/* Records a whole frame into the command list: the atlas upload, each layer in the order it closed and its blur,
   then the frame into `target`. */
static int record(cw_texture *target, const float *frame, uint32_t count, const float *layers,
                  uint32_t layer_instances, const float *records, uint32_t layer_count, const float *clips,
                  uint32_t clip_count, uint32_t width, uint32_t height, float r, float g, float b) {
    for (int k = 0; k < CW_POOL; k++) {
        d.pool[k].used = 0;
    }
    d.srv_next = 1; /* slot 0: the atlas */
    d.frame_clips = clips;
    d.frame_clip_count = clip_count;
    size_t total = (size_t)count + layer_instances;
    UINT64 instance_bytes = (total ? total : 1) * CW_FLOATS * sizeof(float);
    UINT64 clip_bytes = (clip_count ? clip_count : 1) * CW_SHAPE * sizeof(float);
    if (!ensure_buffer(&d.instances, instance_bytes, D3D12_HEAP_TYPE_UPLOAD) ||
        !ensure_buffer(&d.clips, clip_bytes, D3D12_HEAP_TYPE_UPLOAD)) {
        return 0;
    }
    memcpy(d.instances.mapped, frame, (size_t)count * CW_FLOATS * sizeof(float));
    memcpy((float *)d.instances.mapped + (size_t)count * CW_FLOATS, layers,
           (size_t)layer_instances * CW_FLOATS * sizeof(float));
    memset(d.clips.mapped, 0, CW_SHAPE * sizeof(float));
    memcpy(d.clips.mapped, clips, (size_t)clip_count * CW_SHAPE * sizeof(float));
    ID3D12CommandAllocator_Reset(d.allocator);
    ID3D12GraphicsCommandList_Reset(d.list, d.allocator, NULL);
    if (d.queries != NULL) {
        ID3D12GraphicsCommandList_EndQuery(d.list, d.queries, D3D12_QUERY_TYPE_TIMESTAMP, 0);
    }
    if (d.atlas_dirty) {
        transition(d.atlas, &d.atlas_state, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION to = subresource(d.atlas);
        D3D12_TEXTURE_COPY_LOCATION from;
        memset(&from, 0, sizeof from);
        from.pResource = d.staging.resource;
        from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint.Offset = 0;
        from.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8_UNORM;
        from.PlacedFootprint.Footprint.Width = d.atlas_width;
        from.PlacedFootprint.Footprint.Height = d.atlas_height;
        from.PlacedFootprint.Footprint.Depth = 1;
        from.PlacedFootprint.Footprint.RowPitch = d.atlas_width; /* 2048: a multiple of 256 */
        D3D12_BOX region = {d.ax0, d.ay0, 0, d.ax1, d.ay1, 1};
        ID3D12GraphicsCommandList_CopyTextureRegion(d.list, &to, d.ax0, d.ay0, 0, &from, &region);
        d.atlas_dirty = 0;
    }
    transition(d.atlas, &d.atlas_state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap *heaps[1] = {d.srv_heap};
    ID3D12GraphicsCommandList_SetDescriptorHeaps(d.list, 1, heaps);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(d.list, d.root);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(d.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    D3D12_SHADER_RESOURCE_VIEW_DESC av;
    memset(&av, 0, sizeof av);
    av.Format = DXGI_FORMAT_R8_UNORM;
    av.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    av.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    av.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(d.device, d.atlas, &av, srv_cpu(0));
    D3D12_GPU_DESCRIPTOR_HANDLE atlas = srv_gpu(0);
    D3D12_GPU_VIRTUAL_ADDRESS instances = ID3D12Resource_GetGPUVirtualAddress(d.instances.resource);
    ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(
        d.list, 2, ID3D12Resource_GetGPUVirtualAddress(d.clips.resource));
    cw_texture *textures[CW_MAX_LAYERS];
    uint32_t n = layer_count < CW_MAX_LAYERS ? layer_count : CW_MAX_LAYERS;
    for (uint32_t l = 0; l < n; l++) {
        const float *rec = records + (size_t)l * CW_RECORD;
        textures[l] = NULL;
        uint32_t x0 = (uint32_t)rec[2], y0 = (uint32_t)rec[3], x1 = (uint32_t)rec[4], y1 = (uint32_t)rec[5];
        if (x1 <= x0 || y1 <= y0) {
            continue;
        }
        uint32_t w = x1 - x0, h = y1 - y0;
        cw_texture *t = pooled(w, h);
        if (t == NULL) {
            continue;
        }
        float box[4] = {(float)x0, (float)y0, (float)w, (float)h};
        begin_target(t, box, 1, 0, 0, 0, 0);
        uint32_t first = (uint32_t)rec[0];
        draw_target(t, layers + (size_t)first * CW_FLOATS, count + first, (uint32_t)rec[1], box, textures, instances,
                    atlas);
        int k0 = (int)rec[6], k1 = (int)rec[7];
        if (k0 > 0 || k1 > 0) {
            cw_texture *other = pooled(w, h);
            if (other != NULL) {
                cw_texture *from = t, *to = other;
                for (int axis = 0; axis < 2; axis++) {
                    for (int level = 0; level < 2; level++) {
                        int dist = level == 0 ? k0 : k1;
                        if (dist <= 0) {
                            continue;
                        }
                        box_pass(from, to, axis, dist, w, h);
                        cw_texture *swap = from;
                        from = to;
                        to = swap;
                    }
                }
                t = from;
            }
        }
        textures[l] = t;
    }
    float whole[4] = {0.0f, 0.0f, (float)width, (float)height};
    begin_target(target, whole, 1, r, g, b, 1.0f);
    draw_target(target, frame, 0, count, whole, textures, instances, atlas);
    return 1;
}

static void submit(void) {
    if (d.queries != NULL) {
        ID3D12GraphicsCommandList_EndQuery(d.list, d.queries, D3D12_QUERY_TYPE_TIMESTAMP, 1);
        ID3D12GraphicsCommandList_ResolveQueryData(d.list, d.queries, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                                                   d.timestamps.resource, 0);
    }
    ID3D12GraphicsCommandList_Close(d.list);
    ID3D12CommandList *lists[1] = {(ID3D12CommandList *)d.list};
    ID3D12CommandQueue_ExecuteCommandLists(d.queue, 1, lists);
    d.fence_value++;
    ID3D12CommandQueue_Signal(d.queue, d.fence, d.fence_value);
    d.pending = 1;
}

static int32_t d3d_present(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                           const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count,
                           uint32_t width, uint32_t height, float r, float g, float b) {
    if (d.swapchain == NULL) {
        return 1;
    }
    d3d_finish();
    if (!ensure_swapchain(width, height)) {
        return 2;
    }
    cw_texture *target = &d.swap[IDXGISwapChain3_GetCurrentBackBufferIndex(d.swapchain)];
    if (!record(target, frame, count, layers, layer_instances, records, layer_count, clips, clip_count, width, height,
                r, g, b)) {
        return 3;
    }
    transition(target->resource, &target->state, D3D12_RESOURCE_STATE_PRESENT);
    submit();
    HRESULT hr = IDXGISwapChain3_Present(d.swapchain, 1, 0);
    static int shown = 0;
    if (shown++ < 3) {
        trace("presented", hr);
    }
    return SUCCEEDED(hr) ? 0 : 4;
}

static int32_t d3d_render_to(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                             const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count,
                             uint32_t width, uint32_t height, float r, float g, float b, uint32_t *out) {
    if (!d.ready) {
        return 1;
    }
    d3d_finish();
    cw_texture *o = &d.offscreen;
    if (o->resource == NULL || o->width != width || o->height != height) {
        if (o->resource != NULL) {
            ID3D12Resource_Release(o->resource);
        }
        o->resource = make_texture(width, height, CW_FORMAT, 1, D3D12_RESOURCE_STATE_RENDER_TARGET);
        if (o->resource == NULL) {
            return 2;
        }
        o->state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        o->rtv = 2;
        o->width = width;
        o->height = height;
        ID3D12Device_CreateRenderTargetView(d.device, o->resource, NULL, rtv_handle(2));
    }
    UINT pitch = (width * 4 + 255) & ~255u;
    if (out != NULL && !ensure_buffer(&d.readback, (UINT64)pitch * height, D3D12_HEAP_TYPE_READBACK)) {
        return 3;
    }
    if (!record(o, frame, count, layers, layer_instances, records, layer_count, clips, clip_count, width, height, r, g,
                b)) {
        return 4;
    }
    if (out != NULL) {
        transition(o->resource, &o->state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION from = subresource(o->resource);
        D3D12_TEXTURE_COPY_LOCATION to;
        memset(&to, 0, sizeof to);
        to.pResource = d.readback.resource;
        to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint.Footprint.Format = CW_FORMAT;
        to.PlacedFootprint.Footprint.Width = width;
        to.PlacedFootprint.Footprint.Height = height;
        to.PlacedFootprint.Footprint.Depth = 1;
        to.PlacedFootprint.Footprint.RowPitch = pitch;
        ID3D12GraphicsCommandList_CopyTextureRegion(d.list, &to, 0, 0, 0, &from, NULL);
    }
    submit();
    d3d_finish();
    if (out != NULL) {
        for (uint32_t y = 0; y < height; y++) {
            memcpy(out + (size_t)y * width, (const uint8_t *)d.readback.mapped + (size_t)y * pitch, (size_t)width * 4);
        }
    }
    return 0;
}

/* ---- the calls: Direct3D 12, or Vulkan when chosen or when Direct3D 12 cannot start -------------------------- */

static int use_vulkan;

int32_t cw_gpu_init(uint32_t atlas_width, uint32_t atlas_height) {
    const char *want = getenv("CLAYWIND_GPU");
    if (want == NULL || strcmp(want, "vulkan") != 0) {
        int32_t r = d3d_init(atlas_width, atlas_height);
        if (r == 0) {
            use_vulkan = 0;
            trace("using Direct3D 12", 0);
            return 0;
        }
        trace("Direct3D 12 did not start", r);
    }
    use_vulkan = 1;
    return cw_vk_init(atlas_width, atlas_height);
}

int32_t cw_gpu_attach(int32_t kind, uint64_t first, uint64_t second) {
    return use_vulkan ? cw_vk_attach(kind, first, second) : d3d_attach(kind, first, second);
}

void cw_gpu_atlas(const uint8_t *pixels, uint32_t stride, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    if (use_vulkan) {
        cw_vk_atlas(pixels, stride, x0, y0, x1, y1);
    } else {
        d3d_atlas(pixels, stride, x0, y0, x1, y1);
    }
}

int32_t cw_gpu_present(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                       const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count,
                       uint32_t width, uint32_t height, float r, float g, float b) {
    return use_vulkan ? cw_vk_present(frame, count, layers, layer_instances, records, layer_count, clips, clip_count,
                                      width, height, r, g, b)
                      : d3d_present(frame, count, layers, layer_instances, records, layer_count, clips, clip_count,
                                    width, height, r, g, b);
}

int32_t cw_gpu_render_to(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                         const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count,
                         uint32_t width, uint32_t height, float r, float g, float b, uint32_t *out) {
    return use_vulkan ? cw_vk_render_to(frame, count, layers, layer_instances, records, layer_count, clips,
                                        clip_count, width, height, r, g, b, out)
                      : d3d_render_to(frame, count, layers, layer_instances, records, layer_count, clips, clip_count,
                                      width, height, r, g, b, out);
}

int32_t cw_gpu_render_only(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                           const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count,
                           uint32_t width, uint32_t height, float r, float g, float b) {
    return cw_gpu_render_to(frame, count, layers, layer_instances, records, layer_count, clips, clip_count, width,
                            height, r, g, b, NULL);
}

double cw_gpu_gpu_ms(void) { return use_vulkan ? cw_vk_gpu_ms() : d.gpu_ms; }

void cw_gpu_finish(void) {
    if (use_vulkan) {
        cw_vk_finish();
    } else {
        d3d_finish();
    }
}
#else
typedef int cw_d3d12_absent; /* not Windows: metal.c or vulkan.c draws */
#endif
