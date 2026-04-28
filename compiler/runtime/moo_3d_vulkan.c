/**
 * moo_3d_vulkan.c — Vulkan Backend fuer moo.
 * Implementiert Moo3DBackend mit Vulkan API.
 * Integriert: vulkan_mem (K3), vulkan_sync (K4), SPIR-V Shader (K2).
 */

#include "moo_3d_backend.h"
#include "moo_3d_math.h"
#include "moo_3d_vulkan_mem.h"
#include "moo_3d_vulkan_sync.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* Eingebettete SPIR-V Shader */
#include "moo_3d_vulkan_vert_spv.h"
#include "moo_3d_vulkan_frag_spv.h"

/* === Push Constants (MVP Matrix, 64 bytes) === */
typedef struct {
    float mvp[16];
} VulkanPushConstants;

/* === UBO Layout (model, lightDir, fogDist, fogColor) === */
/* SPIR-V Offsets: model@0, lightDir@64, fogDist@76, fogColor@80 */
typedef struct {
    float model[16];       /* offset 0,  64 bytes */
    float lightDir[3];     /* offset 64, 12 bytes */
    float fogDist;         /* offset 76,  4 bytes — packed nach lightDir */
    float fogColor[4];     /* offset 80, 16 bytes */
} VulkanUBO;               /* Total: 96 bytes */

/* === Vulkan Context === */
typedef struct {
    GLFWwindow* window;
    int width, height;

    /* Core */
    VkInstance instance;
    VkPhysicalDevice phys_device;
    VkDevice device;
    VkQueue gfx_queue;
    uint32_t gfx_family;
    VkSurfaceKHR surface;

    /* Swapchain */
    VkSwapchainKHR swapchain;
    VkImage* swapchain_images;
    VkImageView* swapchain_views;
    uint32_t swapchain_count;
    VkFormat swapchain_format;
    VkExtent2D swapchain_extent;

    /* Depth */
    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;

    /* Render Pass + Pipeline */
    VkRenderPass render_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkDescriptorSetLayout desc_layout;
    VkDescriptorPool desc_pool;
    VkDescriptorSet desc_sets[MAX_FRAMES_IN_FLIGHT];

    /* UBO */
    VkMooBuffer ubo_buffers[MAX_FRAMES_IN_FLIGHT];
    VulkanUBO ubo_data;

    /* Framebuffers */
    VkFramebuffer* framebuffers;

    /* Commands */
    VkCommandPool cmd_pool;
    VkCommandBuffer cmd_buffers[MAX_FRAMES_IN_FLIGHT];

    /* Sync */
    VulkanSync sync;

    /* Matrix Stack */
    MooMatrixStack matrix_stack;
    float projection[16];
    float view[16];

    /* Chunk System */
    VkChunkSystem chunk_sys;
    VkMeshCollector mesh_collector;
    int active_chunk;

    /* Immediate-mode stream buffer */
    VkMooBuffer stream_vbo;

    /* Mouse */
    double last_mouse_x;
    double last_mouse_y;
    int mouse_captured;
    double scroll_acc_x;
    double scroll_acc_y;

    /* Screenshot — Index des zuletzt acquirierten Swapchain-Images */
    uint32_t last_image_index;

    /* Test-Sim: programmatische Maus-Eingaben */
    int sim_pos_active;
    float sim_x, sim_y;
    int sim_button[3];        /* 0=LMB, 1=RMB, 2=MMB; 1 = pressed */
} VulkanContext;

/* Forward decl */
static void vk_scroll_callback(GLFWwindow* w, double xoff, double yoff);

/* === Helper: Find queue family === */
static int find_gfx_queue_family(VkPhysicalDevice phys, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, NULL);
    VkQueueFamilyProperties* props = malloc(count * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, props);

    for (uint32_t i = 0; i < count; i++) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surface, &present);
        if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
            free(props);
            return (int)i;
        }
    }
    free(props);
    return -1;
}

/* === Helper: Create shader module === */
static VkShaderModule create_shader_module(VkDevice device, const unsigned char* code, uint32_t size) {
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = (const uint32_t*)code
    };
    VkShaderModule mod;
    if (vkCreateShaderModule(device, &info, NULL, &mod) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return mod;
}

/* === Create Swapchain === */
static int create_swapchain(VulkanContext* ctx) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->phys_device, ctx->surface, &caps);

    /* Wähle ein unterstütztes Format (bevorzugt UNORM für korrekte Farben) */
    uint32_t fmt_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->phys_device, ctx->surface, &fmt_count, NULL);
    VkSurfaceFormatKHR* fmts = malloc(fmt_count * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->phys_device, ctx->surface, &fmt_count, fmts);
    ctx->swapchain_format = fmts[0].format;
    VkColorSpaceKHR color_space = fmts[0].colorSpace;
    /* Bevorzuge B8G8R8A8_UNORM für lineare Farbwiedergabe */
    for (uint32_t i = 0; i < fmt_count; i++) {
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
            ctx->swapchain_format = fmts[i].format;
            color_space = fmts[i].colorSpace;
            break;
        }
    }
    free(fmts);

    ctx->swapchain_extent = caps.currentExtent;
    if (ctx->swapchain_extent.width == UINT32_MAX) {
        ctx->swapchain_extent.width = (uint32_t)ctx->width;
        ctx->swapchain_extent.height = (uint32_t)ctx->height;
    }

    uint32_t img_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && img_count > caps.maxImageCount)
        img_count = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sc_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = ctx->surface,
        .minImageCount = img_count,
        .imageFormat = ctx->swapchain_format,
        .imageColorSpace = color_space,
        .imageExtent = ctx->swapchain_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };

    if (vkCreateSwapchainKHR(ctx->device, &sc_info, NULL, &ctx->swapchain) != VK_SUCCESS)
        return -1;

    vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &ctx->swapchain_count, NULL);
    ctx->swapchain_images = malloc(ctx->swapchain_count * sizeof(VkImage));
    vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &ctx->swapchain_count, ctx->swapchain_images);

    ctx->swapchain_views = malloc(ctx->swapchain_count * sizeof(VkImageView));
    for (uint32_t i = 0; i < ctx->swapchain_count; i++) {
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = ctx->swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = ctx->swapchain_format,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };
        vkCreateImageView(ctx->device, &view_info, NULL, &ctx->swapchain_views[i]);
    }
    return 0;
}

