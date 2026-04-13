#ifndef MOO_3D_VULKAN_SYNC_H
#define MOO_3D_VULKAN_SYNC_H

/**
 * moo_3d_vulkan_sync.h — Vulkan Synchronisation Utilities.
 * Semaphores, Fences, Frame-Synchronisation fuer das Vulkan Backend.
 */

#include <vulkan/vulkan.h>

#define MAX_FRAMES_IN_FLIGHT 2

typedef struct {
    VkSemaphore image_available[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore render_finished[MAX_FRAMES_IN_FLIGHT];
    VkFence     in_flight[MAX_FRAMES_IN_FLIGHT];
    uint32_t    current_frame;
} VulkanSync;

/* Erstelle alle Sync-Objekte */
static inline int vulkan_sync_create(VkDevice device, VulkanSync* sync) {
    VkSemaphoreCreateInfo sem_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device, &sem_info, NULL, &sync->image_available[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &sem_info, NULL, &sync->render_finished[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fence_info, NULL, &sync->in_flight[i]) != VK_SUCCESS) {
            return -1;
        }
    }
    sync->current_frame = 0;
    return 0;
}

/* Warte auf aktuellen Frame-Fence */
static inline void vulkan_sync_wait_frame(VkDevice device, VulkanSync* sync) {
    vkWaitForFences(device, 1, &sync->in_flight[sync->current_frame], VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &sync->in_flight[sync->current_frame]);
}

/* Naechsten Frame-Index vorbereiten */
static inline void vulkan_sync_advance(VulkanSync* sync) {
    sync->current_frame = (sync->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

/* Alle Sync-Objekte zerstoeren */
static inline void vulkan_sync_destroy(VkDevice device, VulkanSync* sync) {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device, sync->image_available[i], NULL);
        vkDestroySemaphore(device, sync->render_finished[i], NULL);
        vkDestroyFence(device, sync->in_flight[i], NULL);
    }
}

/* Submit-Info Helfer: Erstellt VkSubmitInfo fuer aktuellen Frame */
static inline VkSubmitInfo vulkan_sync_submit_info(
    VulkanSync* sync,
    VkCommandBuffer* cmd_buf,
    VkPipelineStageFlags* wait_stage)
{
    VkSubmitInfo info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &sync->image_available[sync->current_frame],
        .pWaitDstStageMask = wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = cmd_buf,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &sync->render_finished[sync->current_frame],
    };
    return info;
}

/* Present-Info Helfer */
static inline VkPresentInfoKHR vulkan_sync_present_info(
    VulkanSync* sync,
    VkSwapchainKHR* swapchain,
    uint32_t* image_index)
{
    VkPresentInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &sync->render_finished[sync->current_frame],
        .swapchainCount = 1,
        .pSwapchains = swapchain,
        .pImageIndices = image_index,
    };
    return info;
}

#endif
