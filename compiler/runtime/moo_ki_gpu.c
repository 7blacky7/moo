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
#include "moo_ki_gpu_api.h"

bool moo_ki_gpu_matmul(const float* a, const float* b, float* o,
                       int32_t m, int32_t k, int32_t n);
bool moo_ki_gpu_ew(int32_t op, const float* a, const float* b, float* o,
                   int64_t n);
bool moo_ki_gpu_reduce_sum(const float* a, int64_t n, double* out_summe);

/* === KIP-G4c: STRIKT-Vertrag (docs/kip/G4c-production-wiring-plan.md §3.1) ===
 * Reiner Host-Zustand, unabhaengig von MOO_HAS_VULKAN (ohne Vulkan-Build wird
 * jeder GPU-Op-Aufruf trotzdem false liefern -> die Aufrufer werfen dann via
 * ihres eigenen "!done && strikt -> moo_throw"-Musters, fail-loud statt still
 * CPU. Env einmalig gelesen (lazy, wie ag_bf16 in moo_autograd.c), per
 * moo_ki_gpu_strikt_setzen ueberschreibbar (Tests/Gate-Skripte). */
static bool g_ki_strikt = false;
static bool g_ki_strikt_env_gelesen = false;
bool moo_ki_gpu_strikt_aktiv(void) {
    if (!g_ki_strikt_env_gelesen) {
        const char* e = getenv("MOO_KI_GPU_STRIKT");
        g_ki_strikt = (e && e[0] == '1' && e[1] == '\0');
        g_ki_strikt_env_gelesen = true;
    }
    return g_ki_strikt;
}
void moo_ki_gpu_strikt_setzen(bool an) {
    g_ki_strikt = an;
    g_ki_strikt_env_gelesen = true;
}

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

/* KIP-G1 residente API — Stubs (kein Vulkan): keine GPU-Residenz moeglich,
 * Aufrufer bleibt CPU-resident. */
void* moo_ki_gpu_buf_belegen(int64_t bytes) { (void)bytes; return NULL; }
void  moo_ki_gpu_buf_freigeben(void* handle) { (void)handle; }
bool  moo_ki_gpu_upload(void* handle, const float* src, int64_t bytes) {
    (void)handle; (void)src; (void)bytes; return false;
}
bool  moo_ki_gpu_download(void* handle, float* dst, int64_t bytes) {
    (void)handle; (void)dst; (void)bytes; return false;
}
bool moo_ki_gpu_matmul_res(void* a, void* b, void* o, int32_t m, int32_t k, int32_t n) {
    (void)a; (void)b; (void)o; (void)m; (void)k; (void)n; return false;
}
bool moo_ki_gpu_matmul_naiv_res(void* a, void* b, void* o, int32_t m, int32_t k, int32_t n) {
    (void)a; (void)b; (void)o; (void)m; (void)k; (void)n; return false;
}
bool moo_ki_gpu_ew_res(int32_t op, void* a, void* b, void* o, int64_t n) {
    (void)op; (void)a; (void)b; (void)o; (void)n; return false;
}
bool moo_ki_gpu_reduce_sum_res(void* a, int64_t n, double* out_summe) {
    (void)a; (void)n; (void)out_summe; return false;
}
bool moo_ki_gpu_unary_res(int32_t op, void* a, void* o, int64_t n, float skalar) {
    (void)op; (void)a; (void)o; (void)n; (void)skalar; return false;
}
bool moo_ki_gpu_unary_bw_res(int32_t op, void* src, void* gout, void* gin,
                             int64_t n, float skalar) {
    (void)op; (void)src; (void)gout; (void)gin; (void)n; (void)skalar; return false;
}
bool moo_ki_gpu_transpose_res(void* a, void* o, int32_t rows, int32_t cols) {
    (void)a; (void)o; (void)rows; (void)cols; return false;
}
bool moo_ki_gpu_copy_res(void* a, void* o, int64_t n, int64_t src_off, int64_t dst_off) {
    (void)a; (void)o; (void)n; (void)src_off; (void)dst_off; return false;
}
bool moo_ki_gpu_grad_accum_res(void* acc, void* g, int64_t n) {
    (void)acc; (void)g; (void)n; return false;
}
bool moo_ki_gpu_softmax_res(int32_t op, void* a, void* o, int32_t rows, int32_t cols) {
    (void)op; (void)a; (void)o; (void)rows; (void)cols; return false;
}
bool moo_ki_gpu_softmax_bw_res(int32_t op, void* y, void* g, void* gin,
                               int32_t rows, int32_t cols) {
    (void)op; (void)y; (void)g; (void)gin; (void)rows; (void)cols; return false;
}
bool moo_ki_gpu_ce_fwd_res(void* logits, void* target, int32_t rows, int32_t cols,
                           double* out_loss) {
    (void)logits; (void)target; (void)rows; (void)cols; (void)out_loss; return false;
}
bool moo_ki_gpu_ce_bw_res(void* logits, void* target, void* grad,
                          int32_t rows, int32_t cols, float scale) {
    (void)logits; (void)target; (void)grad; (void)rows; (void)cols; (void)scale; return false;
}
bool moo_ki_gpu_norm_res(int32_t op, void* x, void* o, int32_t rows, int32_t cols, float eps) {
    (void)op; (void)x; (void)o; (void)rows; (void)cols; (void)eps; return false;
}
bool moo_ki_gpu_norm_bw_res(int32_t op, void* x, void* g, void* dx,
                            int32_t rows, int32_t cols, float eps) {
    (void)op; (void)x; (void)g; (void)dx; (void)rows; (void)cols; (void)eps; return false;
}
bool moo_ki_gpu_reduce_axis_res(int32_t op, int32_t axis, void* a, void* o,
                                int32_t rows, int32_t cols) {
    (void)op; (void)axis; (void)a; (void)o; (void)rows; (void)cols; return false;
}
bool moo_ki_gpu_broadcast_res(int32_t axis, void* src, void* o,
                              int32_t rows, int32_t cols, float scale) {
    (void)axis; (void)src; (void)o; (void)rows; (void)cols; (void)scale; return false;
}
bool moo_ki_gpu_reduce_max_bw_res(int32_t axis, void* a, void* g, void* gin,
                                  int32_t rows, int32_t cols) {
    (void)axis; (void)a; (void)g; (void)gin; (void)rows; (void)cols; return false;
}
bool moo_ki_gpu_gather_res(void* w, void* idx, void* o,
                           int32_t rows, int32_t dim, int32_t vocab) {
    (void)w; (void)idx; (void)o; (void)rows; (void)dim; (void)vocab; return false;
}
bool moo_ki_gpu_scatter_add_res(void* g, void* idx, void* gw,
                                int32_t rows, int32_t dim, int32_t vocab) {
    (void)g; (void)idx; (void)gw; (void)rows; (void)dim; (void)vocab; return false;
}
bool moo_ki_gpu_rope_res(void* a, void* o, int32_t rows, int32_t dim,
                         int32_t head_dim, int32_t pos_offset, int fwd) {
    (void)a; (void)o; (void)rows; (void)dim; (void)head_dim; (void)pos_offset; (void)fwd; return false;
}
bool moo_ki_gpu_head_slice_res(void* a, void* o, int32_t rows, int32_t dim,
                               int32_t head_dim, int32_t col_offset, int extract) {
    (void)a; (void)o; (void)rows; (void)dim; (void)head_dim; (void)col_offset; (void)extract; return false;
}
bool moo_ki_gpu_matmul_bw_res(void* a, void* b, void* g, void* da, void* db,
                              int32_t m, int32_t k, int32_t n) {
    (void)a; (void)b; (void)g; (void)da; (void)db; (void)m; (void)k; (void)n; return false;
}
bool moo_ki_gpu_opt_sgd_res(void* p, void* m, void* grad, int64_t n,
                            float lr, float momentum) {
    (void)p; (void)m; (void)grad; (void)n; (void)lr; (void)momentum; return false;
}
bool moo_ki_gpu_opt_adam_res(void* p, void* grad, void* mv, int64_t n,
                             float lr, float beta1, float beta2, float eps,
                             float wd, int adamw, int64_t t) {
    (void)p; (void)grad; (void)mv; (void)n; (void)lr; (void)beta1; (void)beta2;
    (void)eps; (void)wd; (void)adamw; (void)t; return false;
}
bool moo_ki_gpu_opt_adam2_res(void* p, void* grad, void* m, void* v, int64_t n,
                              float lr, float beta1, float beta2, float eps,
                              float wd, int adamw, int64_t t) {
    (void)p; (void)grad; (void)m; (void)v; (void)n; (void)lr; (void)beta1; (void)beta2;
    (void)eps; (void)wd; (void)adamw; (void)t; return false;
}
void moo_ki_gpu_telemetrie(MooKiGpuTelemetrie* out) {
    if (out) { out->submits = 0; out->uploads = 0; out->downloads = 0; out->cpu_fallbacks = 0; }
}
void moo_ki_gpu_telemetrie_reset(void) { }

