// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025-2026 Kai Dieki
/**
 * @file fid_vulkan.h
 * @brief Vulkan Compute FIR engine — GPU-accelerated FIR batch convolution (FP32).
 *
 * Included by fidrf_cmdlist.h when FIDLIB_VULKAN is defined.
 * Provides RunVK / RunVKBuf and filter_step_vk() for large FIR filters.
 *
 * Design constraints:
 * - FP32 only: VideoCore VII (RPi 5) does not support FP64 in Vulkan compute.
 * - Batch processing: collects FIDLIB_VULKAN_BATCH samples, then dispatches
 *   one GPU work group per batch. Inherent latency = FIDLIB_VULKAN_BATCH - 1.
 * - Public API stays double(void*,double): FP32 conversion at boundary only.
 * - Lazy init: Vulkan device created on first call to fid_run_new() with Vulkan
 *   backend selected. Falls back to NEON/scalar filter_step if GPU unavailable.
 * - Unified memory (RPi 5 / integrated GPU): host-visible+device-local buffers
 *   avoid explicit transfer commands.
 *
 * @ingroup fidlib_run
 */

#ifndef FID_VULKAN_H
#define FID_VULKAN_H

#include <vulkan/vulkan.h>
#include <string.h>
#include <stdlib.h>
#include "fir_dot_spv.h"   /* embedded SPIR-V: fir_dot_spv[], fir_dot_spv_size */

/* ── Magic sentinel ─────────────────────────────────────────────────────── */

#define RUN_MAGIC_VK    0x564B4600u  /* RunVK shared state  */
#define RUNBUF_MAGIC_VK 0x564B4601u  /* RunVKBuf per-channel state */

/* ── Global Vulkan context (lazy-initialized, shared across all filters) ── */

typedef struct FidVkCtx {
    VkInstance       instance;
    VkPhysicalDevice phys_dev;
    VkDevice         device;
    VkQueue          queue;
    uint32_t         queue_family;
    VkCommandPool    cmd_pool;
    int              unified_memory;  /* 1 if GPU and CPU share physical memory */
    int              ok;              /* 1 after successful init */
} FidVkCtx;

static FidVkCtx g_vk = {0};

/* ── Buffer allocation helper ────────────────────────────────────────────── */

typedef struct FidVkBuf {
    VkBuffer       buf;
    VkDeviceMemory mem;
    VkDeviceSize   size;
    void          *mapped;  /* non-NULL when host-coherent/visible */
} FidVkBuf;

/* ── Shared read-only Vulkan filter state ────────────────────────────────── */

typedef struct RunVK {
    unsigned int       magic;          /* RUN_MAGIC_VK */
    int                M;              /* FIR tap count */
    int                B;              /* batch size */
    VkDescriptorSetLayout  dset_layout;
    VkPipelineLayout       pipe_layout;
    VkPipeline             pipeline;
    VkDescriptorPool       dpool;
    FidVkBuf               coef_buf;   /* coefficient buffer (FP32, M floats) */
} RunVK;

/* ── Per-channel Vulkan state ────────────────────────────────────────────── */

typedef struct RunVKBuf {
    unsigned int    type_tag;    /* RUNBUF_MAGIC_VK */
    RunVK          *vk;
    int             in_pos;      /* new samples buffered (0..B-1) */
    int             out_pos;
    int             out_avail;
    float          *x_host;      /* input staging [M-1 + B] floats */
    float          *y_host;      /* output staging [B] floats */
    FidVkBuf        x_buf;       /* device input buffer */
    FidVkBuf        y_buf;       /* device output buffer */
    VkDescriptorSet dset;
    VkCommandBuffer cmd;
} RunVKBuf;

/* ────────────────────────────────────────────────────────────────────────── */
/*  Internal helpers                                                          */
/* ────────────────────────────────────────────────────────────────────────── */

static uint32_t
vk_find_memory(VkPhysicalDevice pd, uint32_t type_bits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    }
    return UINT32_MAX;
}

