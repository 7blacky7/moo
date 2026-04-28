/**
 * moo_3d_vulkan_mem.c — Vulkan Vertex Buffers, Command Recording, Chunks.
 */

#include "moo_3d_vulkan_mem.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ========================================================
 * Vertex Attribute Descriptions
 * ======================================================== */

void vk_moo_vertex_attr_descs(VkVertexInputAttributeDescription out[3]) {
    /* location 0: position */
    out[0].binding  = 0;
    out[0].location = 0;
    out[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    out[0].offset   = offsetof(VkMooVertex, px);
    /* location 1: color */
    out[1].binding  = 0;
    out[1].location = 1;
    out[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    out[1].offset   = offsetof(VkMooVertex, r);
    /* location 2: normal */
    out[2].binding  = 0;
    out[2].location = 2;
    out[2].format   = VK_FORMAT_R32G32B32_SFLOAT;
    out[2].offset   = offsetof(VkMooVertex, nx);
}

/* ========================================================
 * Memory Type Helper
 * ======================================================== */

uint32_t vk_find_memory_type(VkPhysicalDevice phys,
                             uint32_t type_filter,
                             VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    fprintf(stderr, "moo Vulkan: passender Memory-Typ nicht gefunden\n");
    return 0;
}

/* ========================================================
 * GPU Buffer
 * ======================================================== */

int vk_moo_buffer_create(VkDevice device, VkPhysicalDevice phys,
                         VkDeviceSize size,
                         VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags props,
                         VkMooBuffer* out) {
    VkBufferCreateInfo buf_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(device, &buf_info, NULL, &out->buffer) != VK_SUCCESS) {
        fprintf(stderr, "moo Vulkan: Buffer-Erstellung fehlgeschlagen\n");
        return -1;
    }

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(device, out->buffer, &mem_req);

    VkMemoryAllocateInfo alloc_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mem_req.size,
        .memoryTypeIndex = vk_find_memory_type(phys, mem_req.memoryTypeBits, props),
    };

    if (vkAllocateMemory(device, &alloc_info, NULL, &out->memory) != VK_SUCCESS) {
        fprintf(stderr, "moo Vulkan: Memory-Allokierung fehlgeschlagen\n");
        vkDestroyBuffer(device, out->buffer, NULL);
        return -1;
    }

    vkBindBufferMemory(device, out->buffer, out->memory, 0);
    out->size = size;
    return 0;
}

void vk_moo_buffer_destroy(VkDevice device, VkMooBuffer* buf) {
    if (buf->buffer) vkDestroyBuffer(device, buf->buffer, NULL);
    if (buf->memory) vkFreeMemory(device, buf->memory, NULL);
    memset(buf, 0, sizeof(VkMooBuffer));
}

int vk_moo_buffer_upload(VkDevice device, VkMooBuffer* buf,
                         const void* data, VkDeviceSize size) {
    void* mapped;
    if (vkMapMemory(device, buf->memory, 0, size, 0, &mapped) != VK_SUCCESS)
        return -1;
    memcpy(mapped, data, (size_t)size);
    vkUnmapMemory(device, buf->memory);
    return 0;
}

int vk_moo_buffer_staged_upload(VkDevice device, VkPhysicalDevice phys,
                                VkCommandPool pool, VkQueue queue,
                                const void* data, VkDeviceSize size,
                                VkMooBuffer* dst) {
    /* Create staging buffer (host-visible) */
    VkMooBuffer staging = {0};
    if (vk_moo_buffer_create(device, phys, size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &staging) != 0)
        return -1;

    vk_moo_buffer_upload(device, &staging, data, size);

    /* Create device-local dst buffer if not already allocated */
    if (!dst->buffer) {
        if (vk_moo_buffer_create(device, phys, size,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                dst) != 0) {
            vk_moo_buffer_destroy(device, &staging);
            return -1;
        }
    }

    /* Copy staging → dst via one-shot command buffer */
    VkCommandBufferAllocateInfo cmd_alloc = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmd_alloc, &cmd);

    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin);

    VkBufferCopy region = { .size = size };
    vkCmdCopyBuffer(cmd, staging.buffer, dst->buffer, 1, &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cmd,
    };
    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, pool, 1, &cmd);
    vk_moo_buffer_destroy(device, &staging);
    return 0;
}