#else /* MOO_HAS_VULKAN ------------------------------------------------- */

#include <vulkan/vulkan.h>
#include <math.h>            /* KIP-G3c: powf/pow fuer Adam-Bias-Korrektur */
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
    VkPipelineLayout playout_opt;     /* KIP-G3c: + 36B Push-Constants (Optimizer-Ops), gleiches dsl */
    VkPipeline pipe_matmul, pipe_ew, pipe_reduce;
    VkPipeline pipe_matmul_naiv;      /* KIP-G2: nur A/B-Mikrobenchmark */
    VkPipeline pipe_unary, pipe_unary_bw;  /* KIP-G3d-a: unaer/Skalar/Aktivierung F+B */
    VkPipeline pipe_transpose, pipe_copy;  /* KIP-G3d-c: Layout-Ops transpose/reshape/concat */
    VkPipeline pipe_grad_accum;            /* KIP-G3c: gpu_grad += Beitrag (Fan-out) */
    VkPipeline pipe_opt_sgd, pipe_opt_adam;/* KIP-G3c: Optimizer-Schritt SGD-Momentum / Adam(W) */
    VkPipeline pipe_softmax, pipe_softmax_bw; /* KIP-G3a: Softmax/LogSoftmax F+B (zeilenweise) */
    VkPipeline pipe_ce_fwd, pipe_ce_bw;       /* KIP-G3a: fused Cross-Entropy F+B (zeilenweise) */
    VkPipeline pipe_norm, pipe_norm_bw;       /* KIP-G3b: LayerNorm/RMSNorm-Kern F+B (zeilenweise) */
    VkPipeline pipe_reduce_axis, pipe_broadcast, pipe_reduce_max_bw; /* KIP-G3d-b: Achsen-Red + Broadcast + Max-Subgradient */
    VkPipeline pipe_gather, pipe_scatter_add;  /* KIP-G3d-d: gather / deterministische scatter-add */
    VkPipeline pipe_rope, pipe_head_slice;     /* KIP-G4b: strided RoPE-Paarrotation / Kopf-Slice (MHA/GQA) */
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

/* KIP-G1 §5: Telemetrie. submits = Compute-Dispatches (in dispatch_sync
 * gezaehlt), uploads/downloads = explizite Transfers der residenten API. */
static MooKiGpuTelemetrie g_tel = {0};

typedef struct { uint32_t M, K, N, op; } KiPush;

/* KIP-G3c: erweitertes Push-Layout (36B) fuer Optimizer-Ops. Das 3-Buffer-
 * Descriptor-Layout bleibt (m+v teilen den mv-Buffer, G1 2) — nur die
 * Push-Range ist groesser, daher ein eigenes playout_opt mit demselben dsl.
 * Feld-Offsets muessen mit den PC-Bloecken in opt_sgd.comp/opt_adam.comp
 * uebereinstimmen (std430, tight-packed: n@0 flags@4 lr@8 p1@12 ... p6@32). */
typedef struct { uint32_t n, flags; float lr, p1, p2, p3, p4, p5, p6; } KiPushOpt;

static bool env_aus(void) {
    const char* e = getenv("MOO_KI_GPU");
    return e && e[0] == '0' && e[1] == '\0';
}
static bool env_erzwingen(void) {
    const char* e = getenv("MOO_KI_GPU_ERZWINGEN");
    if (e && e[0] == '1' && e[1] == '\0') return true;
    return moo_ki_gpu_strikt_aktiv();   /* KIP-G4c: STRIKT impliziert ERZWINGEN */
}

