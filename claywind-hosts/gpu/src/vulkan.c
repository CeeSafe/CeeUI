/* SPDX-License-Identifier: MIT */
/*
 * ClayWind's Vulkan renderer: the cw_gpu_* calls of metal.c, on Linux and Windows. The same instances and layer
 * records gpu.cee writes, drawn by the same shaders in GLSL (shaders/, compiled to SPIR-V in vulkan_shaders.h):
 *
 * - each layer, in the order it closed, into its own texture of its box, then its blur's box passes;
 * - the frame, into the window's swapchain image or an offscreen image that is read back.
 *
 * A layer composite reads the parent's pixel through an input attachment that is also the colour attachment (the
 * GENERAL layout, a subpass self-dependency, and a barrier before each composite), which is Vulkan's framebuffer
 * fetch. One frame is in flight: a frame waits for the last one's fence before it reuses anything.
 *
 * libvulkan is loaded with dlopen (vulkan-1.dll with LoadLibrary), so the package needs no link flags; the Vulkan
 * headers are needed to build. On Windows, Direct3D 12 (d3d12.c) is the default and this is the fallback
 * (CLAYWIND_GPU=vulkan chooses it). macOS draws with metal.c, and this file compiles to nothing there (a manifest cannot
 * name a C file per system).
 */
#if !defined(__APPLE__)
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "vulkan_shaders.h"

/* CW_TRACE=1: each step of attaching and presenting, and the first frames' results, to stderr. */
static int cw_trace = -1;
static void trace(const char *what, long long value) {
    if (cw_trace < 0) {
        cw_trace = getenv("CW_TRACE") != NULL;
    }
    if (cw_trace) {
        fprintf(stderr, "vulkan: %s %lld\n", what, value);
        fflush(stderr);
    }
}

/* On Windows, d3d12.c answers the cw_gpu_* calls and hands them to these (cw_vk_*) when Vulkan is chosen; elsewhere
   these are the cw_gpu_* calls. */
#if defined(_WIN32)
#define CW_VK(name) cw_vk_##name
#else
#define CW_VK(name) cw_gpu_##name
#endif

/* The calls gpu.cee and main.cee make (metal.c's). */
int32_t CW_VK(init)(uint32_t atlas_width, uint32_t atlas_height, uint32_t images);
int32_t CW_VK(attach)(int32_t kind, uint64_t first, uint64_t second);
void CW_VK(atlas)(const uint8_t *pixels, uint32_t stride, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
void CW_VK(image)(const uint32_t *pixels, uint32_t stride, uint32_t w, uint32_t h, uint32_t u, uint32_t v);
int32_t CW_VK(present)(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                       const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count, uint32_t width, uint32_t height, float r, float g,
                       float b);
int32_t CW_VK(render_to)(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                         const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count, uint32_t width, uint32_t height, float r, float g,
                         float b, uint32_t *out);
int32_t CW_VK(render_only)(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                           const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count, uint32_t width, uint32_t height, float r,
                           float g, float b);
double CW_VK(gpu_ms)(void);
void CW_VK(finish)(void);

#define CW_FLOATS 52 /* gpu.cee's FLOATS: 13 vec4s per instance */
#define CW_SHAPE 20  /* gpu.cee's SHAPE: 5 vec4s per clip shape */
#define CW_RECORD 8  /* gpu.cee's RECORD */
#define CW_KIND 36 /* misc.x */
#define CW_KIND_LAYER 6.0f
#define CW_MAX_LAYERS 256
#define CW_POOL 96
#define CW_SWAP 8

/* ---- the loader ---------------------------------------------------------------------------------------------- */

#define CW_INSTANCE_FUNCS(X)                                                                                         \
    X(vkDestroyInstance)                                                                                             \
    X(vkEnumeratePhysicalDevices)                                                                                    \
    X(vkGetPhysicalDeviceProperties)                                                                                 \
    X(vkGetPhysicalDeviceQueueFamilyProperties)                                                                      \
    X(vkGetPhysicalDeviceMemoryProperties)                                                                           \
    X(vkEnumerateDeviceExtensionProperties)                                                                          \
    X(vkCreateDevice)                                                                                                \
    X(vkGetDeviceProcAddr)                                                                                           \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)                                                                          \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                                                                     \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)                                                                          \
    X(vkDestroySurfaceKHR)

#define CW_DEVICE_FUNCS(X)                                                                                           \
    X(vkGetDeviceQueue)                                                                                              \
    X(vkDeviceWaitIdle)                                                                                              \
    X(vkCreateCommandPool)                                                                                           \
    X(vkAllocateCommandBuffers)                                                                                      \
    X(vkResetCommandBuffer)                                                                                          \
    X(vkBeginCommandBuffer)                                                                                          \
    X(vkEndCommandBuffer)                                                                                            \
    X(vkQueueSubmit)                                                                                                 \
    X(vkCreateFence)                                                                                                 \
    X(vkWaitForFences)                                                                                               \
    X(vkResetFences)                                                                                                 \
    X(vkCreateSemaphore)                                                                                             \
    X(vkCreateBuffer)                                                                                                \
    X(vkDestroyBuffer)                                                                                               \
    X(vkGetBufferMemoryRequirements)                                                                                 \
    X(vkAllocateMemory)                                                                                              \
    X(vkFreeMemory)                                                                                                  \
    X(vkBindBufferMemory)                                                                                            \
    X(vkMapMemory)                                                                                                   \
    X(vkCreateImage)                                                                                                 \
    X(vkDestroyImage)                                                                                                \
    X(vkGetImageMemoryRequirements)                                                                                  \
    X(vkBindImageMemory)                                                                                             \
    X(vkCreateImageView)                                                                                             \
    X(vkDestroyImageView)                                                                                            \
    X(vkCreateSampler)                                                                                               \
    X(vkCreateRenderPass)                                                                                            \
    X(vkCreateFramebuffer)                                                                                           \
    X(vkDestroyFramebuffer)                                                                                          \
    X(vkCreateShaderModule)                                                                                          \
    X(vkCreatePipelineLayout)                                                                                        \
    X(vkCreateGraphicsPipelines)                                                                                     \
    X(vkCreateDescriptorSetLayout)                                                                                   \
    X(vkCreateDescriptorPool)                                                                                        \
    X(vkResetDescriptorPool)                                                                                         \
    X(vkAllocateDescriptorSets)                                                                                      \
    X(vkUpdateDescriptorSets)                                                                                        \
    X(vkCreateQueryPool)                                                                                             \
    X(vkGetQueryPoolResults)                                                                                         \
    X(vkCmdBeginRenderPass)                                                                                          \
    X(vkCmdEndRenderPass)                                                                                            \
    X(vkCmdBindPipeline)                                                                                             \
    X(vkCmdBindDescriptorSets)                                                                                       \
    X(vkCmdPushConstants)                                                                                            \
    X(vkCmdDraw)                                                                                                     \
    X(vkCmdPipelineBarrier)                                                                                          \
    X(vkCmdCopyBufferToImage)                                                                                        \
    X(vkCmdCopyImageToBuffer)                                                                                        \
    X(vkCmdWriteTimestamp)                                                                                           \
    X(vkCmdResetQueryPool)                                                                                           \
    X(vkCmdSetViewport)                                                                                              \
    X(vkCmdSetScissor)                                                                                               \
    X(vkCreateSwapchainKHR)                                                                                          \
    X(vkDestroySwapchainKHR)                                                                                         \
    X(vkGetSwapchainImagesKHR)                                                                                       \
    X(vkAcquireNextImageKHR)                                                                                         \
    X(vkQueuePresentKHR)