static int
vk_alloc_buf(VkDevice dev, VkPhysicalDevice pd,
             VkDeviceSize size, VkBufferUsageFlags usage,
             VkMemoryPropertyFlags mem_flags, FidVkBuf *out)
{
    VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size  = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &bci, NULL, &out->buf) != VK_SUCCESS) return 0;

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, out->buf, &mr);

    uint32_t mi = vk_find_memory(pd, mr.memoryTypeBits, mem_flags);
    if (mi == UINT32_MAX) { vkDestroyBuffer(dev, out->buf, NULL); return 0; }

    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = mi;
    if (vkAllocateMemory(dev, &mai, NULL, &out->mem) != VK_SUCCESS) {
        vkDestroyBuffer(dev, out->buf, NULL); return 0;
    }
    vkBindBufferMemory(dev, out->buf, out->mem, 0);
    out->size   = size;
    out->mapped = NULL;
    if (mem_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        vkMapMemory(dev, out->mem, 0, size, 0, &out->mapped);
    }
    return 1;
}

static void
vk_free_buf(VkDevice dev, FidVkBuf *b)
{
    if (b->mapped) vkUnmapMemory(dev, b->mem);
    if (b->buf)    vkDestroyBuffer(dev, b->buf, NULL);
    if (b->mem)    vkFreeMemory(dev, b->mem, NULL);
    b->buf = VK_NULL_HANDLE; b->mem = VK_NULL_HANDLE; b->mapped = NULL;
}

/* ── Lazy Vulkan context initialization ─────────────────────────────────── */

static int
vk_init(void)
{
    if (g_vk.ok) return 1;

    /* Instance */
    VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &ai;
    if (vkCreateInstance(&ici, NULL, &g_vk.instance) != VK_SUCCESS) return 0;

    /* Physical device: prefer integrated GPU (unified memory) */
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(g_vk.instance, &n, NULL);
    if (n == 0) goto fail_instance;
    VkPhysicalDevice *pdevs = (VkPhysicalDevice *)alloca((size_t)n * sizeof(*pdevs));
    vkEnumeratePhysicalDevices(g_vk.instance, &n, pdevs);

    g_vk.phys_dev = VK_NULL_HANDLE;
    int best_score = -1;
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(pdevs[i], &p);
        /* Check compute queue family */
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pdevs[i], &qn, NULL);
        VkQueueFamilyProperties *qf = (VkQueueFamilyProperties *)alloca(
                                        (size_t)qn * sizeof(*qf));
        vkGetPhysicalDeviceQueueFamilyProperties(pdevs[i], &qn, qf);
        int has_compute = 0;
        for (uint32_t j = 0; j < qn; j++)
            if (qf[j].queueFlags & VK_QUEUE_COMPUTE_BIT) { has_compute = 1; break; }
        if (!has_compute) continue;

        int score = 0;
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 10;
        else if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 8;
        else if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) score += 1;
        if (score > best_score) { best_score = score; g_vk.phys_dev = pdevs[i]; }
    }
    if (g_vk.phys_dev == VK_NULL_HANDLE) goto fail_instance;

    /* Detect unified memory */
    {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(g_vk.phys_dev, &mp);
        g_vk.unified_memory = 0;
        for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
            if ((mp.memoryTypes[i].propertyFlags &
                 (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) ==
                (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                g_vk.unified_memory = 1; break;
            }
        }
    }

    /* Compute queue family */
    {
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(g_vk.phys_dev, &qn, NULL);
        VkQueueFamilyProperties *qf = (VkQueueFamilyProperties *)alloca(
                                        (size_t)qn * sizeof(*qf));
        vkGetPhysicalDeviceQueueFamilyProperties(g_vk.phys_dev, &qn, qf);
        g_vk.queue_family = UINT32_MAX;
        for (uint32_t j = 0; j < qn; j++)
            if (qf[j].queueFlags & VK_QUEUE_COMPUTE_BIT)
                { g_vk.queue_family = j; break; }
        if (g_vk.queue_family == UINT32_MAX) goto fail_instance;
    }

    /* Logical device */
    float prio = 1.0f;
    VkDeviceQueueCreateInfo dqci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    dqci.queueFamilyIndex = g_vk.queue_family;
    dqci.queueCount       = 1;
    dqci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos    = &dqci;
    if (vkCreateDevice(g_vk.phys_dev, &dci, NULL, &g_vk.device) != VK_SUCCESS)
        goto fail_instance;
    vkGetDeviceQueue(g_vk.device, g_vk.queue_family, 0, &g_vk.queue);

    /* Command pool */
    VkCommandPoolCreateInfo cpci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.queueFamilyIndex = g_vk.queue_family;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(g_vk.device, &cpci, NULL, &g_vk.cmd_pool) != VK_SUCCESS)
        goto fail_device;

    g_vk.ok = 1;
    return 1;