static VkPipeline pipeline_bauen(const unsigned char* spv, unsigned int len,
                                 VkPipelineLayout layout) {
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
        .layout = layout,
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

    /* KIP-G3c: zweites Pipeline-Layout — gleiches 3-Buffer-dsl, aber groessere
     * Push-Range (KiPushOpt, 36B) fuer die Optimizer-Ops. */
    VkPushConstantRange pcr_opt = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .size = sizeof(KiPushOpt),
    };
    VkPipelineLayoutCreateInfo pli_opt = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &G.dsl,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcr_opt,
    };
    if (vkCreatePipelineLayout(G.geraet, &pli_opt, NULL, &G.playout_opt) != VK_SUCCESS)
        return false;

    G.pipe_matmul = pipeline_bauen(ki_matmul_spv, ki_matmul_spv_len, G.playout);
    G.pipe_ew = pipeline_bauen(ki_elementwise_spv, ki_elementwise_spv_len, G.playout);
    G.pipe_reduce = pipeline_bauen(ki_reduce_spv, ki_reduce_spv_len, G.playout);
    G.pipe_matmul_naiv = pipeline_bauen(ki_matmul_naiv_spv, ki_matmul_naiv_spv_len, G.playout);
    G.pipe_unary = pipeline_bauen(ki_unary_fwd_spv, ki_unary_fwd_spv_len, G.playout);
    G.pipe_unary_bw = pipeline_bauen(ki_unary_bw_spv, ki_unary_bw_spv_len, G.playout);
    G.pipe_transpose = pipeline_bauen(ki_transpose_spv, ki_transpose_spv_len, G.playout);
    G.pipe_copy = pipeline_bauen(ki_copy_spv, ki_copy_spv_len, G.playout);
    /* KIP-G3c: grad_accum nutzt das 16B-playout, die Opt-Schritte das 36B-playout_opt. */
    G.pipe_grad_accum = pipeline_bauen(ki_grad_accum_spv, ki_grad_accum_spv_len, G.playout);
    G.pipe_opt_sgd = pipeline_bauen(ki_opt_sgd_spv, ki_opt_sgd_spv_len, G.playout_opt);
    G.pipe_opt_adam = pipeline_bauen(ki_opt_adam_spv, ki_opt_adam_spv_len, G.playout_opt);
    /* KIP-G3a: Softmax/LogSoftmax + fused CE — alle 16B-playout, ein Thread/Zeile. */
    G.pipe_softmax = pipeline_bauen(ki_softmax_fwd_spv, ki_softmax_fwd_spv_len, G.playout);
    G.pipe_softmax_bw = pipeline_bauen(ki_softmax_bw_spv, ki_softmax_bw_spv_len, G.playout);
    G.pipe_ce_fwd = pipeline_bauen(ki_ce_fwd_spv, ki_ce_fwd_spv_len, G.playout);
    G.pipe_ce_bw = pipeline_bauen(ki_ce_bw_spv, ki_ce_bw_spv_len, G.playout);
    /* KIP-G3b: LayerNorm/RMSNorm-Kern (16B-playout, ein Thread/Zeile). */
    G.pipe_norm = pipeline_bauen(ki_norm_fwd_spv, ki_norm_fwd_spv_len, G.playout);
    G.pipe_norm_bw = pipeline_bauen(ki_norm_bw_spv, ki_norm_bw_spv_len, G.playout);
    /* KIP-G3d-b: Achsen-Reduktion + Broadcast + Max-Subgradient (16B-playout). */
    G.pipe_reduce_axis = pipeline_bauen(ki_reduce_axis_spv, ki_reduce_axis_spv_len, G.playout);
    G.pipe_broadcast = pipeline_bauen(ki_broadcast_spv, ki_broadcast_spv_len, G.playout);
    G.pipe_reduce_max_bw = pipeline_bauen(ki_reduce_max_bw_spv, ki_reduce_max_bw_spv_len, G.playout);
    /* KIP-G3d-d: gather + deterministische scatter-add (16B-playout). */
    G.pipe_gather = pipeline_bauen(ki_gather_spv, ki_gather_spv_len, G.playout);
    G.pipe_scatter_add = pipeline_bauen(ki_scatter_add_spv, ki_scatter_add_spv_len, G.playout);
    /* KIP-G4b: strided RoPE + Kopf-Slice (16B-playout, dispatch_sync). */
    G.pipe_rope = pipeline_bauen(ki_rope_spv, ki_rope_spv_len, G.playout);
    G.pipe_head_slice = pipeline_bauen(ki_head_slice_spv, ki_head_slice_spv_len, G.playout);
    if (!G.pipe_matmul || !G.pipe_ew || !G.pipe_reduce || !G.pipe_matmul_naiv ||
        !G.pipe_unary || !G.pipe_unary_bw || !G.pipe_transpose || !G.pipe_copy ||
        !G.pipe_grad_accum || !G.pipe_opt_sgd || !G.pipe_opt_adam ||
        !G.pipe_softmax || !G.pipe_softmax_bw || !G.pipe_ce_fwd || !G.pipe_ce_bw ||
        !G.pipe_norm || !G.pipe_norm_bw ||
        !G.pipe_reduce_axis || !G.pipe_broadcast || !G.pipe_reduce_max_bw ||
        !G.pipe_gather || !G.pipe_scatter_add ||
        !G.pipe_rope || !G.pipe_head_slice)
        return false;

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

/* KIP-G3c: DescSet/CmdBuf/Fence-Cache-Trio lazy anlegen (GPU3-C). Geteilt von
 * dispatch_sync (16B-Push) und dispatch_opt (36B-Push) — beide binden dieselbe
 * ds_cache (aus G.dsl alloziert), der synchrone Ablauf schliesst Overlap aus.
 * Fail => false (CPU-Fallback); Retry idempotent (Handles bleiben NULL). */
static bool ensure_dispatch_cache(void) {
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
    return true;
}

/* KIP-G3c: Optimizer-Dispatch — immer resident (kein Upload/Readback im
 * Command-Buffer), nutzt playout_opt (36B-Push). Ein Start-Barrier macht die
 * Writes vorheriger Ops/Uploads in p/m/v/g sichtbar (wie resident-Zweig in
 * dispatch_sync). Genau 1 Submit pro Schritt (submits++). */
static bool dispatch_opt(VkPipeline pipe, KiBuf b[3], KiPushOpt push, uint32_t gx) {
    if (!ensure_dispatch_cache()) return false;
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
    VkMemoryBarrier mb = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
    };
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &mb, 0, NULL, 0, NULL);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, G.playout_opt,
                            0, 1, &ds, 0, NULL);
    vkCmdPushConstants(cb, G.playout_opt, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(push), &push);
    vkCmdDispatch(cb, gx, 1, 1);
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
    if (!ok) vkQueueWaitIdle(G.queue);
    if (ok) g_tel.submits++;
    return ok;
}

/* Gemeinsamer Ablauf: 3 Buffers sind angelegt, Staging der upload-Buffers
 * ist gefuellt. Im SELBEN Command-Buffer: Staging->VRAM-Copies (upload_mask),
 * Barrier, Compute-Dispatch, Barrier, VRAM->Staging-Copies (readback_mask).
 * Weiterhin genau EIN Submit pro Op; GPU3-C: DescSet/CmdBuf/Fence gecacht
 * statt pro Op allokiert (siehe Cache-Trio in KiGpu).
 * Im Vereint-Modus entfallen Copies+Barriers (dev==stg). */
static bool dispatch_sync(VkPipeline pipe, KiBuf b[3], KiPush push,
                          uint32_t gx, uint32_t gy,
                          uint32_t upload_mask, uint32_t readback_mask,
                          bool resident) {
    if (!ensure_dispatch_cache()) return false;
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
    /* KIP-G1: Residente Op liest Buffers, die eine vorherige Op (anderer
     * Submit, gleiche Queue) oder ein Upload beschrieben hat. Der Fence-Wait
     * zwischen den Ops garantiert Ausfuehrungs-Reihenfolge, aber NICHT die
     * Memory-Availability/Visibility — dafuer dieser Barrier am Dispatch-
     * Anfang. Deckt als 2. Sync-Scope die frueher submittierten Writes ab. */
    if (resident) {
        VkMemoryBarrier mb = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        };
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &mb, 0, NULL, 0, NULL);
    }
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
    if (ok) g_tel.submits++;   /* KIP-G1 §5: genau 1 Compute-Submit pro Op */
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
                           /*upload*/ 0x3u, /*readback*/ 0x4u, /*resident*/ false);
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
                           /*upload*/ 0x3u, /*readback*/ 0x4u, /*resident*/ false);
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
                           /*upload*/ 0x1u, /*readback*/ 0x2u, /*resident*/ false);
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