#define CW_DECLARE(name) static PFN_##name name;
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
static PFN_vkCreateInstance vkCreateInstance;
static PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
CW_INSTANCE_FUNCS(CW_DECLARE)
CW_DEVICE_FUNCS(CW_DECLARE)

/* The surface structures of the platform headers (vulkan_xlib.h, vulkan_win32.h), without those headers' X11 and
   Windows includes: the handles travel as plain pointers and integers. */
typedef struct {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    void *display;
    unsigned long window;
} cw_xlib_surface_info;
typedef struct {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    void *hinstance;
    void *hwnd;
} cw_win32_surface_info;
typedef VkResult(VKAPI_PTR *cw_create_surface)(VkInstance, const void *, const VkAllocationCallbacks *,
                                                VkSurfaceKHR *);

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *mapped;
    VkDeviceSize size;
} cw_buffer;

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFramebuffer framebuffer; /* for rp_clear and rp_load, which are compatible */
    uint32_t width, height;
    int used;
} cw_texture;

static struct {
    int ready;
    VkInstance instance;
    VkPhysicalDevice gpu;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    VkPhysicalDeviceMemoryProperties memory;
    int has_xlib, has_win32, has_swapchain;
    double timestamp_period; /* nanoseconds per tick, 0 without timestamps */
    VkCommandPool commands;
    VkCommandBuffer cmd;
    VkFence fence;
    int pending;
    VkQueryPool queries;
    VkDescriptorSetLayout set_layouts[3];
    VkPipelineLayout layout;
    VkDescriptorPool descriptors;
    VkShaderModule modules[6];
    VkRenderPass rp_clear, rp_load, rp_present; /* RGBA8 cleared, RGBA8 overwritten, the swapchain's */
    VkPipeline shapes_rgba, fills_rgba, composite_rgba, box;
    VkPipeline shapes_present, fills_present, composite_present;
    VkSampler linear, nearest;
    VkImage atlas;
    VkDeviceMemory atlas_memory;
    VkImageView atlas_view;
    uint32_t atlas_width, atlas_height;
    int atlas_ready; /* in SHADER_READ_ONLY_OPTIMAL */
    int atlas_dirty;
    uint32_t ax0, ay0, ax1, ay1;
    /* The image atlas (RGBA8, straight alpha), uploaded from its own staging buffer as the coverage atlas is. */
    VkImage images;
    VkDeviceMemory images_memory;
    VkImageView images_view;
    uint32_t images_side;
    int images_ready, images_dirty;
    uint32_t ix0, iy0, ix1, iy1;
    cw_buffer staging, image_staging, instances, clips, readback;
    cw_texture pool[CW_POOL];
    cw_texture offscreen;
    VkSurfaceKHR surface;
    VkFormat present_format;
    VkSwapchainKHR swapchain;
    uint32_t swap_count, swap_width, swap_height;
    VkImage swap_images[CW_SWAP];
    VkImageView swap_views[CW_SWAP];
    VkFramebuffer swap_framebuffers[CW_SWAP];
    VkSemaphore acquired, rendered;
    double gpu_ms;
} g;

static void *open_library(void) {
#if defined(_WIN32)
    return (void *)LoadLibraryA("vulkan-1.dll");
#else
    void *h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    return h != NULL ? h : dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#endif
}

static PFN_vkVoidFunction library_symbol(void *library, const char *name) {
#if defined(_WIN32)
    FARPROC p = GetProcAddress((HMODULE)library, name);
#else
    void *p = dlsym(library, name);
#endif
    PFN_vkVoidFunction f;
    memcpy(&f, &p, sizeof f); /* ISO C has no object-to-function pointer conversion; POSIX guarantees this one */
    return f;
}

#define CW_LOAD(f, getter, owner) memcpy(&f, &(PFN_vkVoidFunction){getter(owner, #f)}, sizeof f);