fail_device:
    vkDestroyDevice(g_vk.device, NULL); g_vk.device = VK_NULL_HANDLE;
fail_instance:
    vkDestroyInstance(g_vk.instance, NULL); g_vk.instance = VK_NULL_HANDLE;
    return 0;
}

/* ── vk_run_new ─────────────────────────────────────────────────────────── */

static void *
vk_run_new(const FidFilter *filt, double (**funcpp)(void *, double))
{
    if (!vk_init()) return NULL;

    /* collect FIR taps */
    int M = 0;
    const FidFilter *ff;
    for (ff = filt; ff->len; ff = FFCNEXT(ff))
        if (ff->typ == 'F' && ff->len > 1) M += ff->len;
    if (M < 2) return NULL;

    double *h_d = (double *)Alloc((size_t)M * sizeof(double));
    double  gain = 1.0;
    int     hi   = 0;
    for (ff = filt; ff->len; ff = FFCNEXT(ff)) {
        if (ff->typ == 'F' && ff->len == 1)
            gain *= ff->val[0];
        else if (ff->typ == 'F') {
            memcpy(h_d + hi, ff->val, (size_t)ff->len * sizeof(double));
            hi += ff->len;
        }
    }
    if (gain != 1.0)
        for (int i = 0; i < M; i++) h_d[i] *= gain;

    const int B = FIDLIB_VULKAN_BATCH;

    /* Descriptor set layout: 3 storage buffers (coef, input, output) */
    VkDescriptorSetLayoutBinding bindings[3] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
    };
    VkDescriptorSetLayoutCreateInfo dslci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 3;
    dslci.pBindings    = bindings;
    VkDescriptorSetLayout dset_layout;
    if (vkCreateDescriptorSetLayout(g_vk.device, &dslci, NULL, &dset_layout) != VK_SUCCESS)
        { free(h_d); return NULL; }

    /* Push constant range: two ints (M, B) */
    VkPushConstantRange pcr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 2 * sizeof(int)};
    VkPipelineLayoutCreateInfo plci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &dset_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    VkPipelineLayout pipe_layout;
    if (vkCreatePipelineLayout(g_vk.device, &plci, NULL, &pipe_layout) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(g_vk.device, dset_layout, NULL);
        free(h_d); return NULL;
    }

    /* Shader module from embedded SPIR-V */
    VkShaderModuleCreateInfo smci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = fir_dot_spv_size;
    smci.pCode    = fir_dot_spv;
    VkShaderModule shader;
    if (vkCreateShaderModule(g_vk.device, &smci, NULL, &shader) != VK_SUCCESS) {
        vkDestroyPipelineLayout(g_vk.device, pipe_layout, NULL);
        vkDestroyDescriptorSetLayout(g_vk.device, dset_layout, NULL);
        free(h_d); return NULL;
    }

    /* Compute pipeline */
    VkComputePipelineCreateInfo cpci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = shader;
    cpci.stage.pName  = "main";
    cpci.layout       = pipe_layout;
    VkPipeline pipeline;
    int ok = vkCreateComputePipelines(g_vk.device, VK_NULL_HANDLE, 1, &cpci, NULL, &pipeline)
             == VK_SUCCESS;
    vkDestroyShaderModule(g_vk.device, shader, NULL);
    if (!ok) {
        vkDestroyPipelineLayout(g_vk.device, pipe_layout, NULL);
        vkDestroyDescriptorSetLayout(g_vk.device, dset_layout, NULL);
        free(h_d); return NULL;
    }

    /* Coefficient buffer (device-local or host-visible on unified) */
    VkMemoryPropertyFlags coef_flags =
        g_vk.unified_memory
        ? (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
           | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    FidVkBuf coef_buf = {0};
    /* For discrete GPU, we'd need a staging buffer. Here we use host-visible. */
    if (!vk_alloc_buf(g_vk.device, g_vk.phys_dev,
                      (VkDeviceSize)M * sizeof(float),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &coef_buf)) {
        vkDestroyPipeline(g_vk.device, pipeline, NULL);
        vkDestroyPipelineLayout(g_vk.device, pipe_layout, NULL);
        vkDestroyDescriptorSetLayout(g_vk.device, dset_layout, NULL);
        free(h_d); return NULL;
    }

    /* Upload FP32 coefficients */
    float *coef_fp32 = (float *)coef_buf.mapped;
    for (int i = 0; i < M; i++) coef_fp32[i] = (float)h_d[i];
    free(h_d);

    /* Descriptor pool */
    VkDescriptorPoolSize dps = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
    VkDescriptorPoolCreateInfo dpci = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets       = 64; /* support many concurrent RunVKBuf instances */
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &dps;
    VkDescriptorPool dpool;
    if (vkCreateDescriptorPool(g_vk.device, &dpci, NULL, &dpool) != VK_SUCCESS) {
        vk_free_buf(g_vk.device, &coef_buf);
        vkDestroyPipeline(g_vk.device, pipeline, NULL);
        vkDestroyPipelineLayout(g_vk.device, pipe_layout, NULL);
        vkDestroyDescriptorSetLayout(g_vk.device, dset_layout, NULL);
        return NULL;
    }

    RunVK *rv = (RunVK *)Alloc(sizeof(RunVK));
    rv->magic       = RUN_MAGIC_VK;
    rv->M           = M;
    rv->B           = B;
    rv->dset_layout = dset_layout;
    rv->pipe_layout = pipe_layout;
    rv->pipeline    = pipeline;
    rv->dpool       = dpool;
    rv->coef_buf    = coef_buf;

    *funcpp = filter_step_vk;
    return rv;
}