/* === Create Depth Buffer === */
static int create_depth(VulkanContext* ctx) {
    VkFormat fmt = VK_FORMAT_D32_SFLOAT;
    VkImageCreateInfo img_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = fmt,
        .extent = { ctx->swapchain_extent.width, ctx->swapchain_extent.height, 1 },
        .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    };
    if (vkCreateImage(ctx->device, &img_info, NULL, &ctx->depth_image) != VK_SUCCESS) return -1;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(ctx->device, ctx->depth_image, &req);
    VkMemoryAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = vk_find_memory_type(ctx->phys_device, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    vkAllocateMemory(ctx->device, &alloc, NULL, &ctx->depth_memory);
    vkBindImageMemory(ctx->device, ctx->depth_image, ctx->depth_memory, 0);

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = ctx->depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = fmt,
        .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 }
    };
    return vkCreateImageView(ctx->device, &view_info, NULL, &ctx->depth_view) == VK_SUCCESS ? 0 : -1;
}

/* === Create Render Pass === */
static int create_render_pass(VulkanContext* ctx) {
    VkAttachmentDescription attachments[2] = {
        { /* Color */
            .format = ctx->swapchain_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        },
        { /* Depth */
            .format = VK_FORMAT_D32_SFLOAT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        }
    };
    VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depth_ref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
        .pDepthStencilAttachment = &depth_ref,
    };
    VkSubpassDependency dep = {
        .srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };
    VkRenderPassCreateInfo rp_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2, .pAttachments = attachments,
        .subpassCount = 1, .pSubpasses = &subpass,
        .dependencyCount = 1, .pDependencies = &dep,
    };
    return vkCreateRenderPass(ctx->device, &rp_info, NULL, &ctx->render_pass) == VK_SUCCESS ? 0 : -1;
}

/* === Create Graphics Pipeline === */
static int create_pipeline(VulkanContext* ctx) {
    VkShaderModule vert = create_shader_module(ctx->device, vulkan_vert_spv, vulkan_vert_spv_len);
    VkShaderModule frag = create_shader_module(ctx->device, vulkan_frag_spv, vulkan_frag_spv_len);
    if (!vert || !frag) return -1;

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" },
    };

    VkVertexInputBindingDescription bind = VK_MOO_VERTEX_BINDING_DESC();
    VkVertexInputAttributeDescription attrs[3];
    vk_moo_vertex_attr_descs(attrs);

    VkPipelineVertexInputStateCreateInfo vert_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &bind,
        .vertexAttributeDescriptionCount = 3, .pVertexAttributeDescriptions = attrs,
    };
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkViewport viewport = { 0, 0, (float)ctx->swapchain_extent.width, (float)ctx->swapchain_extent.height, 0, 1 };
    VkRect2D scissor = { {0, 0}, ctx->swapchain_extent };
    VkPipelineViewportStateCreateInfo vp_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport,
        .scissorCount = 1, .pScissors = &scissor,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };
    VkPipelineColorBlendAttachmentState blend_att = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend_att,
    };

    /* Push Constants: MVP matrix (64 bytes) */
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0, .size = sizeof(VulkanPushConstants),
    };

    /* Descriptor Set Layout for UBO */
    VkDescriptorSetLayoutBinding ubo_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo desc_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &ubo_binding,
    };
    vkCreateDescriptorSetLayout(ctx->device, &desc_layout_info, NULL, &ctx->desc_layout);

    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &ctx->desc_layout,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push_range,
    };
    vkCreatePipelineLayout(ctx->device, &layout_info, NULL, &ctx->pipeline_layout);

    VkGraphicsPipelineCreateInfo pipe_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vert_input,
        .pInputAssemblyState = &assembly,
        .pViewportState = &vp_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &ms,
        .pDepthStencilState = &depth,
        .pColorBlendState = &blend,
        .layout = ctx->pipeline_layout,
        .renderPass = ctx->render_pass,
    };
    VkResult res = vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1, &pipe_info, NULL, &ctx->pipeline);

    vkDestroyShaderModule(ctx->device, vert, NULL);
    vkDestroyShaderModule(ctx->device, frag, NULL);
    return res == VK_SUCCESS ? 0 : -1;
}

/* === Create Framebuffers === */
static int create_framebuffers(VulkanContext* ctx) {
    ctx->framebuffers = malloc(ctx->swapchain_count * sizeof(VkFramebuffer));
    for (uint32_t i = 0; i < ctx->swapchain_count; i++) {
        VkImageView views[2] = { ctx->swapchain_views[i], ctx->depth_view };
        VkFramebufferCreateInfo fb_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = ctx->render_pass,
            .attachmentCount = 2, .pAttachments = views,
            .width = ctx->swapchain_extent.width,
            .height = ctx->swapchain_extent.height,
            .layers = 1,
        };
        vkCreateFramebuffer(ctx->device, &fb_info, NULL, &ctx->framebuffers[i]);
    }
    return 0;
}