static int has_extension(const VkExtensionProperties *list, uint32_t count, const char *name) {
    for (uint32_t k = 0; k < count; k++) {
        if (strcmp(list[k].extensionName, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags wanted) {
    for (uint32_t k = 0; k < g.memory.memoryTypeCount; k++) {
        if ((bits & (1u << k)) && (g.memory.memoryTypes[k].propertyFlags & wanted) == wanted) {
            return k;
        }
    }
    return UINT32_MAX;
}

/* ---- buffers and images ------------------------------------------------------------------------------------- */

/* Makes `b` hold at least `size` bytes, host-visible and mapped (only between frames: nothing may use it). */
static int ensure_buffer(cw_buffer *b, VkDeviceSize size, VkBufferUsageFlags usage) {
    if (b->buffer != VK_NULL_HANDLE && b->size >= size) {
        return 1;
    }
    if (b->buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(g.device, b->buffer, NULL);
        vkFreeMemory(g.device, b->memory, NULL);
        memset(b, 0, sizeof *b);
    }
    VkDeviceSize rounded = 65536;
    while (rounded < size) {
        rounded *= 2;
    }
    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                               .size = rounded,
                               .usage = usage,
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (vkCreateBuffer(g.device, &info, NULL, &b->buffer) != VK_SUCCESS) {
        return 0;
    }
    VkMemoryRequirements need;
    vkGetBufferMemoryRequirements(g.device, b->buffer, &need);
    uint32_t type = memory_type(need.memoryTypeBits,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = need.size, .memoryTypeIndex = type};
    if (type == UINT32_MAX || vkAllocateMemory(g.device, &alloc, NULL, &b->memory) != VK_SUCCESS) {
        return 0;
    }
    vkBindBufferMemory(g.device, b->buffer, b->memory, 0);
    vkMapMemory(g.device, b->memory, 0, VK_WHOLE_SIZE, 0, &b->mapped);
    b->size = rounded;
    return 1;
}

static int make_image(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage *image,
                      VkDeviceMemory *memory, VkImageView *view) {
    VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .imageType = VK_IMAGE_TYPE_2D,
                              .format = format,
                              .extent = {width, height, 1},
                              .mipLevels = 1,
                              .arrayLayers = 1,
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .usage = usage,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    if (vkCreateImage(g.device, &info, NULL, image) != VK_SUCCESS) {
        return 0;
    }
    VkMemoryRequirements need;
    vkGetImageMemoryRequirements(g.device, *image, &need);
    uint32_t type = memory_type(need.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) {
        type = memory_type(need.memoryTypeBits, 0);
    }
    VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = need.size, .memoryTypeIndex = type};
    if (vkAllocateMemory(g.device, &alloc, NULL, memory) != VK_SUCCESS) {
        return 0;
    }
    vkBindImageMemory(g.device, *image, *memory, 0);
    VkImageViewCreateInfo vinfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                   .image = *image,
                                   .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                   .format = format,
                                   .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    return vkCreateImageView(g.device, &vinfo, NULL, view) == VK_SUCCESS;
}

static VkFramebuffer make_framebuffer(VkRenderPass pass, VkImageView view, uint32_t width, uint32_t height) {
    VkFramebufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                                    .renderPass = pass,
                                    .attachmentCount = 1,
                                    .pAttachments = &view,
                                    .width = width,
                                    .height = height,
                                    .layers = 1};
    VkFramebuffer fb = VK_NULL_HANDLE;
    vkCreateFramebuffer(g.device, &info, NULL, &fb);
    return fb;
}

static const VkImageUsageFlags cw_target_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                 VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

/* An RGBA8 texture of this size from the pool, for a layer or a blur pass. */
static cw_texture *pooled(uint32_t width, uint32_t height) {
    int free_slot = -1;
    for (int k = 0; k < CW_POOL; k++) {
        cw_texture *t = &g.pool[k];
        if (t->image != VK_NULL_HANDLE && !t->used && t->width == width && t->height == height) {
            t->used = 1;
            return t;
        }
        if (t->image == VK_NULL_HANDLE && free_slot < 0) {
            free_slot = k;
        }
    }
    if (free_slot < 0) { /* the pool is full: drop an unused texture of another size */
        for (int k = 0; k < CW_POOL; k++) {
            cw_texture *t = &g.pool[k];
            if (!t->used) {
                vkDestroyFramebuffer(g.device, t->framebuffer, NULL);
                vkDestroyImageView(g.device, t->view, NULL);
                vkDestroyImage(g.device, t->image, NULL);
                vkFreeMemory(g.device, t->memory, NULL);
                memset(t, 0, sizeof *t);
                free_slot = k;
                break;
            }
        }
        if (free_slot < 0) {
            return NULL;
        }
    }
    cw_texture *t = &g.pool[free_slot];
    if (!make_image(width, height, VK_FORMAT_R8G8B8A8_UNORM, cw_target_usage, &t->image, &t->memory, &t->view)) {
        return NULL;
    }
    t->framebuffer = make_framebuffer(g.rp_clear, t->view, width, height);
    t->width = width;
    t->height = height;
    t->used = 1;
    return t;
}

/* ---- render passes and pipelines ---------------------------------------------------------------------------- */

/* One subpass whose colour attachment is also its input attachment, in the GENERAL layout, with the dependencies a
   layer's texture and a composite need: what earlier passes wrote is visible to this one's sampling and blending;
   a draw's writes are visible to a later composite's input reads (the self-dependency, with a barrier per
   composite); this pass's writes are visible to later sampling, copies and presentation. */
static VkRenderPass make_render_pass(VkFormat format, VkAttachmentLoadOp load, VkImageLayout final_layout) {
    VkAttachmentDescription attachment = {.format = format,
                                          .samples = VK_SAMPLE_COUNT_1_BIT,
                                          .loadOp = load,
                                          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                          .finalLayout = final_layout};
    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_GENERAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    .inputAttachmentCount = 1,
                                    .pInputAttachments = &ref,
                                    .colorAttachmentCount = 1,
                                    .pColorAttachments = &ref};
    VkPipelineStageFlags any = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                               VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkAccessFlags reads = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
                          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    VkSubpassDependency deps[3] = {
        {VK_SUBPASS_EXTERNAL, 0, any, any, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
         reads | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0},
        {0, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_INPUT_ATTACHMENT_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT},
        {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         any | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, reads, 0},
    };
    VkRenderPassCreateInfo info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                   .attachmentCount = 1,
                                   .pAttachments = &attachment,
                                   .subpassCount = 1,
                                   .pSubpasses = &subpass,
                                   .dependencyCount = 3,
                                   .pDependencies = deps};
    VkRenderPass pass = VK_NULL_HANDLE;
    vkCreateRenderPass(g.device, &info, NULL, &pass);
    return pass;
}

static VkShaderModule make_module(const uint32_t *code, size_t bytes) {
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = bytes, .pCode = code};
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(g.device, &info, NULL, &module);
    return module;
}

static VkPipeline make_pipeline(VkRenderPass pass, VkShaderModule vert, VkShaderModule frag, int blending) {
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vert,
         .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = frag,
         .pName = "main"},
    };
    VkPipelineVertexInputStateCreateInfo input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP};
    VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                     .polygonMode = VK_POLYGON_MODE_FILL,
                                                     .cullMode = VK_CULL_MODE_NONE,
                                                     .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                                     .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    /* Premultiplied source over: one, one minus source alpha. */
    VkPipelineColorBlendAttachmentState blend = {.blendEnable = blending ? VK_TRUE : VK_FALSE,
                                                 .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                                                 .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                                 .colorBlendOp = VK_BLEND_OP_ADD,
                                                 .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                                                 .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                                 .alphaBlendOp = VK_BLEND_OP_ADD,
                                                 .colorWriteMask = 0xf};
    VkPipelineColorBlendStateCreateInfo blending_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blend};
    VkDynamicState dynamic[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamic};
    VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                         .stageCount = 2,
                                         .pStages = stages,
                                         .pVertexInputState = &input,
                                         .pInputAssemblyState = &assembly,
                                         .pViewportState = &viewport,
                                         .pRasterizationState = &raster,
                                         .pMultisampleState = &multisample,
                                         .pColorBlendState = &blending_state,
                                         .pDynamicState = &dynamic_state,
                                         .layout = g.layout,
                                         .renderPass = pass,
                                         .subpass = 0};
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline) != VK_SUCCESS) {
        fprintf(stderr, "gpu: a Vulkan pipeline failed\n");
    }
    return pipeline;
}

/* ---- setup -------------------------------------------------------------------------------------------------- */

static int create_device(void) {
    void *library = open_library();
    if (library == NULL) {
        return 0;
    }
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)library_symbol(library, "vkGetInstanceProcAddr");
    if (vkGetInstanceProcAddr == NULL) {
        return 0;
    }
    CW_LOAD(vkCreateInstance, vkGetInstanceProcAddr, VK_NULL_HANDLE)
    CW_LOAD(vkEnumerateInstanceExtensionProperties, vkGetInstanceProcAddr, VK_NULL_HANDLE)
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    VkExtensionProperties *available = calloc(count ? count : 1, sizeof *available);
    vkEnumerateInstanceExtensionProperties(NULL, &count, available);
    const char *wanted[3];
    uint32_t n = 0;
    if (has_extension(available, count, VK_KHR_SURFACE_EXTENSION_NAME)) {
        wanted[n++] = VK_KHR_SURFACE_EXTENSION_NAME;
        g.has_xlib = has_extension(available, count, "VK_KHR_xlib_surface");
        g.has_win32 = has_extension(available, count, "VK_KHR_win32_surface");
        if (g.has_xlib) {
            wanted[n++] = "VK_KHR_xlib_surface";
        }
        if (g.has_win32) {
            wanted[n++] = "VK_KHR_win32_surface";
        }
    }
    free(available);
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .pApplicationName = "ClayWind",
                             .apiVersion = VK_API_VERSION_1_0};
    VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                 .pApplicationInfo = &app,
                                 .enabledExtensionCount = n,
                                 .ppEnabledExtensionNames = wanted};
    if (vkCreateInstance(&info, NULL, &g.instance) != VK_SUCCESS) {
        return 0;
    }
