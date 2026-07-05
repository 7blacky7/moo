/**
 * moo_ki_gpu.c — GPU2 (Plan-014): Vulkan-Compute fuer GENAU 7 Ops:
 * matmul, add/sub/mul/div (elementweise, gleiche Shapes), sum/mean
 * (Voll-Reduktion). User-Go 2026-07-05, Scope-Grenze: KEIN Autograd/NN-
 * Umbau — die Hooks sitzen nur im Forward-Compute von moo_tensor_ops.c
 * und fallen bei JEDEM Fehler transparent auf den CPU-Pfad zurueck
 * (Rueckgabe false = "hab ich nicht gemacht").
 *
 * PoC-SCHNITT (bewusst, Backlog nach Review): host-visible|coherent
 * Buffers ohne Staging/Device-Local, Buffers pro Aufruf statt Pool,
 * ein Queue-Submit mit Fence pro Op. Korrektheit vor Durchsatz — der
 * Speedup kommt trotzdem, weil bei grossen Tensoren das Compute
 * dominiert. Reduktion: GPU liefert ein Partial pro 256er-Workgroup,
 * der Host summiert die Partials (double) — simpel + genau.
 *
 * DISPATCH-HEURISTIK (kleine Tensoren bleiben CPU):
 *   matmul: M*K*N >= 2^24 (ab ~256^3);  elementwise/reduce: n >= 2^20.
 * ENV: MOO_KI_GPU=0 schaltet alles ab; MOO_KI_GPU_ERZWINGEN=1 ignoriert
 * die Schwellen (fuer Korrektheits-Smoke mit kleinen Tensoren).
 *
 * Ohne MOO_HAS_VULKAN (Default-Build) ist alles hier ein Stub der false
 * liefert — moo_tensor_ops.c braucht dadurch KEIN #ifdef.
 */
#include "moo_runtime.h"

bool moo_ki_gpu_matmul(const float* a, const float* b, float* o,
                       int32_t m, int32_t k, int32_t n);
bool moo_ki_gpu_ew(int32_t op, const float* a, const float* b, float* o,
                   int64_t n);
bool moo_ki_gpu_reduce_sum(const float* a, int64_t n, double* out_summe);

#ifndef MOO_HAS_VULKAN

bool moo_ki_gpu_matmul(const float* a, const float* b, float* o,
                       int32_t m, int32_t k, int32_t n) {
    (void)a; (void)b; (void)o; (void)m; (void)k; (void)n;
    return false;
}
bool moo_ki_gpu_ew(int32_t op, const float* a, const float* b, float* o,
                   int64_t n) {
    (void)op; (void)a; (void)b; (void)o; (void)n;
    return false;
}
bool moo_ki_gpu_reduce_sum(const float* a, int64_t n, double* out_summe) {
    (void)a; (void)n; (void)out_summe;
    return false;
}

#else /* MOO_HAS_VULKAN ------------------------------------------------- */

#include <vulkan/vulkan.h>
#include "moo_ki_gpu_shaders.h"

typedef struct {
    bool init_versucht;
    bool bereit;
    VkInstance instanz;
    VkPhysicalDevice phys;
    VkDevice geraet;
    VkQueue queue;
    uint32_t queue_familie;
    uint32_t mem_typ_hostvis;
    uint32_t mem_typ_devlocal;        /* GPU3-A: VRAM-Arbeitsbuffers */
    bool devlocal_vereint;            /* true: devlocal==hostvis (UMA/iGPU/Fallback) -> keine Copies */
    VkDescriptorSetLayout dsl;        /* 3 Storage-Buffers */
    VkPipelineLayout playout;         /* + 16B Push-Constants */
    VkPipeline pipe_matmul, pipe_ew, pipe_reduce;
    VkCommandPool cmdpool;
    VkDescriptorPool descpool;
    /* GPU3-C: Per-Op-Allokationen gecacht — lazy beim ersten Dispatch
     * angelegt, leben bis Prozessende (wie Pipelines/Pools). Safe, weil
     * dispatch_sync synchron ist: Fence-Wait vor Return => GPU idle. */
    VkDescriptorSet ds_cache;
    VkCommandBuffer cb_cache;
    VkFence fence_cache;
} KiGpu;