/* === Create UBO + Descriptor Sets === */
static int create_ubo(VulkanContext* ctx) {
    /* Descriptor Pool */
    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = MAX_FRAMES_IN_FLIGHT,
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = 1, .pPoolSizes = &pool_size,
    };
    vkCreateDescriptorPool(ctx->device, &pool_info, NULL, &ctx->desc_pool);

    /* UBO Buffers + Descriptor Sets */
    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) layouts[i] = ctx->desc_layout;

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = ctx->desc_pool,
        .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
        .pSetLayouts = layouts,
    };
    vkAllocateDescriptorSets(ctx->device, &alloc_info, ctx->desc_sets);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk_moo_buffer_create(ctx->device, ctx->phys_device, sizeof(VulkanUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &ctx->ubo_buffers[i]);

        VkDescriptorBufferInfo buf_info = {
            .buffer = ctx->ubo_buffers[i].buffer,
            .offset = 0, .range = sizeof(VulkanUBO),
        };
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ctx->desc_sets[i],
            .dstBinding = 0, .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &buf_info,
        };
        vkUpdateDescriptorSets(ctx->device, 1, &write, 0, NULL);
    }

    /* Default UBO values */
    mat4_identity(ctx->ubo_data.model);
    ctx->ubo_data.lightDir[0] = -0.7f;
    ctx->ubo_data.lightDir[1] = 0.4f;
    ctx->ubo_data.lightDir[2] = 0.5f;
    ctx->ubo_data.fogDist = 20.0f;
    // Horizont-Farbe: Passt zu welten.moo Clear-Color (0.53, 0.81, 0.92)
    // Etwas heller fuer nahtlosen Nebel→Himmel Uebergang
    ctx->ubo_data.fogColor[0] = 0.85f;
    ctx->ubo_data.fogColor[1] = 0.87f;
    ctx->ubo_data.fogColor[2] = 0.90f;
    ctx->ubo_data.fogColor[3] = 0.0f;

    /* WICHTIG: UBO sofort in ALLE Frame-Buffers hochladen (nicht auf ersten Swap warten) */
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk_moo_buffer_upload(ctx->device, &ctx->ubo_buffers[i],
            &ctx->ubo_data, sizeof(VulkanUBO));
    }
    return 0;
}

/* ================================================================
 * Backend Interface Implementation
 * ================================================================ */

static void* vk_create_window(const char* title, int w, int h) {
    if (!glfwInit()) return NULL;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* win = glfwCreateWindow(w, h, title, NULL, NULL);
    if (!win) return NULL;

    VulkanContext* ctx = (VulkanContext*)calloc(1, sizeof(VulkanContext));
    ctx->window = win;
    ctx->width = w;
    ctx->height = h;
    ctx->active_chunk = -1;
    ctx->scroll_acc_x = 0;
    ctx->scroll_acc_y = 0;

    /* Scroll-Callback registrieren (UserPointer auf VulkanContext) */
    glfwSetWindowUserPointer(win, ctx);
    glfwSetScrollCallback(win, vk_scroll_callback);

    /* Instance */
    uint32_t glfw_ext_count;
    const char** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_ext_count);
    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .enabledExtensionCount = glfw_ext_count,
        .ppEnabledExtensionNames = glfw_exts,
    };
    #ifndef NDEBUG
    const char* validation[] = { "VK_LAYER_KHRONOS_validation" };
    inst_info.enabledLayerCount = 1;
    inst_info.ppEnabledLayerNames = validation;
    #endif
    if (vkCreateInstance(&inst_info, NULL, &ctx->instance) != VK_SUCCESS) goto fail;

    /* Surface */
    if (glfwCreateWindowSurface(ctx->instance, win, NULL, &ctx->surface) != VK_SUCCESS) goto fail;

    /* Physical Device (erste GPU) */
    uint32_t dev_count = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &dev_count, NULL);
    if (dev_count == 0) goto fail;
    VkPhysicalDevice* devices = malloc(dev_count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(ctx->instance, &dev_count, devices);
    ctx->phys_device = devices[0];
    free(devices);

    /* Queue Family */
    int qf = find_gfx_queue_family(ctx->phys_device, ctx->surface);
    if (qf < 0) goto fail;
    ctx->gfx_family = (uint32_t)qf;

    /* Logical Device */
    float priority = 1.0f;
    VkDeviceQueueCreateInfo q_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = ctx->gfx_family,
        .queueCount = 1, .pQueuePriorities = &priority,
    };
    const char* dev_exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dev_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &q_info,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = dev_exts,
    };
    if (vkCreateDevice(ctx->phys_device, &dev_info, NULL, &ctx->device) != VK_SUCCESS) goto fail;
    vkGetDeviceQueue(ctx->device, ctx->gfx_family, 0, &ctx->gfx_queue);

    /* Swapchain + Depth + RenderPass + Pipeline + Framebuffers */
    if (create_swapchain(ctx) < 0) goto fail;
    if (create_depth(ctx) < 0) goto fail;
    if (create_render_pass(ctx) < 0) goto fail;
    if (create_pipeline(ctx) < 0) goto fail;
    if (create_framebuffers(ctx) < 0) goto fail;

    /* Command Pool + Buffers */
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->gfx_family,
    };
    vkCreateCommandPool(ctx->device, &pool_info, NULL, &ctx->cmd_pool);

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };
    vkAllocateCommandBuffers(ctx->device, &alloc_info, ctx->cmd_buffers);

    /* UBO + Descriptors */
    if (create_ubo(ctx) < 0) goto fail;

    /* Sync */
    if (vulkan_sync_create(ctx->device, &ctx->sync) < 0) goto fail;

    /* Matrix Stack */
    moo_matrix_stack_init(&ctx->matrix_stack);
    mat4_identity(ctx->projection);
    mat4_identity(ctx->view);

    /* Chunk System */
    vk_chunk_system_init(&ctx->chunk_sys, ctx->device, ctx->phys_device,
        ctx->gfx_queue, ctx->cmd_pool, ctx->render_pass,
        ctx->pipeline, ctx->pipeline_layout);

    /* Mesh Collector */
    vk_mesh_collector_init(&ctx->mesh_collector);

    return ctx;

