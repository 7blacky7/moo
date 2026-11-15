/* P2-T83 C1 (Plan f3217c27, Reviews 6315dba0/c23f9a29): valid-Masken der
 * nativen Tensor-Leihe (moo_native_leihen/moo_native_zurueck) gegen die ECHTE
 * Host-Sicherung (moo_tensor_host_sichern -> moo_tensor_f32_sichern aus
 * runtime/moo_tensor.c, kein Stub).
 *   - bf16-Tensor mit valid=STORE und data==NULL: die Leihe materialisiert
 *     f32-data aus dem bf16-store (Werte korrekt), danach DATA|STORE.
 *   - @liest (schreibbar = 0): Maske bleibt DATA|STORE, store bleibt gueltig.
 *   - schreibbar: Schreiben landet kopierfrei in data, danach genau MOO_V_DATA.
 *   - GPU-Sicht (DATA|DEV) bei @liest unveraendert; schreibbar -> DATA.
 *   - Fehlerpfad (falscher Elementtyp): keine Leihe, Maske unberuehrt.
 * Laeuft unter ASan/UBSan/LSan in skripte/p2/runtime/sanitizer.sh. */
#include "../../runtime/moo_buffer_index.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int throws;
static int downloads;

MooValue moo_none(void) { return (MooValue){MOO_NONE, 0}; }
MooValue moo_error(const char *message) { (void)message; return moo_none(); }
void moo_throw(MooValue value) { (void)value; throws++; }
int moo_native_fehler_offen(void) { return throws != 0; }
const char *moo_type_name(MooValue value) { (void)value; return "?"; }
/* GPU-Download wird nur bei valid ohne DATA/STORE erreicht; hier nie. */
bool moo_ki_gpu_download(void *buf, float *ziel, int64_t bytes) {
    (void)buf; (void)ziel; (void)bytes;
    downloads++;
    return false;
}

static uint16_t bf16(float f) {
    uint32_t x;
    memcpy(&x, &f, sizeof(x));
    return (uint16_t)(x >> 16); /* Testwerte sind in bf16 exakt darstellbar */
}

static MooValue tensor_wert(MooTensor *t) {
    MooValue v = {MOO_TENSOR, 0};
    moo_val_set_ptr(&v, t);
    return v;
}

int main(void) {
    MooTensor t;
    memset(&t, 0, sizeof t);
    t.refcount = 1;
    t.ndim = 1;
    t.size = 3;
    t.shape[0] = 3;
    t.strides[0] = 1;
    t.dtype = MOO_DT_BF16;
    uint16_t *store = malloc(3 * sizeof(uint16_t));
    assert(store);
    store[0] = bf16(1.5f);
    store[1] = bf16(-2.0f);
    store[2] = bf16(3.0f);
    t.store = store;
    t.data = NULL;
    t.valid = MOO_V_STORE;
    MooValue wert = tensor_wert(&t);
    void *zeiger = NULL;
    uint64_t laenge = 0;

    /* 1. @liest auf bf16 ohne f32-Sicht: echte Materialisierung aus store. */
    MooNativeLeihe *leihe = moo_native_leihen(&wert, MOO_BUFFER_F32, 0, 0, &zeiger, &laenge);
    assert(leihe && throws == 0);
    assert(t.data != NULL && zeiger == (void *)t.data && laenge == 3);
    assert(t.data[0] == 1.5f && t.data[1] == -2.0f && t.data[2] == 3.0f);
    assert(t.valid == (MOO_V_DATA | MOO_V_STORE));
    moo_native_zurueck(leihe);
    assert(t.valid == (MOO_V_DATA | MOO_V_STORE));
    assert(t.store == store && store[0] == bf16(1.5f));

    /* 2. schreibbar: kopierfrei in data, danach ist nur noch DATA gueltig. */
    leihe = moo_native_leihen(&wert, MOO_BUFFER_F32, 0, 1, &zeiger, &laenge);
    assert(leihe && zeiger == (void *)t.data);
    ((float *)zeiger)[0] = 9.0f;
    moo_native_zurueck(leihe);
    assert(t.valid == MOO_V_DATA && t.data[0] == 9.0f);

    /* 3. GPU-Sicht: @liest laesst DEV gueltig (Fastpath, kein Download). */
    t.valid = MOO_V_DATA | MOO_V_DEV;
    leihe = moo_native_leihen(&wert, MOO_BUFFER_F32, 0, 0, &zeiger, &laenge);
    assert(leihe);
    moo_native_zurueck(leihe);
    assert(t.valid == (MOO_V_DATA | MOO_V_DEV));

    /* 4. schreibbar mit GPU-Sicht: DEV verworfen, auch ohne echtes Schreiben. */
    leihe = moo_native_leihen(&wert, MOO_BUFFER_F32, 0, 1, &zeiger, &laenge);
    moo_native_zurueck(leihe);
    assert(t.valid == MOO_V_DATA);

    /* 5. Falscher Elementtyp: fangbarer Fehler, keine Leihe, Maske unberuehrt. */
    t.valid = MOO_V_STORE | MOO_V_DATA;
    leihe = moo_native_leihen(&wert, MOO_BUFFER_I32, 0, 1, &zeiger, &laenge);
    assert(!leihe && throws == 1 && zeiger == NULL && laenge == 0);
    assert(t.valid == (MOO_V_STORE | MOO_V_DATA));
    moo_native_zurueck(NULL);

    assert(downloads == 0);
    free(t.data);
    free(store);
    return 0;
}