static KiGpu G = {0};

typedef struct { uint32_t M, K, N, op; } KiPush;

static bool env_aus(void) {
    const char* e = getenv("MOO_KI_GPU");
    return e && e[0] == '0' && e[1] == '\0';
}
static bool env_erzwingen(void) {
    const char* e = getenv("MOO_KI_GPU_ERZWINGEN");
    return e && e[0] == '1' && e[1] == '\0';
}

static VkPipeline pipeline_bauen(const unsigned char* spv, unsigned int len) {
    VkShaderModuleCreateInfo smi = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = len,
        .pCode = (const uint32_t*)(const void*)spv,
    };
    VkShaderModule mod;
    if (vkCreateShaderModule(G.geraet, &smi, NULL, &mod) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    VkComputePipelineCreateInfo cpi = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = mod,
            .pName = "main",
        },
        .layout = G.playout,
    };
    VkPipeline p = VK_NULL_HANDLE;
    vkCreateComputePipelines(G.geraet, VK_NULL_HANDLE, 1, &cpi, NULL, &p);
    vkDestroyShaderModule(G.geraet, mod, NULL);
    return p;
}

/* Einmalige Initialisierung; bei jedem Fehler bleibt bereit=false und
 * ALLE Ops liefern fuer immer false (CPU uebernimmt). */
static bool ki_gpu_init(void) {
    if (G.init_versucht) return G.bereit;
    G.init_versucht = true;
    if (env_aus()) return false;

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "moo-ki",
        .apiVersion = VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };
    if (vkCreateInstance(&ici, NULL, &G.instanz) != VK_SUCCESS) return false;

    uint32_t anz = 0;
    vkEnumeratePhysicalDevices(G.instanz, &anz, NULL);
    if (anz == 0) return false;
    if (anz > 8) anz = 8;
    VkPhysicalDevice devs[8];
    vkEnumeratePhysicalDevices(G.instanz, &anz, devs);
    for (uint32_t d = 0; d < anz && !G.phys; d++) {
        uint32_t qanz = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &qanz, NULL);
        if (qanz > 16) qanz = 16;
        VkQueueFamilyProperties qf[16];
        vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &qanz, qf);
        for (uint32_t q = 0; q < qanz; q++) {
            if (qf[q].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                G.phys = devs[d];
                G.queue_familie = q;
                break;
            }
        }
    }
    if (!G.phys) return false;

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = G.queue_familie,
        .queueCount = 1,
        .pQueuePriorities = &prio,
    };
    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci,
    };
    if (vkCreateDevice(G.phys, &dci, NULL, &G.geraet) != VK_SUCCESS)
        return false;
    vkGetDeviceQueue(G.geraet, G.queue_familie, 0, &G.queue);

    /* Host-visible+coherent Memory-Typ suchen */
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(G.phys, &mp);
    G.mem_typ_hostvis = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        VkMemoryPropertyFlags will = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((mp.memoryTypes[i].propertyFlags & will) == will) {
            G.mem_typ_hostvis = i;
            break;
        }
    }
    if (G.mem_typ_hostvis == UINT32_MAX) return false;

    /* GPU3-A: DEVICE_LOCAL-Typ fuer VRAM-Arbeitsbuffers. Wenn der Typ auch
     * host-visible|coherent ist (UMA/iGPU) oder keiner existiert, arbeiten
     * wir "vereint" wie bisher (ein Buffer, keine Staging-Copies). */
    G.mem_typ_devlocal = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            G.mem_typ_devlocal = i;
            break;
        }
    }
    if (G.mem_typ_devlocal == UINT32_MAX) {
        G.mem_typ_devlocal = G.mem_typ_hostvis;
        G.devlocal_vereint = true;
    } else {
        VkMemoryPropertyFlags hv = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        G.devlocal_vereint =
            (mp.memoryTypes[G.mem_typ_devlocal].propertyFlags & hv) == hv;
        if (G.devlocal_vereint) G.mem_typ_devlocal = G.mem_typ_hostvis;
    }

    VkDescriptorSetLayoutBinding binds[3];
    for (int i = 0; i < 3; i++) {
        binds[i] = (VkDescriptorSetLayoutBinding){
            .binding = (uint32_t)i,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };
    }
    VkDescriptorSetLayoutCreateInfo dsli = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3,
        .pBindings = binds,
    };
    if (vkCreateDescriptorSetLayout(G.geraet, &dsli, NULL, &G.dsl) != VK_SUCCESS)
        return false;
    VkPushConstantRange pcr = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .size = sizeof(KiPush),
    };
    VkPipelineLayoutCreateInfo pli = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &G.dsl,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcr,
    };
    if (vkCreatePipelineLayout(G.geraet, &pli, NULL, &G.playout) != VK_SUCCESS)
        return false;

    G.pipe_matmul = pipeline_bauen(ki_matmul_spv, ki_matmul_spv_len);
    G.pipe_ew = pipeline_bauen(ki_elementwise_spv, ki_elementwise_spv_len);
    G.pipe_reduce = pipeline_bauen(ki_reduce_spv, ki_reduce_spv_len);
    if (!G.pipe_matmul || !G.pipe_ew || !G.pipe_reduce) return false;

    VkCommandPoolCreateInfo cpi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                 VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, /* GPU3-C: cb_cache-Reset */
        .queueFamilyIndex = G.queue_familie,
    };
    if (vkCreateCommandPool(G.geraet, &cpi, NULL, &G.cmdpool) != VK_SUCCESS)
        return false;
    VkDescriptorPoolSize dps = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 3,
    };
    VkDescriptorPoolCreateInfo dpi = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &dps,
    };
    if (vkCreateDescriptorPool(G.geraet, &dpi, NULL, &G.descpool) != VK_SUCCESS)
        return false;

    G.bereit = true;
    return true;
}