#define CW_LOAD_INSTANCE(f) CW_LOAD(f, vkGetInstanceProcAddr, g.instance)
    CW_INSTANCE_FUNCS(CW_LOAD_INSTANCE)
    uint32_t gpus = 0;
    vkEnumeratePhysicalDevices(g.instance, &gpus, NULL);
    if (gpus == 0) {
        return 0;
    }
    VkPhysicalDevice list[16];
    if (gpus > 16) {
        gpus = 16;
    }
    vkEnumeratePhysicalDevices(g.instance, &gpus, list);
    /* A discrete GPU if there is one, else the first with a graphics queue. */
    int best = -1, best_score = -1;
    uint32_t best_family = 0;
    for (uint32_t k = 0; k < gpus; k++) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(list[k], &p);
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(list[k], &families, NULL);
        VkQueueFamilyProperties fam[16];
        if (families > 16) {
            families = 16;
        }
        vkGetPhysicalDeviceQueueFamilyProperties(list[k], &families, fam);
        for (uint32_t f = 0; f < families; f++) {
            if (fam[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                            : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                                                                      : 1;
                if (score > best_score) {
                    best = (int)k;
                    best_score = score;
                    best_family = f;
                }
                break;
            }
        }
    }
    if (best < 0) {
        return 0;
    }
    g.gpu = list[best];
    g.family = best_family;
    {
        VkPhysicalDeviceProperties chosen;
        vkGetPhysicalDeviceProperties(g.gpu, &chosen);
        if (cw_trace < 0) {
            cw_trace = getenv("CW_TRACE") != NULL;
        }
        if (cw_trace) {
            fprintf(stderr, "vulkan: device %s (type %d), %u devices\n", chosen.deviceName, (int)chosen.deviceType, gpus);
        }
    }
    vkGetPhysicalDeviceMemoryProperties(g.gpu, &g.memory);
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(g.gpu, &props);
    uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g.gpu, &families, NULL);
    VkQueueFamilyProperties fam[16];
    if (families > 16) {
        families = 16;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(g.gpu, &families, fam);
    g.timestamp_period = fam[g.family].timestampValidBits > 0 ? (double)props.limits.timestampPeriod : 0.0;
    uint32_t dcount = 0;
    vkEnumerateDeviceExtensionProperties(g.gpu, NULL, &dcount, NULL);
    VkExtensionProperties *dext = calloc(dcount ? dcount : 1, sizeof *dext);
    vkEnumerateDeviceExtensionProperties(g.gpu, NULL, &dcount, dext);
    g.has_swapchain = has_extension(dext, dcount, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    free(dext);
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                     .queueFamilyIndex = g.family,
                                     .queueCount = 1,
                                     .pQueuePriorities = &priority};
    const char *device_ext[1] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dinfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                .queueCreateInfoCount = 1,
                                .pQueueCreateInfos = &queue,
                                .enabledExtensionCount = g.has_swapchain ? 1u : 0u,
                                .ppEnabledExtensionNames = device_ext};
    if (vkCreateDevice(g.gpu, &dinfo, NULL, &g.device) != VK_SUCCESS) {
        return 0;
    }
#define CW_LOAD_DEVICE(f) CW_LOAD(f, vkGetDeviceProcAddr, g.device)
    CW_DEVICE_FUNCS(CW_LOAD_DEVICE)
    vkGetDeviceQueue(g.device, g.family, 0, &g.queue);
    return 1;
}

int32_t CW_VK(init)(uint32_t atlas_width, uint32_t atlas_height, uint32_t images) {
    if (g.ready) {
        return 0;
    }
    if (!create_device()) {
        return 1;
    }
    VkCommandPoolCreateInfo pinfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                     .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                     .queueFamilyIndex = g.family};
    vkCreateCommandPool(g.device, &pinfo, NULL, &g.commands);
    VkCommandBufferAllocateInfo cinfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                         .commandPool = g.commands,
                                         .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                         .commandBufferCount = 1};
    vkAllocateCommandBuffers(g.device, &cinfo, &g.cmd);
    VkFenceCreateInfo finfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(g.device, &finfo, NULL, &g.fence);
    VkSemaphoreCreateInfo sinfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(g.device, &sinfo, NULL, &g.acquired);
    vkCreateSemaphore(g.device, &sinfo, NULL, &g.rendered);
    if (g.timestamp_period > 0.0) {
        VkQueryPoolCreateInfo qinfo = {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2};
        vkCreateQueryPool(g.device, &qinfo, NULL, &g.queries);
    }
    /* Set 0: the instances and the atlas; set 1: a layer (or a blur's source); set 2: the target, as input. */
    VkDescriptorSetLayoutBinding set0[4] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
    };
    VkDescriptorSetLayoutBinding set1 = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                                         NULL};
    VkDescriptorSetLayoutBinding set2 = {0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo l0 = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 4, .pBindings = set0};
    VkDescriptorSetLayoutCreateInfo l1 = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &set1};
    VkDescriptorSetLayoutCreateInfo l2 = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &set2};
    vkCreateDescriptorSetLayout(g.device, &l0, NULL, &g.set_layouts[0]);
    vkCreateDescriptorSetLayout(g.device, &l1, NULL, &g.set_layouts[1]);
    vkCreateDescriptorSetLayout(g.device, &l2, NULL, &g.set_layouts[2]);
    VkPushConstantRange push = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32};
    VkPipelineLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                         .setLayoutCount = 3,
                                         .pSetLayouts = g.set_layouts,
                                         .pushConstantRangeCount = 1,
                                         .pPushConstantRanges = &push};
    vkCreatePipelineLayout(g.device, &layout, NULL, &g.layout);
    VkDescriptorPoolSize sizes[3] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2048},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1024},
    };
    VkDescriptorPoolCreateInfo dpool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                        .maxSets = 3072,
                                        .poolSizeCount = 3,
                                        .pPoolSizes = sizes};
    vkCreateDescriptorPool(g.device, &dpool, NULL, &g.descriptors);
    g.modules[0] = make_module(cw_spv_shapes_vert, sizeof cw_spv_shapes_vert);
    g.modules[1] = make_module(cw_spv_shapes_frag, sizeof cw_spv_shapes_frag);
    g.modules[2] = make_module(cw_spv_fill_frag, sizeof cw_spv_fill_frag);
    g.modules[3] = make_module(cw_spv_composite_frag, sizeof cw_spv_composite_frag);
    g.modules[4] = make_module(cw_spv_full_vert, sizeof cw_spv_full_vert);
    g.modules[5] = make_module(cw_spv_box_frag, sizeof cw_spv_box_frag);
    g.rp_clear = make_render_pass(VK_FORMAT_R8G8B8A8_UNORM, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_GENERAL);
    g.rp_load = make_render_pass(VK_FORMAT_R8G8B8A8_UNORM, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_IMAGE_LAYOUT_GENERAL);
    g.shapes_rgba = make_pipeline(g.rp_clear, g.modules[0], g.modules[1], 1);
    g.fills_rgba = make_pipeline(g.rp_clear, g.modules[0], g.modules[2], 1);
    g.composite_rgba = make_pipeline(g.rp_clear, g.modules[0], g.modules[3], 0);
    g.box = make_pipeline(g.rp_load, g.modules[4], g.modules[5], 0);
    /* The atlas is sampled at texel coordinates (unnormalized): linear under a transform, fetched otherwise. */
    VkSamplerCreateInfo linear = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                  .magFilter = VK_FILTER_LINEAR,
                                  .minFilter = VK_FILTER_LINEAR,
                                  .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                  .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                  .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                  .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                  .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
                                  .unnormalizedCoordinates = VK_TRUE};
    vkCreateSampler(g.device, &linear, NULL, &g.linear);
    VkSamplerCreateInfo nearest = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                   .magFilter = VK_FILTER_NEAREST,
                                   .minFilter = VK_FILTER_NEAREST,
                                   .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                   .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                   .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                   .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    vkCreateSampler(g.device, &nearest, NULL, &g.nearest);
    g.atlas_width = atlas_width;
    g.atlas_height = atlas_height;
    if (!make_image(atlas_width, atlas_height, VK_FORMAT_R8_UNORM,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, &g.atlas, &g.atlas_memory,
                    &g.atlas_view) ||
        !ensure_buffer(&g.staging, (VkDeviceSize)atlas_width * atlas_height, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
        return 4;
    }
    memset(g.staging.mapped, 0, (size_t)atlas_width * atlas_height);
    g.atlas_dirty = 1; /* the first frame clears it to zero and makes it readable */
    g.images_side = images;
    if (!make_image(images, images, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, &g.images, &g.images_memory,
                    &g.images_view)) {
        return 4;
    }
    g.ax0 = 0;
    g.ay0 = 0;
    g.ax1 = atlas_width;
    g.ay1 = atlas_height;
    if (g.shapes_rgba == VK_NULL_HANDLE || g.fills_rgba == VK_NULL_HANDLE || g.composite_rgba == VK_NULL_HANDLE ||
        g.box == VK_NULL_HANDLE) {
        return 3;
    }
    g.ready = 1;
    return 0;
}