/* ==================================================================== *
 * KIP-G1: Residente Buffer-API + Telemetrie                            *
 * GPU-residente Tensoren halten ihren Pool-Buffer ueber mehrere Ops;   *
 * zwischen den Ops findet KEIN Host<->Device-Transfer statt (per        *
 * Submit-/Transfer-Zaehler beweisbar). Fundament fuer das G4-Gate.     *
 * ==================================================================== */

/* Lazy-Alloc des gecachten CmdBuf+Fence (geteilt mit dispatch_sync; alle
 * Pfade synchron + single-threaded => Wiederverwendung sicher). Guard auf
 * VK_NULL_HANDLE => egal ob dispatch_sync oder transfer_submit zuerst laeuft. */
static bool ensure_cb_fence(void) {
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
    return true;
}

/* Copy-only-Submit fuer den Staging<->VRAM-Transfer EINES Buffers (diskrete
 * GPU). Vereint-Modus (map == VRAM, UMA/Fallback): kein Copy noetig. */
static bool transfer_submit(KiBuf* kb, bool upload) {
    if (G.devlocal_vereint) return true;        /* map zeigt direkt auf VRAM */
    if (!ensure_cb_fence()) return false;
    VkCommandBuffer cb = G.cb_cache;
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo cbb = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cb, &cbb);
    VkBufferCopy reg = { .srcOffset = 0, .dstOffset = 0, .size = kb->groesse };
    if (upload) {
        /* Host hat kb->stg (coherent) beschrieben -> Copy stg->dev, dann
         * Barrier TRANSFER_WRITE->SHADER_READ, damit die naechste residente
         * Compute-Op (spaeterer Submit, gleiche Queue) die Daten sieht. */
        vkCmdCopyBuffer(cb, kb->stg, kb->dev, 1, &reg);
        VkMemoryBarrier mb = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &mb, 0, NULL, 0, NULL);
    } else {
        /* dev wurde von einer frueheren Compute-Op beschrieben -> Barrier
         * SHADER_WRITE->TRANSFER_READ, dann Copy dev->stg fuer den Host. */
        VkMemoryBarrier mb = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        };
        vkCmdPipelineBarrier(cb,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 1, &mb, 0, NULL, 0, NULL);
        vkCmdCopyBuffer(cb, kb->dev, kb->stg, 1, &reg);
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
    if (!ok) vkQueueWaitIdle(G.queue);
    return ok;
}

void* moo_ki_gpu_buf_belegen(int64_t bytes) {
    if (bytes <= 0) return NULL;
    if (!ki_gpu_init()) return NULL;
    KiBuf* kb = (KiBuf*)malloc(sizeof(KiBuf));
    if (!kb) return NULL;
    if (!buf_holen(kb, (VkDeviceSize)bytes)) { free(kb); return NULL; }
    return kb;
}

void moo_ki_gpu_buf_freigeben(void* handle) {
    if (!handle) return;
    KiBuf* kb = (KiBuf*)handle;
    /* Synchroner Dispatch => GPU idle bei Rueckgabe (Fence-Wait vor jedem
     * Op-Return). Daher kein separater In-flight-Fence-Wait noetig; der Slot
     * wird von buf_zurueck erst NACH diesem Punkt neu vergeben, d.h. der
     * Handle kann nicht auf einen fremden in-flight-Buffer zeigen (G1 §2
     * Slot-Reuse-Sicherheit; Async-Pfad braucht spaeter eine Submit-Map). */
    buf_zurueck(kb);
    free(kb);
}

bool moo_ki_gpu_upload(void* handle, const float* src, int64_t bytes) {
    if (!handle || !src || bytes <= 0) return false;
    KiBuf* kb = (KiBuf*)handle;
    if ((VkDeviceSize)bytes > kb->groesse) return false;
    memcpy(kb->map, src, (size_t)bytes);
    if (!transfer_submit(kb, /*upload*/ true)) return false;
    g_tel.uploads++;
    return true;
}

bool moo_ki_gpu_download(void* handle, float* dst, int64_t bytes) {
    if (!handle || !dst || bytes <= 0) return false;
    KiBuf* kb = (KiBuf*)handle;
    if ((VkDeviceSize)bytes > kb->groesse) return false;
    if (!transfer_submit(kb, /*upload*/ false)) return false;
    memcpy(dst, kb->map, (size_t)bytes);
    g_tel.downloads++;
    return true;
}