fail:
    if (ctx) free(ctx);
    glfwDestroyWindow(win);
    return NULL;
}

static int vk_is_open(void* raw) {
    VulkanContext* ctx = (VulkanContext*)raw;
    return !glfwWindowShouldClose(ctx->window);
}

/* Clear color stored for begin render pass */
static float g_clear_r, g_clear_g, g_clear_b;

static void vk_clear(void* raw, float r, float g, float b) {
    (void)raw;
    g_clear_r = r; g_clear_g = g; g_clear_b = b;
}

static void vk_swap(void* raw) {
    VulkanContext* ctx = (VulkanContext*)raw;

    vulkan_sync_wait_frame(ctx->device, &ctx->sync);

    uint32_t img_idx;
    vkAcquireNextImageKHR(ctx->device, ctx->swapchain, UINT64_MAX,
        ctx->sync.image_available[ctx->sync.current_frame], VK_NULL_HANDLE, &img_idx);
    ctx->last_image_index = img_idx;

    /* Update UBO */
    memcpy(ctx->ubo_data.model, ctx->matrix_stack.current, 64);
    vk_moo_buffer_upload(ctx->device, &ctx->ubo_buffers[ctx->sync.current_frame],
        &ctx->ubo_data, sizeof(VulkanUBO));

    /* Record commands */
    VkCommandBuffer cmd = ctx->cmd_buffers[ctx->sync.current_frame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &begin);

    VkClearValue clears[2] = {
        { .color = {{ g_clear_r, g_clear_g, g_clear_b, 1.0f }} },
        { .depthStencil = { 1.0f, 0 } }
    };
    VkRenderPassBeginInfo rp_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = ctx->render_pass,
        .framebuffer = ctx->framebuffers[img_idx],
        .renderArea = { {0, 0}, ctx->swapchain_extent },
        .clearValueCount = 2, .pClearValues = clears,
    };
    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    /* Bind pipeline + descriptors */
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        ctx->pipeline_layout, 0, 1,
        &ctx->desc_sets[ctx->sync.current_frame], 0, NULL);

    /* Push MVP matrix */
    float mv[16], mvp[16];
    mat4_multiply(mv, ctx->view, ctx->matrix_stack.current);
    mat4_multiply(mvp, ctx->projection, mv);
    VulkanPushConstants pc;
    memcpy(pc.mvp, mvp, 64);
    vkCmdPushConstants(cmd, ctx->pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    /* Flush immediate-mode geometry (non-chunk cube/sphere/triangle calls) */
    if (ctx->mesh_collector.count > 0) {
        VkDeviceSize data_size = (VkDeviceSize)ctx->mesh_collector.count * sizeof(VkMooVertex);

        /* Recreate stream VBO if too small */
        if (ctx->stream_vbo.size < data_size) {
            if (ctx->stream_vbo.buffer)
                vk_moo_buffer_destroy(ctx->device, &ctx->stream_vbo);
            vk_moo_buffer_create(ctx->device, ctx->phys_device, data_size,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &ctx->stream_vbo);
        }
        vk_moo_buffer_upload(ctx->device, &ctx->stream_vbo,
            ctx->mesh_collector.vertices, data_size);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &ctx->stream_vbo.buffer, &offset);
        vkCmdDraw(cmd, (uint32_t)ctx->mesh_collector.count, 1, 0, 0);

        vk_mesh_collector_reset(&ctx->mesh_collector);
    }

    /* Draw cached chunks (inline — rebind pipeline already done) */
    for (int i = 0; i < MAX_VK_CHUNKS; i++) {
        if (ctx->chunk_sys.slots[i].is_used && ctx->chunk_sys.slots[i].is_compiled) {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1,
                &ctx->chunk_sys.slots[i].vertex_buf.buffer, &offset);
            vkCmdDraw(cmd, (uint32_t)ctx->chunk_sys.slots[i].vertex_count, 1, 0, 0);
        }
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    /* Submit */
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = vulkan_sync_submit_info(&ctx->sync, &cmd, &wait_stage);
    vkQueueSubmit(ctx->gfx_queue, 1, &submit, ctx->sync.in_flight[ctx->sync.current_frame]);

    /* Present */
    VkPresentInfoKHR present = vulkan_sync_present_info(&ctx->sync, &ctx->swapchain, &img_idx);
    vkQueuePresentKHR(ctx->gfx_queue, &present);

    vulkan_sync_advance(&ctx->sync);
    glfwPollEvents();
}