/* GPU3-A: Buffer-Paar. dev = DEVICE_LOCAL-Arbeitsbuffer (Shader-Zugriff,
 * VRAM), stg = host-visible Staging (permanent gemappt; map zeigt IMMER
 * hierauf — die Op-Funktionen memcpy-en unveraendert in/aus map).
 * Vereint-Modus (UMA/iGPU/Fallback): dev==stg, keine Copies noetig.
 * buf_anlegen/buf_weg sind bewusst die EINZIGE Alloc-Stelle — der
 * Buffer-Pool (Backlog #2) ersetzt spaeter genau diese zwei Funktionen. */
typedef struct {
    VkBuffer dev;  VkDeviceMemory dev_mem;
    VkBuffer stg;  VkDeviceMemory stg_mem;
    void* map;
    VkDeviceSize groesse;
    int pool_slot;   /* GPU3-B: >=0 = aus dem Pool geliehen, -1 = frisch */
} KiBuf;

static bool buf_einzeln(VkBuffer* buf, VkDeviceMemory* mem,
                        VkDeviceSize groesse, VkBufferUsageFlags usage,
                        uint32_t mem_typ, void** map_oder_null) {
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = groesse,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(G.geraet, &bci, NULL, buf) != VK_SUCCESS)
        return false;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(G.geraet, *buf, &mr);
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = mem_typ,
    };
    if (vkAllocateMemory(G.geraet, &mai, NULL, mem) != VK_SUCCESS)
        return false;
    if (vkBindBufferMemory(G.geraet, *buf, *mem, 0) != VK_SUCCESS)
        return false;
    if (map_oder_null &&
        vkMapMemory(G.geraet, *mem, 0, VK_WHOLE_SIZE, 0, map_oder_null) != VK_SUCCESS)
        return false;
    return true;
}