/* ── vk_run_free ────────────────────────────────────────────────────────── */

static void
vk_run_free(void *run)
{
    RunVK *rv = (RunVK *)run;
    if (!rv || !g_vk.ok) { free(run); return; }
    vkDestroyDescriptorPool(g_vk.device, rv->dpool, NULL);
    vk_free_buf(g_vk.device, &rv->coef_buf);
    vkDestroyPipeline(g_vk.device, rv->pipeline, NULL);
    vkDestroyPipelineLayout(g_vk.device, rv->pipe_layout, NULL);
    vkDestroyDescriptorSetLayout(g_vk.device, rv->dset_layout, NULL);
    free(rv);
}

/* ── vk_run_bufsize ─────────────────────────────────────────────────────── */

static int
vk_run_bufsize(void *run)
{
    RunVK *rv = (RunVK *)run;
    /* CPU-side staging arrays: x_host [M-1+B], y_host [B] */
    return (int)(sizeof(RunVKBuf)
               + (size_t)(rv->M - 1 + rv->B) * sizeof(float)
               + (size_t)(rv->B)              * sizeof(float));
}

/* ── vk_run_initbuf ─────────────────────────────────────────────────────── */

static void
vk_run_initbuf(void *run, void *buf)
{
    RunVK    *rv   = (RunVK *)run;
    RunVKBuf *rb   = (RunVKBuf *)buf;
    char     *base = (char *)(rb + 1);

    rb->type_tag  = RUNBUF_MAGIC_VK;
    rb->vk        = rv;
    rb->in_pos    = 0;
    rb->out_pos   = 0;
    rb->out_avail = 0;
    rb->x_host    = (float *)base;
    rb->y_host    = rb->x_host + (rv->M - 1 + rv->B);
    memset(base, 0, (size_t)(rv->M - 1 + rv->B + rv->B) * sizeof(float));

    /* Device buffers for this channel */
    VkDeviceSize x_bytes = (VkDeviceSize)(rv->M - 1 + rv->B) * sizeof(float);
    VkDeviceSize y_bytes = (VkDeviceSize)rv->B * sizeof(float);
    VkMemoryPropertyFlags hv_flags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    vk_alloc_buf(g_vk.device, g_vk.phys_dev, x_bytes,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, hv_flags, &rb->x_buf);
    vk_alloc_buf(g_vk.device, g_vk.phys_dev, y_bytes,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, hv_flags, &rb->y_buf);

    /* Descriptor set */
    VkDescriptorSetAllocateInfo dsai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool     = rv->dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts        = &rv->dset_layout;
    vkAllocateDescriptorSets(g_vk.device, &dsai, &rb->dset);

    /* Update descriptor set */
    VkDescriptorBufferInfo dbi[3] = {
        {rv->coef_buf.buf, 0, rv->coef_buf.size},
        {rb->x_buf.buf,    0, rb->x_buf.size},
        {rb->y_buf.buf,    0, rb->y_buf.size},
    };
    VkWriteDescriptorSet wds[3];
    for (int i = 0; i < 3; i++) {
        wds[i].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wds[i].pNext            = NULL;
        wds[i].dstSet           = rb->dset;
        wds[i].dstBinding       = (uint32_t)i;
        wds[i].dstArrayElement  = 0;
        wds[i].descriptorCount  = 1;
        wds[i].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wds[i].pImageInfo       = NULL;
        wds[i].pBufferInfo      = &dbi[i];
        wds[i].pTexelBufferView = NULL;
    }
    vkUpdateDescriptorSets(g_vk.device, 3, wds, 0, NULL);

    /* Command buffer */
    VkCommandBufferAllocateInfo cbai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool        = g_vk.cmd_pool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    vkAllocateCommandBuffers(g_vk.device, &cbai, &rb->cmd);
}

