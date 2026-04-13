/**
 * moo_3d_vulkan_mem.h — Vulkan Vertex Buffer + Command Recording + Chunk System.
 * Wird von moo_3d_vulkan.c inkludiert.
 */

#ifndef MOO_3D_VULKAN_MEM_H
#define MOO_3D_VULKAN_MEM_H

#include <vulkan/vulkan.h>
#include <stdbool.h>

/* === Vertex Format (same as GL33: 36 bytes) === */
typedef struct {
    float px, py, pz;
    float r, g, b;
    float nx, ny, nz;
} VkMooVertex;

#define VK_MOO_VERTEX_BINDING_DESC() \
    (VkVertexInputBindingDescription){ \
        .binding = 0, .stride = sizeof(VkMooVertex), \
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX }

/* 3 attribute descriptions: pos, color, normal */
void vk_moo_vertex_attr_descs(VkVertexInputAttributeDescription out[3]);

/* === GPU Buffer === */
typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size;
} VkMooBuffer;

/* Create a buffer with given usage + memory properties */
int vk_moo_buffer_create(VkDevice device, VkPhysicalDevice phys,
                         VkDeviceSize size,
                         VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags props,
                         VkMooBuffer* out);

void vk_moo_buffer_destroy(VkDevice device, VkMooBuffer* buf);

/* Upload data to a host-visible buffer */
int vk_moo_buffer_upload(VkDevice device, VkMooBuffer* buf,
                         const void* data, VkDeviceSize size);

/* Copy via staging buffer (host-visible → device-local) */
int vk_moo_buffer_staged_upload(VkDevice device, VkPhysicalDevice phys,
                                VkCommandPool pool, VkQueue queue,
                                const void* data, VkDeviceSize size,
                                VkMooBuffer* dst);

/* === Dynamic Vertex Collector === */
#define VK_MESH_INITIAL_CAP 1024

typedef struct {
    VkMooVertex* vertices;
    int count;
    int capacity;
} VkMeshCollector;

void vk_mesh_collector_init(VkMeshCollector* mc);
void vk_mesh_collector_reset(VkMeshCollector* mc);
void vk_mesh_collector_free(VkMeshCollector* mc);
void vk_mesh_collector_add_vertex(VkMeshCollector* mc,
                                  float px, float py, float pz,
                                  float r, float g, float b,
                                  float nx, float ny, float nz);
void vk_mesh_collector_add_cube(VkMeshCollector* mc,
                                float x, float y, float z, float size,
                                float r, float g, float b);

/* === Chunk System (Secondary Command Buffers) === */
#define MAX_VK_CHUNKS 256

typedef struct {
    VkMooBuffer vertex_buf;
    VkCommandBuffer cmd_buf;     /* secondary command buffer */
    int vertex_count;
    bool is_compiled;
    bool is_used;
} VkChunkSlot;

typedef struct {
    VkChunkSlot slots[MAX_VK_CHUNKS];
    VkCommandPool cmd_pool;
    VkDevice device;
    VkPhysicalDevice phys_device;
    VkQueue gfx_queue;
    VkRenderPass render_pass;
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
} VkChunkSystem;

void vk_chunk_system_init(VkChunkSystem* cs, VkDevice device,
                          VkPhysicalDevice phys, VkQueue queue,
                          VkCommandPool pool, VkRenderPass rp,
                          VkPipeline pipeline, VkPipelineLayout layout);
int  vk_chunk_alloc(VkChunkSystem* cs);
int  vk_chunk_upload(VkChunkSystem* cs, int id, VkMeshCollector* mc,
                     VkFramebuffer fb, VkExtent2D extent,
                     const float* push_constants, uint32_t push_size);
void vk_chunk_draw(VkChunkSystem* cs, int id,
                   VkCommandBuffer primary_cmd);
void vk_chunk_delete(VkChunkSystem* cs, int id);
void vk_chunk_system_cleanup(VkChunkSystem* cs);

/* === Memory Type Helper === */
uint32_t vk_find_memory_type(VkPhysicalDevice phys,
                             uint32_t type_filter,
                             VkMemoryPropertyFlags props);

#endif