static bool buf_anlegen(KiBuf* b, VkDeviceSize groesse) {
    memset(b, 0, sizeof(*b));
    b->pool_slot = -1;
    b->groesse = groesse;
    if (G.devlocal_vereint) {
        /* Wie vor GPU3-A: ein host-visible Buffer, Shader liest direkt. */
        if (!buf_einzeln(&b->dev, &b->dev_mem, groesse,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         G.mem_typ_hostvis, &b->map))
            return false;
        b->stg = b->dev;
        b->stg_mem = b->dev_mem;
        return true;
    }
    /* Getrennt: dev in VRAM (ungemappt), stg als Transferfenster. */
    if (!buf_einzeln(&b->dev, &b->dev_mem, groesse,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     G.mem_typ_devlocal, NULL))
        return false;
    if (!buf_einzeln(&b->stg, &b->stg_mem, groesse,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     G.mem_typ_hostvis, &b->map))
        return false;
    return true;
}
static void buf_weg(KiBuf* b) {
    if (b->map) vkUnmapMemory(G.geraet, b->stg_mem);
    if (b->stg && b->stg != b->dev) vkDestroyBuffer(G.geraet, b->stg, NULL);
    if (b->stg_mem && b->stg_mem != b->dev_mem) vkFreeMemory(G.geraet, b->stg_mem, NULL);
    memset(b, 0, sizeof(*b));
    b->pool_slot = -1;
}

/* GPU3-B: Buffer-Pool. Kleiner statischer Cache fertiger KiBuf-Paare —
 * spart vkCreateBuffer/vkAllocateMemory/vkMapMemory pro Op (Trainingsloops
 * rufen viele gleichgrosse Ops). Best-Fit mit max 2x Verschnitt; Copies und
 * Shader nutzen b->groesse (angeforderte Groesse), nicht die Slot-Kapazitaet.
 * Kein Locking: der gesamte G-Singleton-Pfad ist single-threaded (bestehende
 * Konvention). Pool lebt bis Prozessende (wie G selbst — kein Teardown).
 * Nur VOLLSTAENDIG angelegte Buffers (map gesetzt) werden adoptiert. */
#define KI_POOL_MAX 12
typedef struct { KiBuf buf; VkDeviceSize kapazitaet; bool belegt; } KiPoolSlot;
static KiPoolSlot g_pool[KI_POOL_MAX];

static bool buf_holen(KiBuf* b, VkDeviceSize groesse) {
    int best = -1;
    for (int i = 0; i < KI_POOL_MAX; i++) {
        if (g_pool[i].belegt || g_pool[i].kapazitaet < groesse) continue;
        if (g_pool[i].kapazitaet > groesse * 2) continue;
        if (best < 0 || g_pool[i].kapazitaet < g_pool[best].kapazitaet) best = i;
    }
    if (best >= 0) {
        g_pool[best].belegt = true;
        *b = g_pool[best].buf;
        b->groesse = groesse;
        b->pool_slot = best;
        return true;
    }
    return buf_anlegen(b, groesse);
}

static void buf_zurueck(KiBuf* b) {
    if (!b->dev) return;
    if (b->pool_slot >= 0 && b->pool_slot < KI_POOL_MAX &&
        g_pool[b->pool_slot].belegt &&
        g_pool[b->pool_slot].buf.dev == b->dev) {
        g_pool[b->pool_slot].belegt = false;   /* Slot behaelt seinen KiBuf */
        memset(b, 0, sizeof(*b));
        b->pool_slot = -1;
        return;
    }
    if (b->map) {   /* vollstaendig angelegt -> adoptieren falls Platz */
        for (int i = 0; i < KI_POOL_MAX; i++) {
            if (g_pool[i].belegt || g_pool[i].kapazitaet != 0) continue;
            g_pool[i].buf = *b;
            g_pool[i].buf.pool_slot = i;
            g_pool[i].kapazitaet = b->groesse;
            g_pool[i].belegt = false;
            memset(b, 0, sizeof(*b));
            b->pool_slot = -1;
            return;
        }
    }
    buf_weg(b);   /* Pool voll oder Buffer unvollstaendig */
}

/* Gemeinsamer Ablauf: 3 Buffers sind angelegt, Staging der upload-Buffers
 * ist gefuellt. Im SELBEN Command-Buffer: Staging->VRAM-Copies (upload_mask),
 * Barrier, Compute-Dispatch, Barrier, VRAM->Staging-Copies (readback_mask).
 * Weiterhin genau EIN Submit pro Op; GPU3-C: DescSet/CmdBuf/Fence gecacht
 * statt pro Op allokiert (siehe Cache-Trio in KiGpu).
 * Im Vereint-Modus entfallen Copies+Barriers (dev==stg). */