/* ========================================================
 * Mesh Collector
 * ======================================================== */

void vk_mesh_collector_init(VkMeshCollector* mc) {
    mc->vertices = (VkMooVertex*)malloc(VK_MESH_INITIAL_CAP * sizeof(VkMooVertex));
    mc->count = 0;
    mc->capacity = VK_MESH_INITIAL_CAP;
}

void vk_mesh_collector_reset(VkMeshCollector* mc) {
    mc->count = 0;
}

void vk_mesh_collector_free(VkMeshCollector* mc) {
    free(mc->vertices);
    mc->vertices = NULL;
    mc->count = 0;
    mc->capacity = 0;
}

static void vk_mesh_grow(VkMeshCollector* mc) {
    mc->capacity *= 2;
    mc->vertices = (VkMooVertex*)realloc(mc->vertices,
                                         mc->capacity * sizeof(VkMooVertex));
}

void vk_mesh_collector_add_vertex(VkMeshCollector* mc,
                                  float px, float py, float pz,
                                  float r, float g, float b,
                                  float nx, float ny, float nz) {
    if (mc->count >= mc->capacity) vk_mesh_grow(mc);
    VkMooVertex* v = &mc->vertices[mc->count++];
    v->px = px; v->py = py; v->pz = pz;
    v->r = r;   v->g = g;   v->b = b;
    v->nx = nx; v->ny = ny; v->nz = nz;
}

static void vk_mesh_add_quad(VkMeshCollector* mc,
                             const float* v0, const float* v1,
                             const float* v2, const float* v3,
                             float r, float g, float b,
                             float nx, float ny, float nz) {
    vk_mesh_collector_add_vertex(mc, v0[0],v0[1],v0[2], r,g,b, nx,ny,nz);
    vk_mesh_collector_add_vertex(mc, v1[0],v1[1],v1[2], r,g,b, nx,ny,nz);
    vk_mesh_collector_add_vertex(mc, v2[0],v2[1],v2[2], r,g,b, nx,ny,nz);
    vk_mesh_collector_add_vertex(mc, v0[0],v0[1],v0[2], r,g,b, nx,ny,nz);
    vk_mesh_collector_add_vertex(mc, v2[0],v2[1],v2[2], r,g,b, nx,ny,nz);
    vk_mesh_collector_add_vertex(mc, v3[0],v3[1],v3[2], r,g,b, nx,ny,nz);
}

void vk_mesh_collector_add_cube(VkMeshCollector* mc,
                                float x, float y, float z, float size,
                                float r, float g, float b) {
    float s = size / 2.0f;
    float c[8][3] = {
        {x-s,y-s,z+s},{x+s,y-s,z+s},{x+s,y+s,z+s},{x-s,y+s,z+s},
        {x+s,y-s,z-s},{x-s,y-s,z-s},{x-s,y+s,z-s},{x+s,y+s,z-s},
    };
    struct { float nx,ny,nz; int i0,i1,i2,i3; } faces[6] = {
        { 0, 0, 1, 0,1,2,3}, { 0, 0,-1, 4,5,6,7},
        { 0, 1, 0, 3,2,7,6}, { 0,-1, 0, 5,4,1,0},
        { 1, 0, 0, 1,4,7,2}, {-1, 0, 0, 5,0,3,6},
    };
    for (int f = 0; f < 6; f++) {
        vk_mesh_add_quad(mc,
            c[faces[f].i0], c[faces[f].i1],
            c[faces[f].i2], c[faces[f].i3],
            r, g, b,
            faces[f].nx, faces[f].ny, faces[f].nz);
    }
}

/* ========================================================
 * Chunk System — Secondary Command Buffers
 * ======================================================== */