/* ── vk_run_newbuf ──────────────────────────────────────────────────────── */

static void *
vk_run_newbuf(void *run)
{
    RunVKBuf *rb = (RunVKBuf *)Alloc((size_t)vk_run_bufsize(run));
    vk_run_initbuf(run, rb);
    return rb;
}

/* ── vk_run_freebuf ─────────────────────────────────────────────────────── */

static void
vk_run_freebuf(void *buf)
{
    RunVKBuf *rb = (RunVKBuf *)buf;
    if (!rb || !g_vk.ok) { free(buf); return; }
    vkFreeCommandBuffers(g_vk.device, g_vk.cmd_pool, 1, &rb->cmd);
    vk_free_buf(g_vk.device, &rb->x_buf);
    vk_free_buf(g_vk.device, &rb->y_buf);
    /* descriptor set returned to pool when pool is destroyed (RunVK freed) */
    free(rb);
}

/* ── vk_run_zapbuf ──────────────────────────────────────────────────────── */

static void
vk_run_zapbuf(void *buf)
{
    RunVKBuf *rb = (RunVKBuf *)buf;
    RunVK    *rv = rb->vk;
    rb->in_pos    = 0;
    rb->out_pos   = 0;
    rb->out_avail = 0;
    memset(rb->x_host, 0, (size_t)(rv->M - 1 + rv->B + rv->B) * sizeof(float));
    if (rb->x_buf.mapped)
        memset(rb->x_buf.mapped, 0, (size_t)(rv->M - 1 + rv->B) * sizeof(float));
}

/* ── filter_step_vk ─────────────────────────────────────────────────────── */

static double
filter_step_vk(void *rbuf, double in)
{
    RunVKBuf *rb = (RunVKBuf *)rbuf;
    RunVK    *rv = rb->vk;

    /* Store input in overlap+new-sample buffer */
    rb->x_host[rv->M - 1 + rb->in_pos] = (float)in;
    rb->in_pos++;

    if (rb->in_pos == rv->B) {
        /* Upload input to GPU */
        if (rb->x_buf.mapped)
            memcpy(rb->x_buf.mapped, rb->x_host,
                   (size_t)(rv->M - 1 + rv->B) * sizeof(float));

        /* Record and submit compute command */
        VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(rb->cmd, &bi);
        vkCmdBindPipeline(rb->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, rv->pipeline);
        vkCmdBindDescriptorSets(rb->cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                rv->pipe_layout, 0, 1, &rb->dset, 0, NULL);
        int push[2] = {rv->M, rv->B};
        vkCmdPushConstants(rb->cmd, rv->pipe_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), push);
        uint32_t groups = ((uint32_t)rv->B + 63u) / 64u;
        vkCmdDispatch(rb->cmd, groups, 1, 1);
        vkEndCommandBuffer(rb->cmd);

        VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &rb->cmd;
        vkQueueSubmit(g_vk.queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(g_vk.queue);

        /* Read back output */
        if (rb->y_buf.mapped)
            memcpy(rb->y_host, rb->y_buf.mapped, (size_t)rv->B * sizeof(float));

        /* Slide overlap: keep last M-1 input samples */
        memmove(rb->x_host, rb->x_host + rv->B, (size_t)(rv->M - 1) * sizeof(float));
        rb->in_pos    = 0;
        rb->out_pos   = 0;
        rb->out_avail = rv->B;
    }

    if (rb->out_avail > 0) {
        rb->out_avail--;
        return (double)rb->y_host[rb->out_pos++];
    }
    return 0.0;
}

#endif /* FID_VULKAN_H */