/* The window to draw into: WindowHandle's kind and handles; Vulkan takes Win32's (2) and Xlib's (3). */
int32_t CW_VK(attach)(int32_t kind, uint64_t first, uint64_t second) {
    if (!g.ready || !g.has_swapchain) {
        return 1;
    }
    VkResult made = VK_ERROR_EXTENSION_NOT_PRESENT;
    if (kind == 3 && g.has_xlib) {
        cw_create_surface create;
        PFN_vkVoidFunction f = vkGetInstanceProcAddr(g.instance, "vkCreateXlibSurfaceKHR");
        memcpy(&create, &f, sizeof create);
        cw_xlib_surface_info info = {.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
                                     .display = (void *)(uintptr_t)first,
                                     .window = (unsigned long)second};
        made = create(g.instance, &info, NULL, &g.surface);
    } else if (kind == 2 && g.has_win32) {
        cw_create_surface create;
        PFN_vkVoidFunction f = vkGetInstanceProcAddr(g.instance, "vkCreateWin32SurfaceKHR");
        memcpy(&create, &f, sizeof create);
        cw_win32_surface_info info = {.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                                      .hinstance = (void *)(uintptr_t)second,
                                      .hwnd = (void *)(uintptr_t)first};
        made = create(g.instance, &info, NULL, &g.surface);
    }
    trace("surface created", made);
    if (made != VK_SUCCESS) {
        return 2;
    }
    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(g.gpu, g.family, g.surface, &supported);
    trace("surface supported", supported);
    if (!supported) {
        return 3;
    }
    /* BGRA8 or RGBA8, UNORM: the frames are sRGB values already, as Raster's are. */
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g.gpu, g.surface, &count, NULL);
    VkSurfaceFormatKHR formats[64];
    if (count > 64) {
        count = 64;
    }
    vkGetPhysicalDeviceSurfaceFormatsKHR(g.gpu, g.surface, &count, formats);
    g.present_format = VK_FORMAT_UNDEFINED;
    for (uint32_t k = 0; k < count; k++) {
        if (formats[k].format == VK_FORMAT_B8G8R8A8_UNORM || formats[k].format == VK_FORMAT_R8G8B8A8_UNORM) {
            g.present_format = formats[k].format;
            break;
        }
    }
    trace("present format", g.present_format);
    if (g.present_format == VK_FORMAT_UNDEFINED) {
        return 4;
    }
    g.rp_present = make_render_pass(g.present_format, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    g.shapes_present = make_pipeline(g.rp_present, g.modules[0], g.modules[1], 1);
    g.fills_present = make_pipeline(g.rp_present, g.modules[0], g.modules[2], 1);
    g.composite_present = make_pipeline(g.rp_present, g.modules[0], g.modules[3], 0);
    return g.shapes_present != VK_NULL_HANDLE && g.fills_present != VK_NULL_HANDLE &&
                   g.composite_present != VK_NULL_HANDLE
               ? 0
               : 5;
}