static void vk_close(void* raw) {
    VulkanContext* ctx = (VulkanContext*)raw;
    vkDeviceWaitIdle(ctx->device);

    vk_chunk_system_cleanup(&ctx->chunk_sys);
    vk_mesh_collector_free(&ctx->mesh_collector);
    if (ctx->stream_vbo.buffer)
        vk_moo_buffer_destroy(ctx->device, &ctx->stream_vbo);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        vk_moo_buffer_destroy(ctx->device, &ctx->ubo_buffers[i]);

    vulkan_sync_destroy(ctx->device, &ctx->sync);
    vkDestroyDescriptorPool(ctx->device, ctx->desc_pool, NULL);
    vkDestroyDescriptorSetLayout(ctx->device, ctx->desc_layout, NULL);

    for (uint32_t i = 0; i < ctx->swapchain_count; i++)
        vkDestroyFramebuffer(ctx->device, ctx->framebuffers[i], NULL);
    free(ctx->framebuffers);

    vkDestroyPipeline(ctx->device, ctx->pipeline, NULL);
    vkDestroyPipelineLayout(ctx->device, ctx->pipeline_layout, NULL);
    vkDestroyRenderPass(ctx->device, ctx->render_pass, NULL);

    vkDestroyImageView(ctx->device, ctx->depth_view, NULL);
    vkDestroyImage(ctx->device, ctx->depth_image, NULL);
    vkFreeMemory(ctx->device, ctx->depth_memory, NULL);

    for (uint32_t i = 0; i < ctx->swapchain_count; i++)
        vkDestroyImageView(ctx->device, ctx->swapchain_views[i], NULL);
    free(ctx->swapchain_views);
    free(ctx->swapchain_images);
    vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);

    vkDestroyCommandPool(ctx->device, ctx->cmd_pool, NULL);
    vkDestroyDevice(ctx->device, NULL);
    vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);
    vkDestroyInstance(ctx->instance, NULL);

    glfwDestroyWindow(ctx->window);
    free(ctx);
}

static void vk_perspective(void* raw, float fov, float near_val, float far_val) {
    VulkanContext* ctx = (VulkanContext*)raw;
    float aspect = (float)ctx->width / (float)ctx->height;
    mat4_perspective(ctx->projection, fov, aspect, near_val, far_val);
    /* Vulkan Clip Space: Y nach unten (OpenGL: Y nach oben) → Y flippen */
    ctx->projection[5] *= -1.0f;
}

static void vk_camera(void* raw, float ex, float ey, float ez, float lx, float ly, float lz) {
    VulkanContext* ctx = (VulkanContext*)raw;
    mat4_lookat(ctx->view, ex, ey, ez, lx, ly, lz, 0, 1, 0);
}

static void vk_push_matrix(void* raw) {
    VulkanContext* ctx = (VulkanContext*)raw;
    moo_matrix_stack_push(&ctx->matrix_stack);
}

static void vk_pop_matrix(void* raw) {
    VulkanContext* ctx = (VulkanContext*)raw;
    moo_matrix_stack_pop(&ctx->matrix_stack);
}

static void vk_translate(void* raw, float x, float y, float z) {
    VulkanContext* ctx = (VulkanContext*)raw;
    float t[16];
    mat4_translate(t, x, y, z);
    float result[16];
    mat4_multiply(result, ctx->matrix_stack.current, t);
    memcpy(ctx->matrix_stack.current, result, 64);
}

static void vk_rotate(void* raw, float angle, float ax, float ay, float az) {
    VulkanContext* ctx = (VulkanContext*)raw;
    float r[16];
    mat4_rotate(r, angle, ax, ay, az);
    float result[16];
    mat4_multiply(result, ctx->matrix_stack.current, r);
    memcpy(ctx->matrix_stack.current, result, 64);
}

static void vk_cube(void* raw, float x, float y, float z, float size, float r, float g, float b) {
    VulkanContext* ctx = (VulkanContext*)raw;
    vk_mesh_collector_add_cube(&ctx->mesh_collector, x, y, z, size, r, g, b);
}

static void vk_sphere(void* raw, float x, float y, float z, float radius, float r, float g, float b, int detail) {
    VulkanContext* ctx = (VulkanContext*)raw;
    /* UV sphere via mesh collector */
    int slices = detail < 4 ? 4 : (detail > 64 ? 64 : detail);
    int stacks = slices;
    for (int i = 0; i < stacks; i++) {
        float lat0 = M_PI * (-0.5f + (float)i / stacks);
        float lat1 = M_PI * (-0.5f + (float)(i + 1) / stacks);
        float y0 = sinf(lat0), yr0 = cosf(lat0);
        float y1 = sinf(lat1), yr1 = cosf(lat1);
        for (int j = 0; j < slices; j++) {
            float lng0 = 2.0f * M_PI * (float)j / slices;
            float lng1 = 2.0f * M_PI * (float)(j + 1) / slices;
            float x0 = cosf(lng0), z0 = sinf(lng0);
            float x1 = cosf(lng1), z1 = sinf(lng1);
            /* Two triangles per quad */
            vk_mesh_collector_add_vertex(&ctx->mesh_collector,
                x + radius*x0*yr0, y + radius*y0, z + radius*z0*yr0, r, g, b, x0*yr0, y0, z0*yr0);
            vk_mesh_collector_add_vertex(&ctx->mesh_collector,
                x + radius*x0*yr1, y + radius*y1, z + radius*z0*yr1, r, g, b, x0*yr1, y1, z0*yr1);
            vk_mesh_collector_add_vertex(&ctx->mesh_collector,
                x + radius*x1*yr1, y + radius*y1, z + radius*z1*yr1, r, g, b, x1*yr1, y1, z1*yr1);

            vk_mesh_collector_add_vertex(&ctx->mesh_collector,
                x + radius*x0*yr0, y + radius*y0, z + radius*z0*yr0, r, g, b, x0*yr0, y0, z0*yr0);
            vk_mesh_collector_add_vertex(&ctx->mesh_collector,
                x + radius*x1*yr1, y + radius*y1, z + radius*z1*yr1, r, g, b, x1*yr1, y1, z1*yr1);
            vk_mesh_collector_add_vertex(&ctx->mesh_collector,
                x + radius*x1*yr0, y + radius*y0, z + radius*z1*yr0, r, g, b, x1*yr0, y0, z1*yr0);
        }
    }
}

