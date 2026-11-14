#include "../../runtime/moo_buffer_index.h"
#include "../../runtime/moo_buffer_value.h"
#include <assert.h>
#include <math.h>

static int releases;
static int throws;
void moo_release(MooValue value) { if (value.tag == MOO_STRING) releases++; }
MooValue moo_none(void) { return (MooValue){MOO_NONE, 0}; }
MooValue moo_number(double value) {
    MooValue result = {MOO_NUMBER, 0};
    moo_val_set_double(&result, value);
    return result;
}
MooValue moo_error(const char *message) { (void)message; return moo_none(); }
void moo_throw(MooValue value) { (void)value; throws++; }
static int legacy_set;
MooValue moo_list_get(MooValue c, MooValue i) { (void)c; (void)i; return moo_number(73); }
MooValue moo_dict_get(MooValue c, MooValue i) { (void)c; (void)i; return moo_number(74); }
MooValue moo_string_index(MooValue c, MooValue i) { (void)c; (void)i; return moo_number(75); }
void moo_list_set(MooValue c, MooValue i, MooValue v) { (void)c; (void)i; (void)v; legacy_set = 1; }
void moo_dict_set(MooValue c, MooValue i, MooValue v) { (void)c; (void)i; (void)v; legacy_set = 2; }

int main(void) {
    MooValue owner;
    assert(moo_buffer_value_new(MOO_BUFFER_I32, 2, &owner) == MOO_BUFFER_OK);
    moo_buffer_index_set(owner, moo_number(1), moo_number(-2147483648.0));
    assert(throws == 0);
    MooValue result = moo_buffer_index_get(owner, moo_number(1));
    assert(result.tag == MOO_NUMBER && moo_val_as_double(result) == -2147483648.0);
    moo_buffer_index_set(owner, moo_number(0), moo_number(2147483647.0));
    assert(moo_val_as_double(moo_buffer_index_get(owner, moo_number(0))) == 2147483647.0);
    moo_buffer_index_set(owner, moo_number(0), moo_number(2147483648.0));
    moo_buffer_index_set(owner, moo_number(0), moo_number(1.5));
    assert(throws == 2);
    assert(moo_val_as_double(moo_buffer_index_get(owner, moo_number(0))) == 2147483647.0);
    MooValue heap = {MOO_STRING, 1};
    moo_buffer_index_set(owner, heap, heap);
    assert(throws == 3 && releases == 2);
    result = moo_buffer_index_get(owner, heap);
    assert(result.tag == MOO_NONE && throws == 4 && releases == 3);
    assert(moo_buffer_index_get(owner, moo_number(2)).tag == MOO_NONE);
    assert(moo_buffer_index_get(owner, moo_number(-1)).tag == MOO_NONE);
    assert(moo_buffer_index_get(owner, moo_number(0.5)).tag == MOO_NONE);
    assert(moo_buffer_index_get(owner, moo_number(NAN)).tag == MOO_NONE);
    assert(moo_buffer_index_get(owner, moo_number(INFINITY)).tag == MOO_NONE);
    assert(throws == 9);
    /* P2-T50: alle Skalartypen sind lesbar/schreibbar (frueher nur i32). */
    MooValue wrong;
    assert(moo_buffer_value_new(MOO_BUFFER_U32, 1, &wrong) == MOO_BUFFER_OK);
    assert(moo_val_as_double(moo_buffer_index_get(wrong, moo_number(0))) == 0.0 && throws == 9);
    moo_buffer_index_set(wrong, moo_number(0), moo_number(4294967295.0));
    assert(moo_val_as_double(moo_buffer_index_get(wrong, moo_number(0))) == 4294967295.0);
    moo_buffer_index_set(wrong, moo_number(0), moo_number(-1));
    assert(throws == 10);
    /* Exercise the real generic dispatcher; mocks only stand in for legacy containers. */
    moo_index_set(owner, moo_number(1), moo_number(123));
    assert(moo_val_as_double(moo_index_get(owner, moo_number(1))) == 123);
    assert(moo_index_get(owner, moo_number(5)).tag == MOO_NONE && throws == 11);
    /* Grenzen je Typ: u8 0..255 ganzzahlig, i64 exakt bis 2^53 lesbar,
     * f32 rundet, endliche Werte ueber FLT_MAX werden verworfen, NaN bleibt. */
    MooValue bytes = moo_buffer_create(MOO_BUFFER_U8, moo_number(2));
    assert(bytes.tag == MOO_BUFFER && throws == 11);
    moo_buffer_index_set(bytes, moo_number(0), moo_number(255));
    moo_buffer_index_set(bytes, moo_number(1), moo_number(256));
    moo_buffer_index_set(bytes, moo_number(1), moo_number(0.5));
    assert(throws == 13 && moo_val_as_double(moo_buffer_index_get(bytes, moo_number(0))) == 255.0);
    MooValue wide = moo_buffer_create(MOO_BUFFER_I64, moo_number(2));
    moo_buffer_index_set(wide, moo_number(0), moo_number(-9007199254740992.0));
    moo_buffer_index_set(wide, moo_number(1), moo_number(9223372036854775808.0));
    assert(throws == 14);
    assert(moo_val_as_double(moo_buffer_index_get(wide, moo_number(0))) == -9007199254740992.0);
    moo_buffer_index_set(wide, moo_number(1), moo_number(-9223372036854775808.0));
    assert(moo_buffer_index_get(wide, moo_number(1)).tag == MOO_NONE && throws == 15);
    MooValue floats = moo_buffer_create(MOO_BUFFER_F32, moo_number(3));
    moo_buffer_index_set(floats, moo_number(0), moo_number(0.1));
    moo_buffer_index_set(floats, moo_number(1), moo_number(1e300));
    moo_buffer_index_set(floats, moo_number(2), moo_number(NAN));
    assert(throws == 16);
    assert(moo_val_as_double(moo_buffer_index_get(floats, moo_number(0))) == (double)0.1f);
    assert(isnan(moo_val_as_double(moo_buffer_index_get(floats, moo_number(2)))));
    /* Konstruktor: Laenge 0 gueltig; negative/gebrochene Laenge und Typ 0 werfen. */
    MooValue leer = moo_buffer_create(MOO_BUFFER_F64, moo_number(0));
    assert(leer.tag == MOO_BUFFER && throws == 16);
    assert(moo_buffer_create(MOO_BUFFER_I32, moo_number(-1)).tag == MOO_NONE && throws == 17);
    assert(moo_buffer_create(MOO_BUFFER_I32, moo_number(1.5)).tag == MOO_NONE && throws == 18);
    assert(moo_buffer_create(0, moo_number(1)).tag == MOO_NONE && throws == 19);
    assert(moo_buffer_create(11, moo_number(1)).tag == MOO_NONE && throws == 20);
    /* P2-T50 C3 (Plan bb3878ef): anhaengen, Ansicht und nativer Pin (Batch A/B);
     * Pin-Leck-Fall: nach unpin ist die Laenge wieder aenderbar (Risiko R1). */
    int t0 = throws;
    MooValue reihe = moo_buffer_create(MOO_BUFFER_I16, moo_number(0));
    moo_buffer_append_value(reihe, moo_number(-5));
    moo_buffer_append_value(reihe, moo_number(7));
    assert(throws == t0 && moo_buffer_len(moo_val_as_ptr(reihe)) == 2);
    moo_buffer_append_value(reihe, moo_number(40000));
    moo_buffer_append_value(reihe, moo_number(0.5));
    moo_buffer_append_value(moo_none(), moo_number(1));
    assert(throws == t0 + 3 && moo_buffer_len(moo_val_as_ptr(reihe)) == 2);
    MooValue sicht = moo_buffer_view_value(reihe, moo_number(1), moo_number(1));
    assert(sicht.tag == MOO_BUFFER && moo_val_as_double(moo_buffer_index_get(sicht, moo_number(0))) == 7.0);
    moo_buffer_append_value(reihe, moo_number(1));
    moo_buffer_append_value(sicht, moo_number(1));
    assert(throws == t0 + 5 && moo_buffer_len(moo_val_as_ptr(reihe)) == 2);
    assert(moo_buffer_view_value(reihe, moo_number(2), moo_number(1)).tag == MOO_NONE);
    assert(moo_buffer_view_value(reihe, moo_number(-1), moo_number(1)).tag == MOO_NONE);
    assert(throws == t0 + 7);
    moo_buffer_drop(moo_val_as_ptr(sicht));
    moo_buffer_append_value(reihe, moo_number(3));
    assert(throws == t0 + 7 && moo_buffer_len(moo_val_as_ptr(reihe)) == 3);
    void *daten = (void *)&t0;
    uint64_t laenge = 99;
    assert(moo_buffer_native_pin(&reihe, MOO_BUFFER_I32, &daten, &laenge) == NULL);
    assert(daten == NULL && laenge == 0 && throws == t0 + 8);
    MooBufferPin *pin = moo_buffer_native_pin(&reihe, MOO_BUFFER_I16, &daten, &laenge);
    assert(pin && laenge == 3 && ((int16_t *)daten)[0] == -5 && ((int16_t *)daten)[2] == 3);
    moo_buffer_append_value(reihe, moo_number(4));
    assert(throws == t0 + 9 && moo_buffer_len(moo_val_as_ptr(reihe)) == 3);
    moo_buffer_unpin(pin);
    moo_buffer_append_value(reihe, moo_number(4));
    assert(throws == t0 + 9 && moo_buffer_len(moo_val_as_ptr(reihe)) == 4);
    MooValue leer2 = moo_buffer_create(MOO_BUFFER_F32, moo_number(0));
    pin = moo_buffer_native_pin(&leer2, MOO_BUFFER_F32, &daten, &laenge);
    assert(pin && daten == NULL && laenge == 0);
    moo_buffer_unpin(pin);
    moo_buffer_drop(moo_val_as_ptr(reihe));
    moo_buffer_drop(moo_val_as_ptr(leer2));
    moo_buffer_drop(moo_val_as_ptr(bytes));
    moo_buffer_drop(moo_val_as_ptr(wide));
    moo_buffer_drop(moo_val_as_ptr(floats));
    moo_buffer_drop(moo_val_as_ptr(leer));
    assert(moo_val_as_double(moo_index_get((MooValue){MOO_LIST, 0}, moo_number(0))) == 73);
    assert(moo_val_as_double(moo_index_get((MooValue){MOO_DICT, 0}, moo_number(0))) == 74);
    assert(moo_val_as_double(moo_index_get((MooValue){MOO_STRING, 0}, moo_number(0))) == 75);
    moo_index_set((MooValue){MOO_LIST, 0}, moo_number(0), moo_number(1));
    assert(legacy_set == 1);
    moo_index_set((MooValue){MOO_DICT, 0}, moo_number(0), moo_number(1));
    assert(legacy_set == 2);
    moo_buffer_drop(moo_val_as_ptr(owner));
    moo_buffer_drop(moo_val_as_ptr(wrong));
    return 0;
}