/* A swapchain of `width` × `height` (or the surface's own extent), its images' views and framebuffers. */
static int ensure_swapchain(uint32_t width, uint32_t height) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g.gpu, g.surface, &caps);
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = width;
        extent.height = height;
    }
    if (g.swapchain != VK_NULL_HANDLE && extent.width == g.swap_width && extent.height == g.swap_height) {
        return 1;
    }
    if (extent.width == 0 || extent.height == 0) {
        return 0;
    }
    vkDeviceWaitIdle(g.device);
    for (uint32_t k = 0; k < g.swap_count; k++) {
        vkDestroyFramebuffer(g.device, g.swap_framebuffers[k], NULL);
        vkDestroyImageView(g.device, g.swap_views[k], NULL);
    }
    uint32_t images = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && images > caps.maxImageCount) {
        images = caps.maxImageCount;
    }
    VkCompositeAlphaFlagBitsKHR alpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                                            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                            : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    VkSwapchainKHR old = g.swapchain;
    VkSwapchainCreateInfoKHR info = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                     .surface = g.surface,
                                     .minImageCount = images,
                                     .imageFormat = g.present_format,
                                     .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
                                     .imageExtent = extent,
                                     .imageArrayLayers = 1,
                                     .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                   VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                                     .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                     .preTransform = caps.currentTransform,
                                     .compositeAlpha = alpha,
                                     .presentMode = VK_PRESENT_MODE_FIFO_KHR,
                                     .clipped = VK_TRUE,
                                     .oldSwapchain = old};
    trace("swapchain usage supported", (long long)caps.supportedUsageFlags);
    trace("swapchain width", extent.width);
    trace("swapchain height", extent.height);
    VkResult created = vkCreateSwapchainKHR(g.device, &info, NULL, &g.swapchain);
    trace("swapchain created", created);
    if (created != VK_SUCCESS) {
        g.swapchain = VK_NULL_HANDLE;
        return 0;
    }
    if (old != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(g.device, old, NULL);
    }
    g.swap_count = CW_SWAP;
    vkGetSwapchainImagesKHR(g.device, g.swapchain, &g.swap_count, g.swap_images);
    for (uint32_t k = 0; k < g.swap_count; k++) {
        VkImageViewCreateInfo vinfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                       .image = g.swap_images[k],
                                       .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                       .format = g.present_format,
                                       .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        vkCreateImageView(g.device, &vinfo, NULL, &g.swap_views[k]);
        g.swap_framebuffers[k] = make_framebuffer(g.rp_present, g.swap_views[k], extent.width, extent.height);
    }
    g.swap_width = extent.width;
    g.swap_height = extent.height;
    return 1;
}

/* ---- frames ------------------------------------------------------------------------------------------------- */

/* Waits for the GPU to finish the last frame, and records its time. */
void CW_VK(finish)(void) {
    if (!g.pending) {
        return;
    }
    vkWaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(g.device, 1, &g.fence);
    g.pending = 0;
    if (g.queries != VK_NULL_HANDLE) {
        uint64_t ticks[2] = {0, 0};
        if (vkGetQueryPoolResults(g.device, g.queries, 0, 2, sizeof ticks, ticks, sizeof ticks[0],
                                  VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            g.gpu_ms = (double)(ticks[1] - ticks[0]) * g.timestamp_period / 1e6;
        }
    }
}

double CW_VK(gpu_ms)(void) { return g.gpu_ms; }

/* Copies the atlas rows [y0, y1) and columns [x0, x1) to the staging buffer; the next frame uploads them. */
void CW_VK(atlas)(const uint8_t *pixels, uint32_t stride, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    if (!g.ready || x1 <= x0 || y1 <= y0) {
        return;
    }
    CW_VK(finish)(); /* the last frame may still copy from the staging buffer */
    uint8_t *staging = (uint8_t *)g.staging.mapped;
    for (uint32_t y = y0; y < y1; y++) {
        memcpy(staging + (size_t)y * g.atlas_width + x0, pixels + (size_t)y * stride + x0, x1 - x0);
    }
    if (!g.atlas_dirty) {
        g.ax0 = x0;
        g.ay0 = y0;
        g.ax1 = x1;
        g.ay1 = y1;
    } else {
        g.ax0 = g.ax0 < x0 ? g.ax0 : x0;
        g.ay0 = g.ay0 < y0 ? g.ay0 : y0;
        g.ax1 = g.ax1 > x1 ? g.ax1 : x1;
        g.ay1 = g.ay1 > y1 ? g.ay1 : y1;
    }
    g.atlas_dirty = 1;
}

/* Copies a `w` × `h` image (rows `stride` pixels apart, RGBA8) to the image staging buffer at (u, v); the next frame
   uploads it. */
void CW_VK(image)(const uint32_t *pixels, uint32_t stride, uint32_t w, uint32_t h, uint32_t u, uint32_t v) {
    if (!g.ready || w == 0 || h == 0 || u + w > g.images_side || v + h > g.images_side) {
        return;
    }
    CW_VK(finish)(); /* the last frame may still copy from the staging buffer */
    if (g.image_staging.buffer == VK_NULL_HANDLE &&
        !ensure_buffer(&g.image_staging, (VkDeviceSize)g.images_side * g.images_side * 4,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
        return;
    }
    uint32_t *staging = (uint32_t *)g.image_staging.mapped;
    for (uint32_t y = 0; y < h; y++) {
        memcpy(staging + (size_t)(v + y) * g.images_side + u, pixels + (size_t)y * stride, (size_t)w * 4);
    }
    if (!g.images_dirty) {
        g.ix0 = u;
        g.iy0 = v;
        g.ix1 = u + w;
        g.iy1 = v + h;
    } else {
        g.ix0 = g.ix0 < u ? g.ix0 : u;
        g.iy0 = g.iy0 < v ? g.iy0 : v;
        g.ix1 = g.ix1 > u + w ? g.ix1 : u + w;
        g.iy1 = g.iy1 > v + h ? g.iy1 : v + h;
    }
    g.images_dirty = 1;
}

static void image_barrier(VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags src_access,
                          VkAccessFlags dst_access, VkPipelineStageFlags src, VkPipelineStageFlags dst) {
    VkImageMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                              .srcAccessMask = src_access,
                              .dstAccessMask = dst_access,
                              .oldLayout = from,
                              .newLayout = to,
                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .image = image,
                              .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(g.cmd, src, dst, 0, 0, NULL, 0, NULL, 1, &b);
}

static VkDescriptorSet allocate_set(int which) {
    VkDescriptorSetAllocateInfo info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                        .descriptorPool = g.descriptors,
                                        .descriptorSetCount = 1,
                                        .pSetLayouts = &g.set_layouts[which]};
    VkDescriptorSet set = VK_NULL_HANDLE;
    vkAllocateDescriptorSets(g.device, &info, &set);
    return set;
}

/* A set 1 or 2 holding `view` (in GENERAL): a sampled image, or the input attachment. */
static VkDescriptorSet image_set(int which, VkImageView view) {
    VkDescriptorSet set = allocate_set(which);
    VkDescriptorImageInfo image = {g.nearest, view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                  .dstSet = set,
                                  .dstBinding = 0,
                                  .descriptorCount = 1,
                                  .descriptorType = which == 1 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                               : VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
                                  .pImageInfo = &image};
    vkUpdateDescriptorSets(g.device, 1, &write, 0, NULL);
    return set;
}

static void begin_pass(VkRenderPass pass, VkFramebuffer fb, uint32_t width, uint32_t height, float r, float gr,
                       float b, float a) {
    VkClearValue clear;
    clear.color.float32[0] = r;
    clear.color.float32[1] = gr;
    clear.color.float32[2] = b;
    clear.color.float32[3] = a;
    VkRenderPassBeginInfo info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                  .renderPass = pass,
                                  .framebuffer = fb,
                                  .renderArea = {{0, 0}, {width, height}},
                                  .clearValueCount = 1,
                                  .pClearValues = &clear};
    vkCmdBeginRenderPass(g.cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {width, height}};
    vkCmdSetViewport(g.cmd, 0, 1, &viewport);
    vkCmdSetScissor(g.cmd, 0, 1, &scissor);
}

typedef struct {
    float target[4];
    int32_t box[4];
} cw_push;