static bool dispatch_sync(VkPipeline pipe, KiBuf b[3], KiPush push,
                          uint32_t gx, uint32_t gy,
                          uint32_t upload_mask, uint32_t readback_mask) {
    /* GPU3-C: DescSet/CmdBuf/Fence einmalig anlegen und wiederverwenden.
     * Fail => false (CPU-Fallback); Retry beim naechsten Aufruf ist
     * idempotent, weil die Handles VK_NULL_HANDLE bleiben. */
    if (G.ds_cache == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo dsa = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = G.descpool,
            .descriptorSetCount = 1,
            .pSetLayouts = &G.dsl,
        };
        if (vkAllocateDescriptorSets(G.geraet, &dsa, &G.ds_cache) != VK_SUCCESS) {
            G.ds_cache = VK_NULL_HANDLE;
            return false;
        }
    }
    if (G.cb_cache == VK_NULL_HANDLE) {
        VkCommandBufferAllocateInfo cba = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = G.cmdpool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (vkAllocateCommandBuffers(G.geraet, &cba, &G.cb_cache) != VK_SUCCESS) {
            G.cb_cache = VK_NULL_HANDLE;
            return false;
        }
    }
    if (G.fence_cache == VK_NULL_HANDLE) {
        VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        if (vkCreateFence(G.geraet, &fci, NULL, &G.fence_cache) != VK_SUCCESS) {
            G.fence_cache = VK_NULL_HANDLE;
            return false;
        }
    }
    VkDescriptorSet ds = G.ds_cache;
    VkDescriptorBufferInfo bi[3];
    VkWriteDescriptorSet ws[3];
    for (int i = 0; i < 3; i++) {
        bi[i] = (VkDescriptorBufferInfo){ .buffer = b[i].dev, .range = VK_WHOLE_SIZE };
        ws[i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ds,
            .dstBinding = (uint32_t)i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &bi[i],
        };
    }
    vkUpdateDescriptorSets(G.geraet, 3, ws, 0, NULL);

    VkCommandBuffer cb = G.cb_cache;
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo cbb = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cb, &cbb);
    bool kopieren = !G.devlocal_vereint;
    if (kopieren && upload_mask) {
        for (int i = 0; i < 3; i++) {
            if (!(upload_mask & (1u << i))) continue;
            VkBufferCopy reg = { .srcOffset = 0, .dstOffset = 0, .size = b[i].groesse };
            vkCmdCopyBuffer(cb, b[i].stg, b[i].dev, 1, &reg);
        }
        VkMemoryBarrier mb = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &mb, 0, NULL, 0, NULL);
    }
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, G.playout,
                            0, 1, &ds, 0, NULL);
    vkCmdPushConstants(cb, G.playout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(push), &push);
    vkCmdDispatch(cb, gx, gy, 1);
    if (kopieren && readback_mask) {
        VkMemoryBarrier mb = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 1, &mb, 0, NULL, 0, NULL);
        for (int i = 0; i < 3; i++) {
            if (!(readback_mask & (1u << i))) continue;
            VkBufferCopy reg = { .srcOffset = 0, .dstOffset = 0, .size = b[i].groesse };
            vkCmdCopyBuffer(cb, b[i].dev, b[i].stg, 1, &reg);
        }
    }
    vkEndCommandBuffer(cb);

    vkResetFences(G.geraet, 1, &G.fence_cache);
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cb,
    };
    bool ok = vkQueueSubmit(G.queue, 1, &si, G.fence_cache) == VK_SUCCESS &&
              vkWaitForFences(G.geraet, 1, &G.fence_cache, VK_TRUE,
                              30ull * 1000000000ull) == VK_SUCCESS;
    /* Fehlpfad (Timeout/Device-Lost): Queue leeren, damit der gecachte
     * Fence/CmdBuf beim naechsten Aufruf nicht in-flight resettet wird. */
    if (!ok) vkQueueWaitIdle(G.queue);
    return ok;
}

