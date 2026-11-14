/* SPDX-License-Identifier: MIT */
/*
 * ClayWind's Metal renderer (the GPU spike): the instances that gpu.cee's GpuBackend writes, drawn in one instanced
 * draw per frame. Every shape is a quad whose fragment shader computes coverage from a signed distance: rounded
 * rectangles (fills, borders and rings as an outer minus an inner one), blurred shadows, linear and radial gradients
 * with up to four stops, and glyph and icon masks from a coverage atlas that Raster itself rasterized.
 *
 * Objective-C through objc_msgSend; Metal, QuartzCore and CoreGraphics loaded with dlopen, so the package needs no
 * link flags (as CeeSafe's window runtime does). macOS only.
 */
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
int32_t cw_metal_init(uint32_t atlas_width, uint32_t atlas_height);
int32_t cw_metal_attach(uint64_t nswindow, uint64_t nsview);
void cw_metal_atlas(const uint8_t *pixels, uint32_t stride, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
int32_t cw_metal_present(const float *instances, uint32_t count, uint32_t width, uint32_t height, float r, float g,
                         float b);
int32_t cw_metal_render_to(const float *instances, uint32_t count, uint32_t width, uint32_t height, float r, float g,
                           float b, uint32_t *out);
int32_t cw_metal_render_only(const float *instances, uint32_t count, uint32_t width, uint32_t height);
double cw_metal_gpu_ms(void);
void cw_metal_finish(void);

#define CW_FLOATS 52 /* gpu.cee's FLOATS: 13 float4s per instance */

static const char *cw_shader =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct Inst { float4 rect; float4 radii; float4 c0; float4 c1; float4 c2; float4 c3; float4 stops;\n"
    "              float4 params; float4 clip; float4 clipShape; float4 clipRadii; float4 misc; float4 extra; };\n"
    "struct VOut { float4 position [[position]]; uint inst [[flat]]; };\n"
    "vertex VOut vmain(uint vid [[vertex_id]], uint iid [[instance_id]], const device Inst *insts [[buffer(0)]],\n"
    "                  constant float2 &viewport [[buffer(1)]]) {\n"
    "    const device Inst &I = insts[iid];\n"
    "    float outset = I.misc.w + 1.0;\n"
    "    float4 r = float4(I.rect.xy - outset, I.rect.xy + I.rect.zw + outset);\n"
    "    r = float4(max(r.xy, I.clip.xy), min(r.zw, I.clip.zw));\n"
    "    r.zw = max(r.zw, r.xy);\n"
    "    float2 c = float2((vid & 1) ? r.z : r.x, (vid & 2) ? r.w : r.y);\n"
    "    VOut o;\n"
    "    o.position = float4(c.x / viewport.x * 2.0 - 1.0, 1.0 - c.y / viewport.y * 2.0, 0.0, 1.0);\n"
    "    o.inst = iid;\n"
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
    "    float span = pb - pa;\n"
    "    float f = span > 0.00001 ? (u - pa) / span : 0.0;\n"
    "    float4 c = mix(a, b, f);\n"
    "    return float4(c.rgb * c.a, c.a);\n"
    "}\n"
    "fragment float4 fmain(VOut in [[stage_in]], const device Inst *insts [[buffer(0)]],\n"
    "                      texture2d<float, access::read> atlas [[texture(0)]]) {\n"
    "    const device Inst &I = insts[in.inst];\n"
    "    float2 p = in.position.xy;\n"
    "    if (p.x < I.clip.x || p.y < I.clip.y || p.x >= I.clip.z || p.y >= I.clip.w) { discard_fragment(); }\n"
    "    float k = 1.0;\n"
    "    if (any(I.clipRadii > 0.0)) { k = cover(sdRR(p, I.clipShape, I.clipRadii)); }\n"
    "    int kind = int(I.misc.x);\n"
    "    float4 color;\n"
    "    if (kind == 5) {\n"
    "        uint2 t = uint2(p - I.rect.xy + I.params.xy);\n"
    "        color = I.c0 * atlas.read(t).r;\n"
    "    } else if (kind == 0) {\n"
    "        color = I.c0 * cover(sdRR(p, I.rect, I.radii));\n"
    "    } else if (kind == 1) {\n"
    "        float o = cover(sdRR(p, I.rect, I.radii));\n"
    "        float i = (I.params.z > 0.0 && I.params.w > 0.0) ? cover(sdRR(p, I.params, I.c1)) : 0.0;\n"
    "        color = I.c0 * max(o - i, 0.0);\n"
    "    } else if (kind == 2) {\n"
    "        float d = sdRR(p, I.rect, I.radii);\n"
    "        float s = I.params.x;\n"
    "        float a = s > 0.0 ? 0.5 * (1.0 - erfApprox(d / (s * 1.41421356))) : cover(d);\n"
    "        color = I.c0 * a;\n"
    "    } else {\n"
    "        float t = kind == 3 ? dot(p - I.params.xy, I.params.zw) / I.extra.z\n"
    "                            : length(p - I.params.xy) / I.params.z;\n"
    "        color = gradColor(I, t) * cover(sdRR(p, I.rect, I.radii));\n"
    "    }\n"
    "    return color * (k * I.misc.y);\n"
    "}\n";

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

static struct {
    id device, queue, library;
    id pipeline_window;    /* BGRA8Unorm, the window's drawables */
    id pipeline_offscreen; /* RGBA8Unorm, read back as Raster's byte order */
    id atlas;
    id layer, view, window;
    id offscreen;
    uint32_t offscreen_width, offscreen_height;
    id last; /* the last command buffer committed */
    double gpu_ms;
} m;

#define SEND0(ret) ((ret(*)(id, SEL))cw.send)
#define SEND(ret, ...) ((ret(*)(id, SEL, __VA_ARGS__))cw.send)

static id call(id o, const char *s) { return SEND0(id)(o, cw.sel(s)); }
static id nsstring(const char *text) { return SEND(id, const char *)(cw.cls("NSString"), cw.sel("stringWithUTF8String:"), text); }

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

static id make_pipeline(unsigned long format) {
    id error = NULL;
    id descriptor = call(call(cw.cls("MTLRenderPipelineDescriptor"), "alloc"), "init");
    SEND(void, id)(descriptor, cw.sel("setVertexFunction:"),
                   SEND(id, id)(m.library, cw.sel("newFunctionWithName:"), nsstring("vmain")));
    SEND(void, id)(descriptor, cw.sel("setFragmentFunction:"),
                   SEND(id, id)(m.library, cw.sel("newFunctionWithName:"), nsstring("fmain")));
    id attachment = SEND(id, unsigned long)(call(descriptor, "colorAttachments"), cw.sel("objectAtIndexedSubscript:"), 0ul);
    SEND(void, unsigned long)(attachment, cw.sel("setPixelFormat:"), format);
    SEND(void, signed char)(attachment, cw.sel("setBlendingEnabled:"), 1);
    /* Premultiplied source over: one, one minus source alpha (MTLBlendFactorOne 1, OneMinusSourceAlpha 5). */
    SEND(void, unsigned long)(attachment, cw.sel("setSourceRGBBlendFactor:"), 1ul);
    SEND(void, unsigned long)(attachment, cw.sel("setSourceAlphaBlendFactor:"), 1ul);
    SEND(void, unsigned long)(attachment, cw.sel("setDestinationRGBBlendFactor:"), 5ul);
    SEND(void, unsigned long)(attachment, cw.sel("setDestinationAlphaBlendFactor:"), 5ul);
    id pipeline = SEND(id, id, id *)(m.device, cw.sel("newRenderPipelineStateWithDescriptor:error:"), descriptor, &error);
    if (pipeline == NULL) {
        report("the pipeline", error);
    }
    call(descriptor, "release");
    return pipeline;
}

int32_t cw_metal_init(uint32_t atlas_width, uint32_t atlas_height) {
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
    id error = NULL;
    m.library = SEND(id, id, id, id *)(m.device, cw.sel("newLibraryWithSource:options:error:"), nsstring(cw_shader),
                                       NULL, &error);
    if (m.library == NULL) {
        report("the shaders", error);
        cw.pool_pop(pool);
        return 3;
    }
    m.pipeline_window = make_pipeline(80ul);    /* MTLPixelFormatBGRA8Unorm */
    m.pipeline_offscreen = make_pipeline(70ul); /* MTLPixelFormatRGBA8Unorm */
    /* The coverage atlas: R8Unorm (10), read by the fragment shader. */
    id descriptor = SEND(id, unsigned long, unsigned long, unsigned long, signed char)(
        cw.cls("MTLTextureDescriptor"), cw.sel("texture2DDescriptorWithPixelFormat:width:height:mipmapped:"), 10ul,
        (unsigned long)atlas_width, (unsigned long)atlas_height, 0);
    SEND(void, unsigned long)(descriptor, cw.sel("setUsage:"), 1ul); /* ShaderRead */
    m.atlas = SEND(id, id)(m.device, cw.sel("newTextureWithDescriptor:"), descriptor);
    cw.pool_pop(pool);
    return m.pipeline_window != NULL && m.pipeline_offscreen != NULL && m.atlas != NULL ? 0 : 4;
}

int32_t cw_metal_attach(uint64_t nswindow, uint64_t nsview) {
    if (m.device == NULL) {
        return 1;
    }
    void *pool = cw.pool_push();
    m.window = (id)(uintptr_t)nswindow;
    m.view = (id)(uintptr_t)nsview;
    m.layer = call(call(cw.cls("CAMetalLayer"), "alloc"), "init");
    SEND(void, id)(m.layer, cw.sel("setDevice:"), m.device);
    SEND(void, unsigned long)(m.layer, cw.sel("setPixelFormat:"), 80ul);
    SEND(void, signed char)(m.layer, cw.sel("setFramebufferOnly:"), 1);
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
void cw_metal_finish(void) {
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

double cw_metal_gpu_ms(void) { return m.gpu_ms; }

/* Copies the atlas rows [y0, y1) and columns [x0, x1) from `pixels` (a `stride`-wide R8 image) to the texture. */
void cw_metal_atlas(const uint8_t *pixels, uint32_t stride, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    if (m.atlas == NULL || x1 <= x0 || y1 <= y0) {
        return;
    }
    cw_metal_finish(); /* the last frame may still read the texture */
    cw_region region = {{x0, y0, 0}, {x1 - x0, y1 - y0, 1}};
    SEND(void, cw_region, unsigned long, const void *, unsigned long)(
        m.atlas, cw.sel("replaceRegion:mipmapLevel:withBytes:bytesPerRow:"), region, 0ul,
        pixels + (size_t)y0 * stride + x0, (unsigned long)stride);
}

/* Encodes one frame's draw into `target` and commits it. */
static void encode(id target, id pipeline, const float *instances, uint32_t count, uint32_t width, uint32_t height,
                   float r, float g, float b, id drawable) {
    id pass = call(cw.cls("MTLRenderPassDescriptor"), "renderPassDescriptor");
    id color = SEND(id, unsigned long)(call(pass, "colorAttachments"), cw.sel("objectAtIndexedSubscript:"), 0ul);
    SEND(void, id)(color, cw.sel("setTexture:"), target);
    SEND(void, unsigned long)(color, cw.sel("setLoadAction:"), 2ul);  /* Clear */
    SEND(void, unsigned long)(color, cw.sel("setStoreAction:"), 1ul); /* Store */
    cw_clear clear = {r, g, b, 1.0};
    SEND(void, cw_clear)(color, cw.sel("setClearColor:"), clear);
    id buffer = call(m.queue, "commandBuffer");
    id encoder = SEND(id, id)(buffer, cw.sel("renderCommandEncoderWithDescriptor:"), pass);
    if (count > 0) {
        id data = SEND(id, const void *, unsigned long, unsigned long)(
            m.device, cw.sel("newBufferWithBytes:length:options:"), instances,
            (unsigned long)count * CW_FLOATS * sizeof(float), 0ul);
        float viewport[2] = {(float)width, (float)height};
        SEND(void, id)(encoder, cw.sel("setRenderPipelineState:"), pipeline);
        SEND(void, id, unsigned long, unsigned long)(encoder, cw.sel("setVertexBuffer:offset:atIndex:"), data, 0ul, 0ul);
        SEND(void, const void *, unsigned long, unsigned long)(encoder, cw.sel("setVertexBytes:length:atIndex:"),
                                                               viewport, sizeof viewport, 1ul);
        SEND(void, id, unsigned long, unsigned long)(encoder, cw.sel("setFragmentBuffer:offset:atIndex:"), data, 0ul,
                                                     0ul);
        SEND(void, id, unsigned long)(encoder, cw.sel("setFragmentTexture:atIndex:"), m.atlas, 0ul);
        /* A triangle strip of 4 vertices per instance (MTLPrimitiveTypeTriangleStrip 4). */
        SEND(void, unsigned long, unsigned long, unsigned long, unsigned long)(
            encoder, cw.sel("drawPrimitives:vertexStart:vertexCount:instanceCount:"), 4ul, 0ul, 4ul,
            (unsigned long)count);
        call(data, "release");
    }
    call(encoder, "endEncoding");
    if (drawable != NULL) {
        SEND(void, id)(buffer, cw.sel("presentDrawable:"), drawable);
    }
    call(buffer, "commit");
    m.last = call(buffer, "retain");
}

/* Draws the frame into the window's next drawable, `width` × `height` pixels, and presents it. */
int32_t cw_metal_present(const float *instances, uint32_t count, uint32_t width, uint32_t height, float r, float g,
                         float b) {
    if (m.layer == NULL) {
        return 1;
    }
    cw_metal_finish();
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
    encode(call(drawable, "texture"), m.pipeline_window, instances, count, width, height, r, g, b, drawable);
    cw.pool_pop(pool);
    return 0;
}

/* Draws the frame into an offscreen texture and copies its pixels to `out` (RGBA bytes, Raster's order). */
int32_t cw_metal_render_to(const float *instances, uint32_t count, uint32_t width, uint32_t height, float r, float g,
                           float b, uint32_t *out) {
    if (m.device == NULL) {
        return 1;
    }
    cw_metal_finish();
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
    encode(m.offscreen, m.pipeline_offscreen, instances, count, width, height, r, g, b, NULL);
    cw_metal_finish();
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
int32_t cw_metal_render_only(const float *instances, uint32_t count, uint32_t width, uint32_t height) {
    return cw_metal_render_to(instances, count, width, height, 0.0f, 0.0f, 0.0f, NULL);
}