static void vk_triangle(void* raw,
    float x1, float y1, float z1, float x2, float y2, float z2,
    float x3, float y3, float z3, float r, float g, float b)
{
    VulkanContext* ctx = (VulkanContext*)raw;
    /* Compute normal */
    float ax = x2-x1, ay = y2-y1, az = z2-z1;
    float bx = x3-x1, by = y3-y1, bz = z3-z1;
    float nx = ay*bz - az*by, ny = az*bx - ax*bz, nz = ax*by - ay*bx;
    float len = sqrtf(nx*nx + ny*ny + nz*nz);
    if (len > 0) { nx/=len; ny/=len; nz/=len; }
    vk_mesh_collector_add_vertex(&ctx->mesh_collector, x1,y1,z1, r,g,b, nx,ny,nz);
    vk_mesh_collector_add_vertex(&ctx->mesh_collector, x2,y2,z2, r,g,b, nx,ny,nz);
    vk_mesh_collector_add_vertex(&ctx->mesh_collector, x3,y3,z3, r,g,b, nx,ny,nz);
}

static int vk_key_pressed(void* raw, const char* name) {
    VulkanContext* ctx = (VulkanContext*)raw;
    int key = 0;
    if (strcmp(name, "oben") == 0 || strcmp(name, "up") == 0) key = GLFW_KEY_UP;
    else if (strcmp(name, "unten") == 0 || strcmp(name, "down") == 0) key = GLFW_KEY_DOWN;
    else if (strcmp(name, "links") == 0 || strcmp(name, "left") == 0) key = GLFW_KEY_LEFT;
    else if (strcmp(name, "rechts") == 0 || strcmp(name, "right") == 0) key = GLFW_KEY_RIGHT;
    else if (strcmp(name, "leertaste") == 0 || strcmp(name, "space") == 0) key = GLFW_KEY_SPACE;
    else if (strcmp(name, "escape") == 0) key = GLFW_KEY_ESCAPE;
    else if (strcmp(name, "shift") == 0) key = GLFW_KEY_LEFT_SHIFT;
    else if (strlen(name) == 1 && name[0] >= 'a' && name[0] <= 'z')
        key = GLFW_KEY_A + (name[0] - 'a');
    else return 0;
    return glfwGetKey(ctx->window, key) == GLFW_PRESS;
}

/* Chunk interface */
static int vk_chunk_create_fn(void* raw) {
    VulkanContext* ctx = (VulkanContext*)raw;
    return vk_chunk_alloc(&ctx->chunk_sys);
}

static void vk_chunk_begin_fn(void* raw, int id) {
    VulkanContext* ctx = (VulkanContext*)raw;
    ctx->active_chunk = id;
    vk_mesh_collector_reset(&ctx->mesh_collector);
}

static void vk_chunk_end_fn(void* raw) {
    VulkanContext* ctx = (VulkanContext*)raw;
    if (ctx->active_chunk < 0) return;

    /* Compute MVP for push constants */
    float mv[16], mvp[16];
    mat4_multiply(mv, ctx->view, ctx->matrix_stack.current);
    mat4_multiply(mvp, ctx->projection, mv);

    VulkanPushConstants pc;
    memcpy(pc.mvp, mvp, 64);

    vk_chunk_upload(&ctx->chunk_sys, ctx->active_chunk, &ctx->mesh_collector,
        ctx->framebuffers[0], ctx->swapchain_extent,
        (const float*)&pc, sizeof(pc));
    ctx->active_chunk = -1;
}

static void vk_chunk_draw_fn(void* raw, int id) {
    (void)raw;
    (void)id;
    /* Chunks are drawn automatically in vk_swap() */
}

static void vk_chunk_delete_fn(void* raw, int id) {
    VulkanContext* ctx = (VulkanContext*)raw;
    vk_chunk_delete(&ctx->chunk_sys, id);
}

/* === Export Backend === */
/* ========================================================
 * Mouse
 * ======================================================== */