void vk_chunk_system_init(VkChunkSystem* cs, VkDevice device,
                          VkPhysicalDevice phys, VkQueue queue,
                          VkCommandPool pool, VkRenderPass rp,
                          VkPipeline pipeline, VkPipelineLayout layout) {
    memset(cs->slots, 0, sizeof(cs->slots));
    cs->device          = device;
    cs->phys_device     = phys;
    cs->gfx_queue       = queue;
    cs->cmd_pool        = pool;
    cs->render_pass     = rp;
    cs->pipeline        = pipeline;
    cs->pipeline_layout = layout;
}

int vk_chunk_alloc(VkChunkSystem* cs) {
    for (int i = 0; i < MAX_VK_CHUNKS; i++) {
        if (!cs->slots[i].is_used) {
            cs->slots[i].is_used = true;
            cs->slots[i].is_compiled = false;
            cs->slots[i].vertex_count = 0;
            memset(&cs->slots[i].vertex_buf, 0, sizeof(VkMooBuffer));

            /* Allocate secondary command buffer */
            VkCommandBufferAllocateInfo alloc = {
                .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool        = cs->cmd_pool,
                .level              = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
                .commandBufferCount = 1,
            };
            vkAllocateCommandBuffers(cs->device, &alloc, &cs->slots[i].cmd_buf);
            return i;
        }
    }
    return -1;
}

int vk_chunk_upload(VkChunkSystem* cs, int id, VkMeshCollector* mc,
                    VkFramebuffer fb, VkExtent2D extent,
                    const float* push_constants, uint32_t push_size) {
    if (id < 0 || id >= MAX_VK_CHUNKS || !cs->slots[id].is_used) return -1;
    if (mc->count == 0) return -1;

    VkChunkSlot* slot = &cs->slots[id];

    /* Upload vertex data via staging */
    VkDeviceSize buf_size = (VkDeviceSize)mc->count * sizeof(VkMooVertex);
    if (slot->vertex_buf.buffer)
        vk_moo_buffer_destroy(cs->device, &slot->vertex_buf);

    if (vk_moo_buffer_staged_upload(cs->device, cs->phys_device,
            cs->cmd_pool, cs->gfx_queue,
            mc->vertices, buf_size, &slot->vertex_buf) != 0)
        return -1;

    slot->vertex_count = mc->count;

    /* Note: Secondary command buffer recording removed (TASK I, sprachen-analyst).
     * The recorded buffer was never executed — chunk draws happen inline in vk_swap()
     * via vkCmdBindVertexBuffers + vkCmdDraw on the primary command buffer.
     * Recording here only produced Vulkan validation errors (VUID-01795 push-constant
     * stage mismatch, VUID-08608 dynamic state vs static pipeline, VUID-08600
     * descriptor set 0 not bound). The unused parameters are kept for ABI stability. */
    (void)fb; (void)extent; (void)push_constants; (void)push_size;

    slot->is_compiled = true;
    return 0;
}

void vk_chunk_draw(VkChunkSystem* cs, int id, VkCommandBuffer primary_cmd) {
    if (id < 0 || id >= MAX_VK_CHUNKS) return;
    VkChunkSlot* slot = &cs->slots[id];
    if (!slot->is_compiled || slot->vertex_count == 0) return;

    vkCmdExecuteCommands(primary_cmd, 1, &slot->cmd_buf);
}

void vk_chunk_delete(VkChunkSystem* cs, int id) {
    if (id < 0 || id >= MAX_VK_CHUNKS) return;
    VkChunkSlot* slot = &cs->slots[id];
    if (!slot->is_used) return;

    vk_moo_buffer_destroy(cs->device, &slot->vertex_buf);
    if (slot->cmd_buf) {
        vkFreeCommandBuffers(cs->device, cs->cmd_pool, 1, &slot->cmd_buf);
    }
    memset(slot, 0, sizeof(VkChunkSlot));
}

void vk_chunk_system_cleanup(VkChunkSystem* cs) {
    for (int i = 0; i < MAX_VK_CHUNKS; i++)
        vk_chunk_delete(cs, i);
}