bool moo_ki_gpu_matmul_res(void* a, void* b, void* o,
                           int32_t m, int32_t k, int32_t n) {
    if (!a || !b || !o) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    KiBuf bufs[3] = { *(KiBuf*)a, *(KiBuf*)b, *(KiBuf*)o };
    KiPush push = { (uint32_t)m, (uint32_t)k, (uint32_t)n, 0 };
    bool ok = dispatch_sync(G.pipe_matmul, bufs, push,
                            ((uint32_t)n + 15u) / 16u, ((uint32_t)m + 15u) / 16u,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G2: naive residente Matmul — NUR fuer den A/B-Mikrobenchmark (alt vs
 * neu). Identisch zu matmul_res bis auf die Pipeline. Nicht im Produktivpfad. */
bool moo_ki_gpu_matmul_naiv_res(void* a, void* b, void* o,
                                int32_t m, int32_t k, int32_t n) {
    if (!a || !b || !o) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    KiBuf bufs[3] = { *(KiBuf*)a, *(KiBuf*)b, *(KiBuf*)o };
    KiPush push = { (uint32_t)m, (uint32_t)k, (uint32_t)n, 0 };
    bool ok = dispatch_sync(G.pipe_matmul_naiv, bufs, push,
                            ((uint32_t)n + 15u) / 16u, ((uint32_t)m + 15u) / 16u,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

bool moo_ki_gpu_ew_res(int32_t op, void* a, void* b, void* o, int64_t n) {
    if (!a || !b || !o) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    KiBuf bufs[3] = { *(KiBuf*)a, *(KiBuf*)b, *(KiBuf*)o };
    KiPush push = { (uint32_t)n, 0, 0, (uint32_t)op };
    bool ok = dispatch_sync(G.pipe_ew, bufs, push,
                            (uint32_t)((n + 255) / 256), 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

bool moo_ki_gpu_reduce_sum_res(void* a, int64_t n, double* out_summe) {
    if (!a || !out_summe) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    int64_t gruppen = (n + 255) / 256;
    /* Eingabe a ist bereits resident. Partials (b[1]) + Dummy (b[2]) leihweise
     * aus dem Pool; readback_mask 0x2 faltet den Partial-Download in den
     * Compute-Submit (Reduktion verlaesst die GPU inhaerent). */
    KiBuf partials, dummy;
    if (!buf_holen(&partials, (VkDeviceSize)gruppen * 4)) {
        g_tel.cpu_fallbacks++; return false;
    }
    if (!buf_holen(&dummy, 4)) {
        buf_zurueck(&partials); g_tel.cpu_fallbacks++; return false;
    }
    KiBuf bufs[3] = { *(KiBuf*)a, partials, dummy };
    KiPush push = { (uint32_t)n, 0, 0, 0 };
    bool ok = dispatch_sync(G.pipe_reduce, bufs, push, (uint32_t)gruppen, 1,
                            /*upload*/ 0u, /*readback*/ 0x2u, /*resident*/ true);
    if (ok) {
        const float* teile = (const float*)partials.map;
        double s = 0.0;
        for (int64_t i = 0; i < gruppen; i++) s += (double)teile[i];
        *out_summe = s;
        g_tel.downloads++;              /* Partial-Readback = Device->Host */
    } else {
        g_tel.cpu_fallbacks++;
    }
    buf_zurueck(&partials);
    buf_zurueck(&dummy);
    return ok;
}

/* KIP-G3d-a: unaere/Skalar/Aktivierungs-Ops auf residenten Buffers.
 * Skalar wird als float-Bitmuster im KiPush.K-Feld transportiert (der Shader
 * liest ihn via uintBitsToFloat) — das haelt Push-Constant (16B) und das
 * 3-Buffer-Descriptor-Layout unveraendert. Forward bindet den Eingang doppelt
 * (binding 0+1), binding 1 ist im Shader ungenutzt. */
bool moo_ki_gpu_unary_res(int32_t op, void* a, void* o, int64_t n, float skalar) {
    if (!a || !o) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    uint32_t sbits; memcpy(&sbits, &skalar, sizeof(sbits));
    KiBuf bufs[3] = { *(KiBuf*)a, *(KiBuf*)a, *(KiBuf*)o };
    KiPush push = { (uint32_t)n, sbits, 0, (uint32_t)op };
    bool ok = dispatch_sync(G.pipe_unary, bufs, push,
                            (uint32_t)((n + 255) / 256), 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3d-a: Backward — gin = gout * f'(src). src = x oder y (Aufrufer-Vertrag,
 * moo_ki_gpu_api.h). Reiner Gradient-Beitrag ohne Akkumulation (G3c). */
bool moo_ki_gpu_unary_bw_res(int32_t op, void* src, void* gout, void* gin,
                             int64_t n, float skalar) {
    if (!src || !gout || !gin) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    uint32_t sbits; memcpy(&sbits, &skalar, sizeof(sbits));
    KiBuf bufs[3] = { *(KiBuf*)src, *(KiBuf*)gout, *(KiBuf*)gin };
    KiPush push = { (uint32_t)n, sbits, 0, (uint32_t)op };
    bool ok = dispatch_sync(G.pipe_unary_bw, bufs, push,
                            (uint32_t)((n + 255) / 256), 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3d-c: 2D-Transponierung auf residentem Buffer. Fwd: a[rows,cols] ->
 * o[cols,rows]. Bwd: Aufrufer ruft mit gout + vertauschten Dims (cols,rows). */
bool moo_ki_gpu_transpose_res(void* a, void* o, int32_t rows, int32_t cols) {
    if (!a || !o) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    KiBuf bufs[3] = { *(KiBuf*)a, *(KiBuf*)a, *(KiBuf*)o };
    KiPush push = { (uint32_t)rows, (uint32_t)cols, 0, 0 };
    bool ok = dispatch_sync(G.pipe_transpose, bufs, push,
                            ((uint32_t)cols + 15u) / 16u, ((uint32_t)rows + 15u) / 16u,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3d-c: Kopie o[dst_off+i]=a[src_off+i]. Deckt reshape (Offsets 0),
 * concat-Forward (dst_off) und concat-Split/Backward (src_off) ab. Offsets in
 * Elementen; Push M=n, K=src_off, N=dst_off (uint). */
bool moo_ki_gpu_copy_res(void* a, void* o, int64_t n, int64_t src_off, int64_t dst_off) {
    if (!a || !o) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    KiBuf bufs[3] = { *(KiBuf*)a, *(KiBuf*)a, *(KiBuf*)o };
    KiPush push = { (uint32_t)n, (uint32_t)src_off, (uint32_t)dst_off, 0 };
    bool ok = dispatch_sync(G.pipe_copy, bufs, push,
                            (uint32_t)((n + 255) / 256), 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G4b: strided interleaved RoPE-Paarrotation auf residentem Buffer. a=[rows,
 * dim]; ein Thread pro Paar (rows*dim/2). In-Head-Paarindex = (col%head_dim)/2 ->
 * Multi-Head/GQA korrekt. Push: M=Paarzahl, K=head_dim, N=dim, op=(pos_offset<<1)
 * |dir mit dir 0=Fwd(+angle)/1=Bwd(-angle). REINE Rotation (kein +=). */
bool moo_ki_gpu_rope_res(void* a, void* o, int32_t rows, int32_t dim,
                         int32_t head_dim, int32_t pos_offset, int fwd) {
    if (!a || !o) { g_tel.cpu_fallbacks++; return false; }
    if (rows <= 0 || dim <= 0 || head_dim <= 0 || (dim & 1) || (head_dim & 1) ||
        (dim % head_dim) != 0 || pos_offset < 0) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    int64_t paare = (int64_t)rows * dim / 2;
    uint32_t dir = fwd ? 0u : 1u;
    KiBuf bufs[3] = { *(KiBuf*)a, *(KiBuf*)a, *(KiBuf*)o };
    KiPush push = { (uint32_t)paare, (uint32_t)head_dim, (uint32_t)dim,
                    ((uint32_t)pos_offset << 1) | dir };
    bool ok = dispatch_sync(G.pipe_rope, bufs, push,
                            (uint32_t)((paare + 255) / 256), 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G4b: strided Kopf-Slice fuer Multi-Head/GQA. extract!=0: o[rows,head_dim]
 * aus a[rows,dim]-Spalten [col_offset, col_offset+head_dim). extract==0: Merge
 * a[rows,head_dim] in o[rows,dim] an dieselben Spalten (reiner Write; disjunkte
 * Koepfe). Push: M=rows*head_dim, K=dim, N=(head_dim<<16)|col_offset. */
bool moo_ki_gpu_head_slice_res(void* a, void* o, int32_t rows, int32_t dim,
                               int32_t head_dim, int32_t col_offset, int extract) {
    if (!a || !o) { g_tel.cpu_fallbacks++; return false; }
    if (rows <= 0 || dim <= 0 || head_dim <= 0 || col_offset < 0 ||
        col_offset + head_dim > dim || head_dim > 0xFFFF || col_offset > 0xFFFF) {
        g_tel.cpu_fallbacks++; return false;
    }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    int64_t m = (int64_t)rows * head_dim;
    KiBuf bufs[3] = { *(KiBuf*)a, *(KiBuf*)a, *(KiBuf*)o };
    KiPush push = { (uint32_t)m, (uint32_t)dim,
                    ((uint32_t)head_dim << 16) | (uint32_t)col_offset,
                    extract ? 0u : 1u };
    bool ok = dispatch_sync(G.pipe_head_slice, bufs, push,
                            (uint32_t)((m + 255) / 256), 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3c: Gradient-Akkumulation acc[i] += g[i] auf residenten Buffers. Loest
 * das += ein, das die Bwd-Ops (G3d-a/-c) bewusst weggelassen haben. acc wird
 * an binding 0 (rw) UND binding 2 (ungenutzt) gebunden; g an binding 1. */
bool moo_ki_gpu_grad_accum_res(void* acc, void* g, int64_t n) {
    if (!acc || !g) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    KiBuf bufs[3] = { *(KiBuf*)acc, *(KiBuf*)g, *(KiBuf*)acc };
    KiPush push = { (uint32_t)n, 0, 0, 0 };
    bool ok = dispatch_sync(G.pipe_grad_accum, bufs, push,
                            (uint32_t)((n + 255) / 256), 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3c: SGD-mit-Momentum-Schritt (in-place). p, m read+write, grad read.
 * m := mu*m + grad; p := p - lr*m. Momentum-Zustand m bleibt resident (E2b
 * laedt ihn per moo_ki_gpu_download herunter). */
bool moo_ki_gpu_opt_sgd_res(void* p, void* m, void* grad, int64_t n,
                            float lr, float momentum) {
    if (!p || !m || !grad) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    KiBuf bufs[3] = { *(KiBuf*)p, *(KiBuf*)m, *(KiBuf*)grad };
    KiPushOpt push = { (uint32_t)n, 0u, lr, momentum, 0.f, 0.f, 0.f, 0.f, 0.f };
    bool ok = dispatch_opt(G.pipe_opt_sgd, bufs, push, (uint32_t)((n + 255) / 256));
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3c: Adam/AdamW-Schritt (in-place). p, mv read+write, grad read; mv ist
 * EIN residenter Buffer der Groesse 2n (m in [0,n), v in [n,2n) — G1 2, keine
 * getrennte 4. Bindung). adamw!=0 -> decoupled weight decay. Bias-Korrektur
 * bc1/bc2 host-seitig aus t (1-basiert, wie moo_nn.c). t>=1 erwartet. */
bool moo_ki_gpu_opt_adam_res(void* p, void* grad, void* mv, int64_t n,
                             float lr, float beta1, float beta2, float eps,
                             float wd, int adamw, int64_t t) {
    if (!p || !grad || !mv) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    float bc1 = (float)(1.0 - pow((double)beta1, (double)t));
    float bc2 = (float)(1.0 - pow((double)beta2, (double)t));
    KiPushOpt push = { (uint32_t)n, adamw ? 1u : 0u, lr, beta1, beta2,
                       eps, wd, bc1, bc2 };
    KiBuf bufs[3] = { *(KiBuf*)p, *(KiBuf*)mv, *(KiBuf*)grad };
    bool ok = dispatch_opt(G.pipe_opt_adam, bufs, push, (uint32_t)((n + 255) / 256));
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G4c: Adam/AdamW mit ZWEI getrennten Handles m/v (statt gepacktem mv,
 * siehe Kommentar in moo_ki_gpu_api.h). Komposition aus bestehenden 3-Buffer-
 * Ops (unary_res Skalar, ew_res Tensor-Tensor) -- das Descriptor-Set-Layout
 * ist hart auf 3 Bindings begrenzt, daher kein einzelner 4-Buffer-Kernel.
 * 2 Temp-Buffer aus dem Pool. Jeder Zwischenschritt-Fehlschlag -> sauberer
 * Komplett-Abbruch (false), Aufrufer faellt komplett auf CPU zurueck. */
bool moo_ki_gpu_opt_adam2_res(void* p, void* grad, void* m, void* v, int64_t n,
                              float lr, float beta1, float beta2, float eps,
                              float wd, int adamw, int64_t t) {
    if (!p || !grad || !m || !v) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    KiBuf t1, t2;
    if (!buf_holen(&t1, (VkDeviceSize)n * 4)) { g_tel.cpu_fallbacks++; return false; }
    if (!buf_holen(&t2, (VkDeviceSize)n * 4)) {
        buf_zurueck(&t1); g_tel.cpu_fallbacks++; return false;
    }
    float bc1 = (float)(1.0 - pow((double)beta1, (double)t));
    float bc2 = (float)(1.0 - pow((double)beta2, (double)t));

    /* ew_res-Op-Codes (moo_tensor_ops.c ew_op): 0=add 1=sub 2=mul 3=div */
    enum { EW_ADD = 0, EW_SUB = 1, EW_MUL = 2, EW_DIV = 3 };

    bool ok = true;
    if (adamw) ok = ok && moo_ki_gpu_unary_res(MOO_KI_U_MULS, p, p, n, 1.0f - lr * wd);
    /* m = beta1*m + (1-beta1)*grad */
    ok = ok && moo_ki_gpu_unary_res(MOO_KI_U_MULS, m, &t1, n, beta1);
    ok = ok && moo_ki_gpu_unary_res(MOO_KI_U_MULS, grad, &t2, n, 1.0f - beta1);
    ok = ok && moo_ki_gpu_ew_res(EW_ADD, &t1, &t2, m, n);
    /* v = beta2*v + (1-beta2)*grad*grad */
    ok = ok && moo_ki_gpu_ew_res(EW_MUL, grad, grad, &t1, n);
    ok = ok && moo_ki_gpu_unary_res(MOO_KI_U_MULS, &t1, &t1, n, 1.0f - beta2);
    ok = ok && moo_ki_gpu_unary_res(MOO_KI_U_MULS, v, &t2, n, beta2);
    ok = ok && moo_ki_gpu_ew_res(EW_ADD, &t1, &t2, v, n);
    /* mhat=m/bc1 -> t1 ; denom=sqrt(v/bc2)+eps -> t2 */
    ok = ok && moo_ki_gpu_unary_res(MOO_KI_U_MULS, m, &t1, n, 1.0f / bc1);
    ok = ok && moo_ki_gpu_unary_res(MOO_KI_U_MULS, v, &t2, n, 1.0f / bc2);
    ok = ok && moo_ki_gpu_unary_res(MOO_KI_U_SQRT, &t2, &t2, n, 0.0f);
    ok = ok && moo_ki_gpu_unary_res(MOO_KI_U_ADDS, &t2, &t2, n, eps);
    /* p -= lr * mhat/denom */
    ok = ok && moo_ki_gpu_ew_res(EW_DIV, &t1, &t2, &t1, n);
    ok = ok && moo_ki_gpu_unary_res(MOO_KI_U_MULS, &t1, &t1, n, lr);
    ok = ok && moo_ki_gpu_ew_res(EW_SUB, p, &t1, p, n);

    buf_zurueck(&t1);
    buf_zurueck(&t2);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3a: zeilenweise Softmax (op 0) / LogSoftmax (op 1), max-shift-stabil.
 * Ein Thread pro Zeile. a wird an binding 0 UND 1 (ungenutzt) gebunden. */
bool moo_ki_gpu_softmax_res(int32_t op, void* a, void* o, int32_t rows, int32_t cols) {
    if (!a || !o) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    KiBuf bufs[3] = { *(KiBuf*)a, *(KiBuf*)a, *(KiBuf*)o };
    KiPush push = { (uint32_t)rows, (uint32_t)cols, 0, (uint32_t)op };
    bool ok = dispatch_sync(G.pipe_softmax, bufs, push,
                            ((uint32_t)rows + 255u) / 256u, 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3a: zugehoeriger Backward. op 0: da = y*(g - sum(g*y)); op 1: da =
 * g - exp(y)*sum(g). y = Forward-Ausgang, g = grad_out. REINER Beitrag (kein +=). */
bool moo_ki_gpu_softmax_bw_res(int32_t op, void* y, void* g, void* gin,
                               int32_t rows, int32_t cols) {
    if (!y || !g || !gin) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    KiBuf bufs[3] = { *(KiBuf*)y, *(KiBuf*)g, *(KiBuf*)gin };
    KiPush push = { (uint32_t)rows, (uint32_t)cols, 0, (uint32_t)op };
    bool ok = dispatch_sync(G.pipe_softmax_bw, bufs, push,
                            ((uint32_t)rows + 255u) / 256u, 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3a: fused Cross-Entropy Forward. loss_i pro Zeile in ein Partial-
 * Buffer (leihweise aus dem Pool, binding 2), readback 0x4 faltet den
 * Partial-Download in den Compute-Submit; der Host mittelt (loss = mean_i).
 * logits/target sind resident (binding 0/1). submits++ + downloads++. */
bool moo_ki_gpu_ce_fwd_res(void* logits, void* target, int32_t rows, int32_t cols,
                           double* out_loss) {
    if (!logits || !target || !out_loss) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    KiBuf part;
    if (!buf_holen(&part, (VkDeviceSize)rows * 4)) { g_tel.cpu_fallbacks++; return false; }
    KiBuf bufs[3] = { *(KiBuf*)logits, *(KiBuf*)target, part };
    KiPush push = { (uint32_t)rows, (uint32_t)cols, 0, 0 };
    bool ok = dispatch_sync(G.pipe_ce_fwd, bufs, push,
                            ((uint32_t)rows + 255u) / 256u, 1,
                            /*upload*/ 0u, /*readback*/ 0x4u, /*resident*/ true);
    if (ok) {
        const float* pt = (const float*)part.map;
        double s = 0.0;
        for (int32_t i = 0; i < rows; i++) s += (double)pt[i];
        *out_loss = s / (double)rows;
        g_tel.downloads++;
    } else {
        g_tel.cpu_fallbacks++;
    }
    buf_zurueck(&part);
    return ok;
}

/* KIP-G3a: fused Cross-Entropy Backward. grad_ij = (softmax(x)_ij - t_ij)*scale
 * (scale = 1/batch fuer loss=mean). REINER Beitrag (kein +=). scale via
 * KiPush.N (float-Bitmuster). logits/target/grad resident. */
bool moo_ki_gpu_ce_bw_res(void* logits, void* target, void* grad,
                          int32_t rows, int32_t cols, float scale) {
    if (!logits || !target || !grad) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    uint32_t sbits; memcpy(&sbits, &scale, sizeof(sbits));
    KiBuf bufs[3] = { *(KiBuf*)logits, *(KiBuf*)target, *(KiBuf*)grad };
    KiPush push = { (uint32_t)rows, (uint32_t)cols, sbits, 0 };
    bool ok = dispatch_sync(G.pipe_ce_bw, bufs, push,
                            ((uint32_t)rows + 255u) / 256u, 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3b: zeilenweiser Normalisierungs-KERN (ohne Affine). op 0 LayerNorm
 * (x-mean)/sqrt(var+eps), op 1 RMSNorm x/sqrt(mean(x^2)+eps). Affine (*gamma
 * [+beta]) ist ew-Komposition. x an binding 0 UND 1 (ungenutzt). eps via
 * KiPush.N (float-Bit). */
bool moo_ki_gpu_norm_res(int32_t op, void* x, void* o, int32_t rows, int32_t cols, float eps) {
    if (!x || !o) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    uint32_t ebits; memcpy(&ebits, &eps, sizeof(ebits));
    KiBuf bufs[3] = { *(KiBuf*)x, *(KiBuf*)x, *(KiBuf*)o };
    KiPush push = { (uint32_t)rows, (uint32_t)cols, ebits, (uint32_t)op };
    bool ok = dispatch_sync(G.pipe_norm, bufs, push,
                            ((uint32_t)rows + 255u) / 256u, 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3b: zugehoeriger Backward w.r.t. x (Stats aus x rekonstruiert).
 * op 0 LN: dx=(1/s)(g-mean(g)-n*mean(g*n)); op 1 RMS: dx=(1/s)(g-n*mean(g*n)).
 * REINER Beitrag ohne += (das += ist G3c). */
bool moo_ki_gpu_norm_bw_res(int32_t op, void* x, void* g, void* dx,
                            int32_t rows, int32_t cols, float eps) {
    if (!x || !g || !dx) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    uint32_t ebits; memcpy(&ebits, &eps, sizeof(ebits));
    KiBuf bufs[3] = { *(KiBuf*)x, *(KiBuf*)g, *(KiBuf*)dx };
    KiPush push = { (uint32_t)rows, (uint32_t)cols, ebits, (uint32_t)op };
    bool ok = dispatch_sync(G.pipe_norm_bw, bufs, push,
                            ((uint32_t)rows + 255u) / 256u, 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3d-b: Achsen-Reduktion mit keepdims. op sum/mean/max, axis 0 -> [1,c],
 * axis 1 -> [r,1] (CPU-Ref moo_tensor_ops.c reduce_op). a an binding 0 UND 1
 * (ungenutzt). Ein Thread pro Ausgabe-Element. */
bool moo_ki_gpu_reduce_axis_res(int32_t op, int32_t axis, void* a, void* o,
                                int32_t rows, int32_t cols) {
    if (!a || !o) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    uint32_t outn = (axis == 0) ? (uint32_t)cols : (uint32_t)rows;
    KiBuf bufs[3] = { *(KiBuf*)a, *(KiBuf*)a, *(KiBuf*)o };
    KiPush push = { (uint32_t)rows, (uint32_t)cols, (uint32_t)axis, (uint32_t)op };
    bool ok = dispatch_sync(G.pipe_reduce_axis, bufs, push,
                            (outn + 255u) / 256u, 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3d-b: Broadcast mit Skalierung (Reduktions-Backward fuer sum/mean +
 * Baustein fuer ew-Broadcast-Forward). out[i,j]=src[axis==0?j:i]*scale. scale=1
 * fuer sum-bw / reinen Broadcast, 1/r|1/c fuer mean-bw. REINER Beitrag (kein +=). */
bool moo_ki_gpu_broadcast_res(int32_t axis, void* src, void* o,
                              int32_t rows, int32_t cols, float scale) {
    if (!src || !o) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    uint32_t sbits; memcpy(&sbits, &scale, sizeof(sbits));
    KiBuf bufs[3] = { *(KiBuf*)src, *(KiBuf*)src, *(KiBuf*)o };
    KiPush push = { (uint32_t)rows, (uint32_t)cols, sbits, (uint32_t)axis };
    bool ok = dispatch_sync(G.pipe_broadcast, bufs, push,
                            (uint32_t)(((int64_t)rows * cols + 255) / 256), 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3d-b: Subgradient der Max-Reduktion. g fliesst an die (erste) argmax-
 * Position je Gruppe (CPU-Ref bw_max). a=Input (fuer argmax), g=grad_out,
 * gin=[r,c]-Ergebnis. Ein Thread pro reduzierter Gruppe. REINER Beitrag (kein +=). */
bool moo_ki_gpu_reduce_max_bw_res(int32_t axis, void* a, void* g, void* gin,
                                  int32_t rows, int32_t cols) {
    if (!a || !g || !gin) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    uint32_t grpn = (axis == 0) ? (uint32_t)cols : (uint32_t)rows;
    KiBuf bufs[3] = { *(KiBuf*)a, *(KiBuf*)g, *(KiBuf*)gin };
    KiPush push = { (uint32_t)rows, (uint32_t)cols, (uint32_t)axis, 0 };
    bool ok = dispatch_sync(G.pipe_reduce_max_bw, bufs, push,
                            (grpn + 255u) / 256u, 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3d-d: Embedding-Lookup out[i,d] = W[idx[i], d]. idx = integer-wertige
 * floats. binding 0 = W, binding 1 = idx, binding 2 = out. Ein Thread pro
 * Ausgabe-Element. */
bool moo_ki_gpu_gather_res(void* w, void* idx, void* o,
                           int32_t rows, int32_t dim, int32_t vocab) {
    if (!w || !idx || !o) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    KiBuf bufs[3] = { *(KiBuf*)w, *(KiBuf*)idx, *(KiBuf*)o };
    KiPush push = { (uint32_t)rows, (uint32_t)dim, (uint32_t)vocab, 0 };
    bool ok = dispatch_sync(G.pipe_gather, bufs, push,
                            (uint32_t)(((int64_t)rows * dim + 255) / 256), 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3d-d: DETERMINISTISCHE scatter-add (gather-Backward) als Segment-
 * Reduktion. gW[v,d] = sum_{i: uint(idx[i])==v} g[i,d], sequentiell in i -> 2
 * Laeufe bit-identisch (KEIN atomicAdd, G0 §2). binding 0 = g, binding 1 = idx,
 * binding 2 = gW. Ein Thread pro (v,d). REINER Beitrag ohne += (das += ist G3c). */
bool moo_ki_gpu_scatter_add_res(void* g, void* idx, void* gw,
                                int32_t rows, int32_t dim, int32_t vocab) {
    if (!g || !idx || !gw) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    KiBuf bufs[3] = { *(KiBuf*)g, *(KiBuf*)idx, *(KiBuf*)gw };
    KiPush push = { (uint32_t)rows, (uint32_t)dim, (uint32_t)vocab, 0 };
    bool ok = dispatch_sync(G.pipe_scatter_add, bufs, push,
                            (uint32_t)(((int64_t)vocab * dim + 255) / 256), 1,
                            /*upload*/ 0u, /*readback*/ 0u, /*resident*/ true);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

/* KIP-G3d-e: matmul-Backward fuer C = A@B (A[m,k], B[k,n], C[m,n]) als
 * KOMPOSITION bestehender residenter Ops (G3d-c transpose + G2 tiled matmul):
 *   dA = g @ B^T  (B[k,n] -> bt[n,k];  da[m,k] = g[m,n] @ bt[n,k])
 *   dB = A^T @ g  (A[m,k] -> at[k,m];  db[k,n] = at[k,m] @ g[m,n])
 * bt/at leihweise aus dem Pool. da/db sind REINE Beitraege ohne += (die
 * Fan-out-Akkumulation macht der Aufrufer via moo_ki_gpu_grad_accum_res, G3c).
 * 4 Compute-Submits (2 transpose + 2 matmul). */
bool moo_ki_gpu_matmul_bw_res(void* a, void* b, void* g, void* da, void* db,
                              int32_t m, int32_t k, int32_t n) {
    if (!a || !b || !g || !da || !db) { g_tel.cpu_fallbacks++; return false; }
    if (!ki_gpu_init()) { g_tel.cpu_fallbacks++; return false; }
    KiBuf bt, at;
    if (!buf_holen(&bt, (VkDeviceSize)n * k * 4)) { g_tel.cpu_fallbacks++; return false; }
    if (!buf_holen(&at, (VkDeviceSize)k * m * 4)) {
        buf_zurueck(&bt); g_tel.cpu_fallbacks++; return false;
    }
    bool ok = moo_ki_gpu_transpose_res(b, &bt, k, n)
           && moo_ki_gpu_matmul_res(g, &bt, da, m, n, k)
           && moo_ki_gpu_transpose_res(a, &at, m, k)
           && moo_ki_gpu_matmul_res(&at, g, db, k, m, n);
    buf_zurueck(&bt);
    buf_zurueck(&at);
    if (!ok) g_tel.cpu_fallbacks++;
    return ok;
}

void moo_ki_gpu_telemetrie(MooKiGpuTelemetrie* out) {
    if (out) *out = g_tel;
}
void moo_ki_gpu_telemetrie_reset(void) {
    g_tel = (MooKiGpuTelemetrie){0};
}

#endif /* MOO_HAS_VULKAN */

/* KIP-FINAL-FIX (e413b176, GPT-Gegenreview KI-PROD-01): die MooValue/Dict/
 * String-Wrapper moo_ki_gpu_statistik()/_reset() sind NICHT mehr hier --
 * sie brauchten moo_dict_new/moo_string_new/moo_number/moo_dict_set/moo_none,
 * was diese bewusst schlanke, standalone-linkbare GPU-Kern-Datei (nur
 * Testquelle + moo_ki_gpu.c + Vulkan/libm, siehe die Standalone-Gate-Skripte
 * skripte/kip_gpu_coverage.sh, kip_g4_lm.sh, kip_g4b_lm.sh) zum Linken der
 * vollen Runtime-Symbole zwang. Jetzt in runtime/moo_ki_gpu_statistik.c
 * (Vollruntime-only, ueber build.rs immer mitgebaut) -- ruft ausschliesslich
 * die branch-unabhaengigen moo_ki_gpu_telemetrie/_reset() oben auf, keine
 * Verhaltensaenderung fuer gpu_statistik()/gpu_statistik_reset(). */