static void vk_capture_mouse(void* vctx) {
    VulkanContext* ctx = (VulkanContext*)vctx;
    if (!ctx || !ctx->window) return;
    glfwSetInputMode(ctx->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwGetCursorPos(ctx->window, &ctx->last_mouse_x, &ctx->last_mouse_y);
    ctx->mouse_captured = 1;
}

static void vk_release_mouse(void* vctx) {
    VulkanContext* ctx = (VulkanContext*)vctx;
    if (!ctx || !ctx->window) return;
    glfwSetInputMode(ctx->window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    ctx->mouse_captured = 0;
}

static float vk_mouse_x(void* vctx) {
    VulkanContext* ctx = (VulkanContext*)vctx;
    if (!ctx || !ctx->window) return 0.0f;
    if (ctx->sim_pos_active) return ctx->sim_x;
    double cx, cy;
    glfwGetCursorPos(ctx->window, &cx, &cy);
    return (float)cx;
}

static float vk_mouse_y(void* vctx) {
    VulkanContext* ctx = (VulkanContext*)vctx;
    if (!ctx || !ctx->window) return 0.0f;
    if (ctx->sim_pos_active) return ctx->sim_y;
    double cx, cy;
    glfwGetCursorPos(ctx->window, &cx, &cy);
    return (float)cy;
}

static int vk_mouse_button(void* vctx, int btn) {
    VulkanContext* ctx = (VulkanContext*)vctx;
    if (!ctx || !ctx->window) return 0;
    if (btn >= 0 && btn < 3 && ctx->sim_button[btn]) return 1;
    int glfw_btn = (btn == 0) ? GLFW_MOUSE_BUTTON_LEFT
                  : (btn == 1) ? GLFW_MOUSE_BUTTON_RIGHT
                  : GLFW_MOUSE_BUTTON_MIDDLE;
    return glfwGetMouseButton(ctx->window, glfw_btn) == GLFW_PRESS ? 1 : 0;
}

static void vk_simulate_mouse_pos(void* vctx, float x, float y) {
    VulkanContext* ctx = (VulkanContext*)vctx;
    if (!ctx) return;
    ctx->sim_pos_active = 1;
    ctx->sim_x = x;
    ctx->sim_y = y;
}

static void vk_simulate_mouse_button(void* vctx, int btn, int pressed) {
    VulkanContext* ctx = (VulkanContext*)vctx;
    if (!ctx || btn < 0 || btn >= 3) return;
    ctx->sim_button[btn] = pressed ? 1 : 0;
}

static void vk_simulate_scroll(void* vctx, float dy) {
    VulkanContext* ctx = (VulkanContext*)vctx;
    if (!ctx) return;
    ctx->scroll_acc_y += dy;
}

static float vk_mouse_wheel(void* vctx) {
    VulkanContext* ctx = (VulkanContext*)vctx;
    if (!ctx) return 0.0f;
    float v = (float)ctx->scroll_acc_y;
    ctx->scroll_acc_y = 0;
    return v;
}

static void vk_scroll_callback(GLFWwindow* w, double xoff, double yoff) {
    VulkanContext* ctx = (VulkanContext*)glfwGetWindowUserPointer(w);
    if (!ctx) return;
    ctx->scroll_acc_x += xoff;
    ctx->scroll_acc_y += yoff;
}

static float vk_mouse_dx(void* vctx) {
    VulkanContext* ctx = (VulkanContext*)vctx;
    if (!ctx || !ctx->window || !ctx->mouse_captured) return 0.0f;
    double cx, cy;
    glfwGetCursorPos(ctx->window, &cx, &cy);
    float dx = (float)(cx - ctx->last_mouse_x);
    ctx->last_mouse_x = cx;
    return dx;
}

static float vk_mouse_dy(void* vctx) {
    VulkanContext* ctx = (VulkanContext*)vctx;
    if (!ctx || !ctx->window || !ctx->mouse_captured) return 0.0f;
    double cx, cy;
    glfwGetCursorPos(ctx->window, &cx, &cy);
    float dy = (float)(cy - ctx->last_mouse_y);
    ctx->last_mouse_y = cy;
    return dy;
}

static void vk_set_fog_density(void* raw, float density) {
    VulkanContext* ctx = (VulkanContext*)raw;
    ctx->ubo_data.fogDist = 1.0f / (density > 0.001f ? density : 0.001f);
}

static void vk_set_light_dir(void* raw, float x, float y, float z) {
    VulkanContext* ctx = (VulkanContext*)raw;
    ctx->ubo_data.lightDir[0] = x;
    ctx->ubo_data.lightDir[1] = y;
    ctx->ubo_data.lightDir[2] = z;
}

static void vk_set_ambient(void* raw, float level) {
    (void)raw; (void)level;
    /* Vulkan ambient ist im Shader hardcoded — TODO: Uniform */
}

/* === Vulkan Screenshot ============================================
 * Kopiert das zuletzt praesentierte Swapchain-Image via
 * vkCmdCopyImageToBuffer in einen Host-Visible Staging-Buffer und
 * schreibt es als 24-bit BMP. Verlangt VK_IMAGE_USAGE_TRANSFER_SRC_BIT
 * auf der Swapchain (in create_swapchain bereits gesetzt).
 * ================================================================= */
static int vk_screenshot_bmp(void* raw, const char* path) {
    VulkanContext* ctx = (VulkanContext*)raw;
    if (!ctx || !path) return 0;

    /* Auf alle GPU-Operationen warten, damit das Image stabil ist. */
    vkDeviceWaitIdle(ctx->device);

    int w = (int)ctx->swapchain_extent.width;
    int h = (int)ctx->swapchain_extent.height;
    if (w <= 0 || h <= 0) return 0;

    VkDeviceSize buf_size = (VkDeviceSize)w * (VkDeviceSize)h * 4;

    /* Staging-Buffer (host-visible). */
    VkMooBuffer staging = {0};
    if (vk_moo_buffer_create(ctx->device, ctx->phys_device, buf_size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &staging) != 0) {
        return 0;
    }

    /* One-shot Command-Buffer aus dem Pool. */
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cb_alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(ctx->device, &cb_alloc, &cmd) != VK_SUCCESS) {
        vk_moo_buffer_destroy(ctx->device, &staging);
        return 0;
    }

    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin);

    VkImage src = ctx->swapchain_images[ctx->last_image_index];

    /* Layout: PRESENT_SRC -> TRANSFER_SRC_OPTIMAL */
    VkImageMemoryBarrier to_xfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = src,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &to_xfer);

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { (uint32_t)w, (uint32_t)h, 1 },
    };
    vkCmdCopyImageToBuffer(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        staging.buffer, 1, &region);

    /* Layout zurueck: TRANSFER_SRC_OPTIMAL -> PRESENT_SRC */
    VkImageMemoryBarrier to_present = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = src,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, NULL, 0, NULL, 1, &to_present);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &cmd,
    };
    vkQueueSubmit(ctx->gfx_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->gfx_queue);
    vkFreeCommandBuffers(ctx->device, ctx->cmd_pool, 1, &cmd);

    void* mapped = NULL;
    if (vkMapMemory(ctx->device, staging.memory, 0, buf_size, 0, &mapped) != VK_SUCCESS) {
        vk_moo_buffer_destroy(ctx->device, &staging);
        return 0;
    }

    int is_bgra = (ctx->swapchain_format == VK_FORMAT_B8G8R8A8_UNORM ||
                   ctx->swapchain_format == VK_FORMAT_B8G8R8A8_SRGB);

    FILE* fp = fopen(path, "wb");
    if (!fp) {
        vkUnmapMemory(ctx->device, staging.memory);
        vk_moo_buffer_destroy(ctx->device, &staging);
        return 0;
    }

    int row_bytes = w * 3;
    int row_pad = (4 - (row_bytes % 4)) % 4;
    int row_padded = row_bytes + row_pad;
    int data_size = row_padded * h;
    int file_size = 54 + data_size;

    unsigned char hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = file_size & 0xFF; hdr[3] = (file_size >> 8) & 0xFF;
    hdr[4] = (file_size >> 16) & 0xFF; hdr[5] = (file_size >> 24) & 0xFF;
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = w & 0xFF; hdr[19] = (w >> 8) & 0xFF;
    hdr[20] = (w >> 16) & 0xFF; hdr[21] = (w >> 24) & 0xFF;
    hdr[22] = h & 0xFF; hdr[23] = (h >> 8) & 0xFF;
    hdr[24] = (h >> 16) & 0xFF; hdr[25] = (h >> 24) & 0xFF;
    hdr[26] = 1;
    hdr[28] = 24;
    hdr[34] = data_size & 0xFF; hdr[35] = (data_size >> 8) & 0xFF;
    hdr[36] = (data_size >> 16) & 0xFF; hdr[37] = (data_size >> 24) & 0xFF;
    fwrite(hdr, 1, 54, fp);

    unsigned char* src_pixels = (unsigned char*)mapped;
    unsigned char* row = (unsigned char*)malloc(row_padded);
    if (!row) {
        fclose(fp);
        vkUnmapMemory(ctx->device, staging.memory);
        vk_moo_buffer_destroy(ctx->device, &staging);
        return 0;
    }
    memset(row, 0, row_padded);

    /* BMP ist bottom-up, Vulkan-Image ist top-down → Reihen umgekehrt schreiben. */
    for (int y = h - 1; y >= 0; y--) {
        unsigned char* sp = src_pixels + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            unsigned char r, g, b;
            if (is_bgra) {
                b = sp[0]; g = sp[1]; r = sp[2];
            } else {
                r = sp[0]; g = sp[1]; b = sp[2];
            }
            row[x*3 + 0] = b;
            row[x*3 + 1] = g;
            row[x*3 + 2] = r;
            sp += 4;
        }
        fwrite(row, 1, row_padded, fp);
    }

    free(row);
    fclose(fp);
    vkUnmapMemory(ctx->device, staging.memory);
    vk_moo_buffer_destroy(ctx->device, &staging);
    return 1;
}

