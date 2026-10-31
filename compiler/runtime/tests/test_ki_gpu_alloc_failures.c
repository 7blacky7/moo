/* Vulkan-Buffer-Fehlerpfade: echte Runtime mit kontrollierten Vulkan-Stubs.
 * Keine GPU-Ausfuehrung; alle Speicheroperationen werden bilanziert.
 * Build: siehe run_ki_gpu_alloc_failures.sh. MOO_GPU_SOURCE erlaubt einen
 * unveraenderten Altstand als Negativkontrolle, ohne Projektdateien zu ersetzen.
 */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s (case=%s, stage=%d, nth=%d)\n", \
            __FILE__, __LINE__, #x, case_name, fail_stage, fail_nth); \
    exit(1); } } while (0)
#define CAP 512
#define HANDLE(T, n) ((T)(uintptr_t)(n))

typedef struct { int alive; size_t size; uintptr_t bound; } TestBuffer;
typedef struct { int alive, mapped; void* data; } TestMemory;
static TestBuffer buffers[CAP];
static TestMemory memories[CAP];
static unsigned next_buffer, next_memory, live_buffers, live_memories;
static int calls[5], fail_stage, fail_nth, cases;
static const char* case_name = "init";

/* Jede Fehlerstufe darf ihre Ausgabe vergiften: Vulkan garantiert bei einem
 * Laufzeitfehler im Allgemeinen keinen gueltigen Ausgabeparameter. */