/* How an instance is drawn: 0 by the shapes shader, 1 by the fill shader (a plain fill), 2 as a layer composite. */
static int pipeline_class(const float *instance) {
    if (instance[CW_KIND] == CW_KIND_LAYER) {
        return 2;
    }
    return instance[CW_KIND] == 0.0f && instance[51] == 0.0f ? 1 : 0; /* kind 0, xf1.w 0 */
}

/* Draws `count` instances of `data` (which the instance buffer holds from index `base`) into the current pass's
   target, whose box in the frame is `box`: shapes and fills in runs, each layer composite on its own. */
static void draw_target(VkPipeline shapes, VkPipeline fills, VkPipeline composite, const float *data, uint32_t base,
                        uint32_t count, const float box[4], VkImageView target_view, cw_texture **textures,
                        VkDescriptorSet set0) {
    cw_push push = {{box[0], box[1], box[2], box[3]}, {0, 0, 0, 0}};
    vkCmdPushConstants(g.cmd, g.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push,
                       &push);
    vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, 0, 1, &set0, 0, NULL);
    VkDescriptorSet input = VK_NULL_HANDLE;
    VkPipeline bound = VK_NULL_HANDLE;
    uint32_t k = 0;
    while (k < count) {
        const float *instance = data + (size_t)k * CW_FLOATS;
        int kind = pipeline_class(instance);
        if (kind == 2) {
            int layer = (int)instance[28]; /* params.x */
            if (layer >= 0 && layer < CW_MAX_LAYERS && textures[layer] != NULL) {
                if (input == VK_NULL_HANDLE) {
                    input = image_set(2, target_view);
                    vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, 2, 1, &input, 0, NULL);
                }
                VkDescriptorSet source = image_set(1, textures[layer]->view);
                vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, 1, 1, &source, 0, NULL);
                /* What was drawn so far becomes visible to the composite's input attachment reads. */
                VkMemoryBarrier visible = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                           .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                           .dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT};
                vkCmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 1, &visible, 0,
                                     NULL, 0, NULL);
                if (bound != composite) {
                    vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composite);
                    bound = composite;
                }
                vkCmdDraw(g.cmd, 4, 1, 0, base + k);
            }
            k++;
            continue;
        }
        uint32_t run = k + 1;
        while (run < count && pipeline_class(data + (size_t)run * CW_FLOATS) == kind) {
            run++;
        }
        VkPipeline p = kind == 1 ? fills : shapes;
        if (bound != p) {
            vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
            bound = p;
        }
        vkCmdDraw(g.cmd, 4, run - k, 0, base + k);
        k = run;
    }
}

/* One box pass of radius d along `axis` from `from` into `to`, both w × h. */
static void box_pass(cw_texture *from, cw_texture *to, int axis, int d, uint32_t w, uint32_t h) {
    begin_pass(g.rp_load, to->framebuffer, w, h, 0, 0, 0, 0);
    vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.box);
    VkDescriptorSet source = image_set(1, from->view);
    vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, 1, 1, &source, 0, NULL);
    cw_push push = {{0, 0, (float)w, (float)h}, {axis, d, (int32_t)w, (int32_t)h}};
    vkCmdPushConstants(g.cmd, g.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push,
                       &push);
    vkCmdDraw(g.cmd, 4, 1, 0, 0);
    vkCmdEndRenderPass(g.cmd);
}

/* Records a whole frame: the atlas upload, each layer in the order it closed and its blur, then the frame into
   `target` through `pass`. */