Moo3DBackend moo_backend_vulkan = {
    .create_window = vk_create_window,
    .close = vk_close,
    .is_open = vk_is_open,
    .clear = vk_clear,
    .swap = vk_swap,
    .perspective = vk_perspective,
    .camera = vk_camera,
    .push_matrix = vk_push_matrix,
    .pop_matrix = vk_pop_matrix,
    .translate = vk_translate,
    .rotate = vk_rotate,
    .cube = vk_cube,
    .sphere = vk_sphere,
    .triangle = vk_triangle,
    .key_pressed = vk_key_pressed,
    .capture_mouse = vk_capture_mouse,
    .release_mouse = vk_release_mouse,
    .mouse_dx = vk_mouse_dx,
    .mouse_dy = vk_mouse_dy,
    .mouse_x = vk_mouse_x,
    .mouse_y = vk_mouse_y,
    .mouse_button = vk_mouse_button,
    .mouse_wheel = vk_mouse_wheel,
    .set_fog_density = vk_set_fog_density,
    .set_light_dir = vk_set_light_dir,
    .set_ambient = vk_set_ambient,
    .chunk_create = vk_chunk_create_fn,
    .chunk_begin = vk_chunk_begin_fn,
    .chunk_end = vk_chunk_end_fn,
    .chunk_draw = vk_chunk_draw_fn,
    .chunk_delete = vk_chunk_delete_fn,
    .screenshot_bmp = vk_screenshot_bmp,
    .simulate_mouse_pos = vk_simulate_mouse_pos,
    .simulate_mouse_button = vk_simulate_mouse_button,
    .simulate_scroll = vk_simulate_scroll,
};