static int fail_here(int stage) { return ++calls[stage] == fail_nth && stage == fail_stage; }
static unsigned buffer_id(VkBuffer b) {
    uintptr_t n = (uintptr_t)b;
    CHECK(n > 0 && n < CAP && buffers[n].alive);
    return (unsigned)n;
}
static unsigned memory_id(VkDeviceMemory m) {
    uintptr_t n = (uintptr_t)m;
    CHECK(n > 0 && n < CAP && memories[n].alive);
    return (unsigned)n;
}
static VKAPI_ATTR VkResult VKAPI_CALL test_create(VkDevice d,
        const VkBufferCreateInfo* ci, const VkAllocationCallbacks* a, VkBuffer* out) {
    (void)d; (void)a;
    if (fail_here(1)) { *out = HANDLE(VkBuffer, UINTPTR_MAX); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    CHECK(next_buffer + 1 < CAP && ci->size > 0 && ci->size <= 65536);
    unsigned n = ++next_buffer;
    buffers[n] = (TestBuffer){1, (size_t)ci->size, 0};
    ++live_buffers; *out = HANDLE(VkBuffer, n); return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL test_requirements(VkDevice d, VkBuffer b, VkMemoryRequirements* out) {
    (void)d;
    *out = (VkMemoryRequirements){buffers[buffer_id(b)].size, 4, 1};
}
static VKAPI_ATTR VkResult VKAPI_CALL test_allocate(VkDevice d,
        const VkMemoryAllocateInfo* ai, const VkAllocationCallbacks* a, VkDeviceMemory* out) {
    (void)d; (void)a;
    if (fail_here(2)) { *out = HANDLE(VkDeviceMemory, UINTPTR_MAX); return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    CHECK(next_memory + 1 < CAP && ai->allocationSize <= 65536);
    unsigned n = ++next_memory;
    memories[n] = (TestMemory){1, 0, calloc(1, (size_t)ai->allocationSize)};
    CHECK(memories[n].data != NULL);
    ++live_memories; *out = HANDLE(VkDeviceMemory, n); return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL test_bind(VkDevice d, VkBuffer b, VkDeviceMemory m, VkDeviceSize offset) {
    (void)d; (void)offset;
    unsigned bi = buffer_id(b), mi = memory_id(m);
    if (fail_here(3)) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    buffers[bi].bound = mi; return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL test_map(VkDevice d, VkDeviceMemory m,
        VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** out) {
    (void)d; (void)offset; (void)size; (void)flags;
    unsigned n = memory_id(m); CHECK(!memories[n].mapped);
    if (fail_here(4)) { *out = (void*)UINTPTR_MAX; return VK_ERROR_MEMORY_MAP_FAILED; }
    memories[n].mapped = 1; *out = memories[n].data; return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL test_unmap(VkDevice d, VkDeviceMemory m) {
    (void)d; unsigned n = memory_id(m); CHECK(memories[n].mapped);
    memories[n].mapped = 0;
}
static VKAPI_ATTR void VKAPI_CALL test_destroy(VkDevice d, VkBuffer b, const VkAllocationCallbacks* a) {
    (void)d; (void)a; unsigned n = buffer_id(b);
    buffers[n].alive = 0; --live_buffers;
}
static VKAPI_ATTR void VKAPI_CALL test_free(VkDevice d, VkDeviceMemory m, const VkAllocationCallbacks* a) {
    (void)d; (void)a; unsigned n = memory_id(m);
    CHECK(!memories[n].mapped);
    for (unsigned i = 1; i <= next_buffer; ++i) CHECK(!buffers[i].alive || buffers[i].bound != n);
    free(memories[n].data); memories[n].data = NULL;
    memories[n].alive = 0; --live_memories;
}

/* Nur die zu testenden Ressourcenfunktionen ersetzen. Die uebrigen Vulkan-
 * Symbole werden gelinkt, aber in diesen ausschliesslichen Fehlerpfaden nie benutzt. */
#define vkCreateBuffer test_create
#define vkGetBufferMemoryRequirements test_requirements
#define vkAllocateMemory test_allocate
#define vkBindBufferMemory test_bind
#define vkMapMemory test_map
#define vkUnmapMemory test_unmap
#define vkDestroyBuffer test_destroy
#define vkFreeMemory test_free
#ifndef MOO_HAS_VULKAN
#define MOO_HAS_VULKAN 1
#endif
#ifndef MOO_GPU_SOURCE
#define MOO_GPU_SOURCE "../moo_ki_gpu.c"
#endif
#include MOO_GPU_SOURCE

/* Pro Fall ein frischer, threadlokal nicht benoetigter Singleton. */
static void begin_case(const char* name, int unified, int stage, int nth) {
    CHECK(live_buffers == 0 && live_memories == 0);
    memset(buffers, 0, sizeof(buffers)); memset(memories, 0, sizeof(memories));
    memset(calls, 0, sizeof(calls)); memset(g_pool, 0, sizeof(g_pool));
    memset(&G, 0, sizeof(G)); memset(&g_tel, 0, sizeof(g_tel));
    next_buffer = next_memory = 0;
    fail_stage = stage; fail_nth = nth; case_name = name;
    G.init_versucht = true; G.bereit = true; G.devlocal_vereint = unified != 0;
    G.geraet = HANDLE(VkDevice, 1);
}
static void finish_case(void) {
    /* Erlaubt: gute Buffer liegen nach einem Fehlversuch weiterhin im Pool. */
    for (int i = 0; i < KI_POOL_MAX; ++i) {
        CHECK(!g_pool[i].belegt);
        if (g_pool[i].kapazitaet) buf_weg(&g_pool[i].buf);
    }
    memset(g_pool, 0, sizeof(g_pool));
    CHECK(live_buffers == 0 && live_memories == 0);
    ++cases;
}
static void check_empty(const KiBuf* b) {
    CHECK(!b->dev && !b->dev_mem && !b->stg && !b->stg_mem && !b->map);
    CHECK(b->groesse == 0 && b->pool_slot == -1);
}

/* Teilfehler duerfen beim direkten Aufruf und bei der Handle-API nichts leaken. */
static void test_partial_failures(void) {
    for (int unified = 0; unified <= 1; ++unified) {
        for (int stage = 1; stage <= 4; ++stage) {
            int count = (unified || stage == 4) ? 1 : 2;
            for (int nth = 1; nth <= count; ++nth) {
                begin_case("partial", unified, stage, nth);
                KiBuf b; memset(&b, 0xa5, sizeof(b));
                CHECK(!buf_anlegen(&b, 64));
                check_empty(&b);
                buf_zurueck(&b); buf_weg(&b); /* Aufraeumen ist wiederholbar. */
                finish_case();
                begin_case("resident-handle", unified, stage, nth);
                CHECK(moo_ki_gpu_buf_belegen(64) == NULL);
                finish_case();
            }
        }
    }
}

/* Entscheidend: Fehler beim 1., 2. oder 3. Buffer der &&-Kette. */
static void test_short_circuit(void) {
    float in[4] = {1, 2, 3, 4}, out[4] = {0};
    double sum = 0;
    for (int unified = 0; unified <= 1; ++unified) {
        for (int op = 0; op < 3; ++op) {
            for (int stage = 1; stage <= 4; ++stage) {
                int count = (unified || stage == 4) ? 3 : 6;
                for (int nth = 1; nth <= count; ++nth) {
                    begin_case(op == 0 ? "matmul" : op == 1 ? "elementwise" : "reduce", unified, stage, nth);
                    bool ok = op == 0 ? moo_ki_gpu_matmul(in, in, out, 2, 2, 2)
                            : op == 1 ? moo_ki_gpu_ew(0, in, in, out, 4)
                            : moo_ki_gpu_reduce_sum(in, 4, &sum);
                    CHECK(!ok); CHECK(calls[stage] == nth);
                    finish_case();
                }
            }
        }
    }
}

/* Auch normale Freigabe und Pool-Ueberlauf muessen ohne Doppelfreigabe gehen. */
static void test_pool_and_success(void) {
    for (int unified = 0; unified <= 1; ++unified) {
        begin_case("pool-overflow-and-reuse", unified, 0, 0);
        KiBuf b[KI_POOL_MAX + 1];
        for (int i = 0; i <= KI_POOL_MAX; ++i) CHECK(buf_holen(&b[i], 64));
        for (int i = 0; i <= KI_POOL_MAX; ++i) { buf_zurueck(&b[i]); check_empty(&b[i]); }
        CHECK(live_buffers == (unsigned)(KI_POOL_MAX * (unified ? 1 : 2)));
        unsigned allocations = next_memory;
        CHECK(buf_holen(&b[0], 64)); CHECK(next_memory == allocations);
        buf_zurueck(&b[0]); check_empty(&b[0]);
        finish_case();

        begin_case("direct-success", unified, 0, 0);
        KiBuf single; CHECK(buf_anlegen(&single, 64));
        buf_weg(&single); check_empty(&single); buf_weg(&single);
        finish_case();
    }
}

int main(void) {
    CHECK(setenv("MOO_KI_GPU_ERZWINGEN", "1", 1) == 0);
    test_partial_failures();
    test_short_circuit();
    test_pool_and_success();
    printf("PASS: %d Buffer-Lifecycle-Faelle; keine ungueltigen Handles, keine Leaks.\n", cases);
    return 0;
}