static int record(VkRenderPass pass, VkFramebuffer target, VkImageView target_view, int present, const float *frame,
                  uint32_t count, const float *layers, uint32_t layer_instances, const float *records,
                  uint32_t layer_count, const float *clips, uint32_t clip_count, uint32_t width, uint32_t height,
                  float r, float gr, float b) {
    for (int k = 0; k < CW_POOL; k++) {
        g.pool[k].used = 0;
    }
    vkResetDescriptorPool(g.device, g.descriptors, 0);
    /* The instances: the frame's from index 0, the layers' after them. */
    size_t total = (size_t)count + layer_instances;
    if (!ensure_buffer(&g.instances, (total ? total : 1) * CW_FLOATS * sizeof(float),
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        return 0;
    }
    memcpy(g.instances.mapped, frame, (size_t)count * CW_FLOATS * sizeof(float));
    memcpy((float *)g.instances.mapped + (size_t)count * CW_FLOATS, layers,
           (size_t)layer_instances * CW_FLOATS * sizeof(float));
    /* The clip shapes (one zeroed one when there are none: a storage buffer may not be empty). */
    if (!ensure_buffer(&g.clips, (clip_count ? clip_count : 1) * CW_SHAPE * sizeof(float),
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        return 0;
    }
    memset(g.clips.mapped, 0, CW_SHAPE * sizeof(float));
    memcpy(g.clips.mapped, clips, (size_t)clip_count * CW_SHAPE * sizeof(float));
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkResetCommandBuffer(g.cmd, 0);
    vkBeginCommandBuffer(g.cmd, &begin);
    if (g.queries != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(g.cmd, g.queries, 0, 2);
        vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g.queries, 0);
    }
    if (g.atlas_dirty) {
        image_barrier(g.atlas, g.atlas_ready ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy = {.bufferOffset = (VkDeviceSize)g.ay0 * g.atlas_width + g.ax0,
                                  .bufferRowLength = g.atlas_width,
                                  .bufferImageHeight = 0,
                                  .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                  .imageOffset = {(int32_t)g.ax0, (int32_t)g.ay0, 0},
                                  .imageExtent = {g.ax1 - g.ax0, g.ay1 - g.ay0, 1}};
        vkCmdCopyBufferToImage(g.cmd, g.staging.buffer, g.atlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        image_barrier(g.atlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        g.atlas_ready = 1;
        g.atlas_dirty = 0;
    }
    if (g.images_dirty) {
        image_barrier(g.images, g.images_ready ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy = {.bufferOffset = ((VkDeviceSize)g.iy0 * g.images_side + g.ix0) * 4,
                                  .bufferRowLength = g.images_side,
                                  .bufferImageHeight = 0,
                                  .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                  .imageOffset = {(int32_t)g.ix0, (int32_t)g.iy0, 0},
                                  .imageExtent = {g.ix1 - g.ix0, g.iy1 - g.iy0, 1}};
        vkCmdCopyBufferToImage(g.cmd, g.image_staging.buffer, g.images, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &copy);
        image_barrier(g.images, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        g.images_ready = 1;
        g.images_dirty = 0;
    } else if (!g.images_ready) { /* no image yet: readable, its contents undefined and never read */
        image_barrier(g.images, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
                      VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        g.images_ready = 1;
    }
    VkDescriptorSet set0 = allocate_set(0);
    VkDescriptorBufferInfo buffer = {g.instances.buffer, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo shapes = {g.clips.buffer, 0, VK_WHOLE_SIZE};
    VkDescriptorImageInfo atlas = {g.linear, g.atlas_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo images = {g.nearest, g.images_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet writes[4] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = set0,
         .dstBinding = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &buffer},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = set0,
         .dstBinding = 1,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .pImageInfo = &atlas},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = set0,
         .dstBinding = 2,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &shapes},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = set0,
         .dstBinding = 3,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .pImageInfo = &images},
    };
    vkUpdateDescriptorSets(g.device, 4, writes, 0, NULL);
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
        begin_pass(g.rp_clear, t->framebuffer, w, h, 0, 0, 0, 0);
        uint32_t first = (uint32_t)rec[0];
        draw_target(g.shapes_rgba, g.fills_rgba, g.composite_rgba, layers + (size_t)first * CW_FLOATS, count + first,
                    (uint32_t)rec[1], box, t->view, textures, set0);
        vkCmdEndRenderPass(g.cmd);
        int k0 = (int)rec[6], k1 = (int)rec[7];
        if (k0 > 0 || k1 > 0) {
            cw_texture *other = pooled(w, h);
            if (other != NULL) {
                cw_texture *from = t, *to = other;
                for (int axis = 0; axis < 2; axis++) {
                    for (int level = 0; level < 2; level++) {
                        int d = level == 0 ? k0 : k1;
                        if (d <= 0) {
                            continue;
                        }
                        box_pass(from, to, axis, d, w, h);
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
    begin_pass(pass, target, width, height, r, gr, b, 1.0f);
    draw_target(present ? g.shapes_present : g.shapes_rgba, present ? g.fills_present : g.fills_rgba,
                present ? g.composite_present : g.composite_rgba, frame, 0, count, whole, target_view, textures, set0);
    vkCmdEndRenderPass(g.cmd);
    return 1;
}

static void end_and_submit(int present) {
    if (g.queries != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.queries, 1);
    }
    vkEndCommandBuffer(g.cmd);
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                           .waitSemaphoreCount = present ? 1u : 0u,
                           .pWaitSemaphores = &g.acquired,
                           .pWaitDstStageMask = &wait,
                           .commandBufferCount = 1,
                           .pCommandBuffers = &g.cmd,
                           .signalSemaphoreCount = present ? 1u : 0u,
                           .pSignalSemaphores = &g.rendered};
    vkQueueSubmit(g.queue, 1, &submit, g.fence);
    g.pending = 1;
}

/* Draws the frame into the window's next swapchain image, `width` × `height` pixels, and presents it. */
int32_t CW_VK(present)(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                       const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count, uint32_t width, uint32_t height, float r, float g_,
                       float b) {
    if (g.surface == VK_NULL_HANDLE) {
        return 1;
    }
    CW_VK(finish)();
    if (!ensure_swapchain(width, height)) {
        return 2;
    }
    uint32_t index = 0;
    static int frames = 0;
    frames++;
    if (frames <= 3) {
        trace("acquiring, frame", frames);
    }
    VkResult got = vkAcquireNextImageKHR(g.device, g.swapchain, UINT64_MAX, g.acquired, VK_NULL_HANDLE, &index);
    if (frames <= 3) {
        trace("acquired", got);
    }
    if (got == VK_ERROR_OUT_OF_DATE_KHR) {
        g.swap_width = 0; /* recreated on the next frame */
        ensure_swapchain(width, height);
        return 2;
    }
    if (got != VK_SUCCESS && got != VK_SUBOPTIMAL_KHR) {
        return 2;
    }
    /* The surface decides the extent; the frame keeps its own size and the scissor clips it. */
    uint32_t w = g.swap_width < width ? g.swap_width : width;
    uint32_t h = g.swap_height < height ? g.swap_height : height;
    if (!record(g.rp_present, g.swap_framebuffers[index], g.swap_views[index], 1, frame, count, layers,
                layer_instances, records, layer_count, clips, clip_count, w, h, r, g_, b)) {
        return 3;
    }
    end_and_submit(1);
    VkPresentInfoKHR info = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                             .waitSemaphoreCount = 1,
                             .pWaitSemaphores = &g.rendered,
                             .swapchainCount = 1,
                             .pSwapchains = &g.swapchain,
                             .pImageIndices = &index};
    VkResult shown = vkQueuePresentKHR(g.queue, &info);
    if (frames <= 3) {
        trace("presented", shown);
    }
    if (shown == VK_ERROR_OUT_OF_DATE_KHR || shown == VK_SUBOPTIMAL_KHR) {
        g.swap_width = 0;
    }
    return 0;
}

/* Draws the frame into an offscreen image and copies its pixels to `out` (RGBA bytes, Raster's order). */
int32_t CW_VK(render_to)(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                         const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count, uint32_t width, uint32_t height, float r,
                         float g_, float b, uint32_t *out) {
    if (!g.ready) {
        return 1;
    }
    CW_VK(finish)();
    cw_texture *o = &g.offscreen;
    if (o->image == VK_NULL_HANDLE || o->width != width || o->height != height) {
        if (o->image != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(g.device, o->framebuffer, NULL);
            vkDestroyImageView(g.device, o->view, NULL);
            vkDestroyImage(g.device, o->image, NULL);
            vkFreeMemory(g.device, o->memory, NULL);
            memset(o, 0, sizeof *o);
        }
        if (!make_image(width, height, VK_FORMAT_R8G8B8A8_UNORM, cw_target_usage, &o->image, &o->memory, &o->view)) {
            return 2;
        }
        o->framebuffer = make_framebuffer(g.rp_clear, o->view, width, height);
        o->width = width;
        o->height = height;
    }
    if (out != NULL && !ensure_buffer(&g.readback, (VkDeviceSize)width * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        return 3;
    }
    if (!record(g.rp_clear, o->framebuffer, o->view, 0, frame, count, layers, layer_instances, records, layer_count,
                clips, clip_count, width, height, r, g_, b)) {
        return 4;
    }
    if (out != NULL) {
        VkBufferImageCopy copy = {.bufferOffset = 0,
                                  .bufferRowLength = width,
                                  .bufferImageHeight = height,
                                  .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                  .imageOffset = {0, 0, 0},
                                  .imageExtent = {width, height, 1}};
        vkCmdCopyImageToBuffer(g.cmd, o->image, VK_IMAGE_LAYOUT_GENERAL, g.readback.buffer, 1, &copy);
    }
    end_and_submit(0);
    CW_VK(finish)();
    if (out != NULL) {
        memcpy(out, g.readback.mapped, (size_t)width * height * 4);
    }
    return 0;
}

/* Draws the frame offscreen and waits for it, reading nothing back: for timing the GPU. */
int32_t CW_VK(render_only)(const float *frame, uint32_t count, const float *layers, uint32_t layer_instances,
                           const float *records, uint32_t layer_count, const float *clips, uint32_t clip_count, uint32_t width, uint32_t height, float r,
                           float g_, float b) {
    return CW_VK(render_to)(frame, count, layers, layer_instances, records, layer_count, clips, clip_count, width, height, r, g_, b,
                            NULL);
}
#else
typedef int cw_vulkan_absent; /* macOS: metal.c draws */
#endif