bool moo_ki_gpu_matmul(const float* a, const float* b, float* o,
                       int32_t m, int32_t k, int32_t n) {
    if (!env_erzwingen() &&
        (int64_t)m * k * n < (int64_t)1 << 24) return false;
    if (!ki_gpu_init()) return false;
    KiBuf bufs[3];
    bool ok = buf_holen(&bufs[0], (VkDeviceSize)m * k * 4) &&
              buf_holen(&bufs[1], (VkDeviceSize)k * n * 4) &&
              buf_holen(&bufs[2], (VkDeviceSize)m * n * 4);
    if (ok) {
        memcpy(bufs[0].map, a, (size_t)m * k * 4);
        memcpy(bufs[1].map, b, (size_t)k * n * 4);
        KiPush push = { (uint32_t)m, (uint32_t)k, (uint32_t)n, 0 };
        ok = dispatch_sync(G.pipe_matmul, bufs, push,
                           ((uint32_t)n + 15u) / 16u, ((uint32_t)m + 15u) / 16u,
                           /*upload*/ 0x3u, /*readback*/ 0x4u);
        if (ok) memcpy(o, bufs[2].map, (size_t)m * n * 4);
    }
    for (int i = 0; i < 3; i++) buf_zurueck(&bufs[i]);
    return ok;
}

bool moo_ki_gpu_ew(int32_t op, const float* a, const float* b, float* o,
                   int64_t n) {
    if (!env_erzwingen() && n < (int64_t)1 << 20) return false;
    if (n > (int64_t)1 << 30) return false;
    if (!ki_gpu_init()) return false;
    KiBuf bufs[3];
    bool ok = buf_holen(&bufs[0], (VkDeviceSize)n * 4) &&
              buf_holen(&bufs[1], (VkDeviceSize)n * 4) &&
              buf_holen(&bufs[2], (VkDeviceSize)n * 4);
    if (ok) {
        memcpy(bufs[0].map, a, (size_t)n * 4);
        memcpy(bufs[1].map, b, (size_t)n * 4);
        KiPush push = { (uint32_t)n, 0, 0, (uint32_t)op };
        ok = dispatch_sync(G.pipe_ew, bufs, push,
                           ((uint32_t)((n + 255) / 256)), 1,
                           /*upload*/ 0x3u, /*readback*/ 0x4u);
        if (ok) memcpy(o, bufs[2].map, (size_t)n * 4);
    }
    for (int i = 0; i < 3; i++) buf_zurueck(&bufs[i]);
    return ok;
}

bool moo_ki_gpu_reduce_sum(const float* a, int64_t n, double* out_summe) {
    if (!env_erzwingen() && n < (int64_t)1 << 20) return false;
    if (n > (int64_t)1 << 30) return false;
    if (!ki_gpu_init()) return false;
    int64_t gruppen = (n + 255) / 256;
    KiBuf bufs[3];
    /* Binding 2 wird vom Reduce-Shader nicht benutzt — Mini-Dummy, damit
     * das gemeinsame 3-Buffer-Layout gilt. */
    bool ok = buf_holen(&bufs[0], (VkDeviceSize)n * 4) &&
              buf_holen(&bufs[1], (VkDeviceSize)gruppen * 4) &&
              buf_holen(&bufs[2], 4);
    if (ok) {
        memcpy(bufs[0].map, a, (size_t)n * 4);
        KiPush push = { (uint32_t)n, 0, 0, 0 };
        ok = dispatch_sync(G.pipe_reduce, bufs, push, (uint32_t)gruppen, 1,
                           /*upload*/ 0x1u, /*readback*/ 0x2u);
        if (ok) {
            const float* teile = (const float*)bufs[1].map;
            double s = 0.0;
            for (int64_t i = 0; i < gruppen; i++) s += (double)teile[i];
            *out_summe = s;
        }
    }
    for (int i = 0; i < 3; i++) buf_zurueck(&bufs[i]);
    return ok;
}

#endif /* MOO_HAS_VULKAN */
