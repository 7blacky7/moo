#include "../moo_buffer.h"
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

void moo_test_buffer_value(void);

/* P2-T48 D (Plan a71947f7 E3, Review e1635264): Strukturpuffer gegen ein
 * C-struct. new_struct mit sizeof/_Alignof und fester ID; ID 0 und zu grosse
 * Ausrichtung abgewiesen; struct_id fuer Besitzer, Ansicht und verschachtelte
 * Ansicht; Ansichtsstart/Stride == sizeof ueber Pin-Adressen; Skalar-API
 * sieht keinen Skalartyp. */
typedef struct { uint32_t id; float x; double masse; } MooTestTeilchen;

static void test_struct_buffer(void) {
    const uint64_t id = 0x1234ABCDull;
    MooBuffer *b = NULL, *v = NULL, *vv = NULL;
    MooBufferPin *pin = NULL, *vpin = NULL;
    void *data = NULL, *vdata = NULL;
    assert(moo_buffer_new_struct(sizeof(MooTestTeilchen), _Alignof(MooTestTeilchen), 0, 2, &b) == MOO_BUFFER_INVALID && !b);
    assert(moo_buffer_new_struct(32, MOO_MAX_ALIGN * 2, id, 1, &b) == MOO_BUFFER_INVALID && !b);
    assert(moo_buffer_struct_id(NULL) == 0);
    assert(moo_buffer_new_struct(sizeof(MooTestTeilchen), _Alignof(MooTestTeilchen), id, 4, &b) == MOO_BUFFER_OK);
    assert(moo_buffer_struct_id(b) == id && moo_buffer_len(b) == 4);
    assert(moo_buffer_scalar_type(b) == MOO_BUFFER_SCALAR_NONE);
    for (uint32_t i = 0; i < 4; ++i) {
        MooTestTeilchen t = { i + 1, (float)i * 0.5f, (double)i * 2.0 };
        assert(moo_buffer_set(b, i, &t) == MOO_BUFFER_OK);
    }
    uint32_t wert = 0;
    assert(moo_buffer_get_scalar(b, MOO_BUFFER_U32, 0, &wert) == MOO_BUFFER_TYPE);
    assert(moo_buffer_view(b, 1, 3, &v) == MOO_BUFFER_OK && moo_buffer_struct_id(v) == id);
    assert(moo_buffer_view(v, 1, 2, &vv) == MOO_BUFFER_OK && moo_buffer_struct_id(vv) == id);
    assert(moo_buffer_pin(b, &pin, &data) == MOO_BUFFER_OK && data);
    assert(moo_buffer_pin(vv, &vpin, &vdata) == MOO_BUFFER_OK && vdata);
    /* Stride = sizeof(C-struct): vv beginnt bei Element 2 des Besitzers. */
    assert((uintptr_t)vdata - (uintptr_t)data == 2 * sizeof(MooTestTeilchen));
    assert((uintptr_t)data % _Alignof(MooTestTeilchen) == 0);
    const MooTestTeilchen *ts = data, *vs = vdata;
    assert(ts[3].id == 4 && ts[3].x == 1.5f && ts[3].masse == 6.0);
    assert(vs[0].id == 3 && vs[1].id == 4 && vs[1].masse == 6.0);
    MooTestTeilchen kopie;
    assert(moo_buffer_get(vv, 1, &kopie) == MOO_BUFFER_OK && kopie.id == 4);
    assert(moo_buffer_get(vv, 2, &kopie) == MOO_BUFFER_BOUNDS);
    moo_buffer_unpin(vpin);
    moo_buffer_unpin(pin);
    moo_buffer_drop(vv);
    moo_buffer_drop(v);
    moo_buffer_drop(b);
}

int main(void) {
    test_struct_buffer();
    moo_test_buffer_value();
    MooBuffer *b = NULL, *v = NULL, *nested = NULL;
    MooBufferPin* pin = NULL;
    assert(moo_buffer_new(3, 2, 1, &b) == MOO_BUFFER_INVALID && !b);
    void* data = NULL;
    uint32_t x = 42, y = 0;
    assert(moo_buffer_new(sizeof(x), _Alignof(uint32_t), 0, &b) == MOO_BUFFER_OK);
    assert(moo_buffer_get(b, 0, &y) == MOO_BUFFER_BOUNDS);
    assert(moo_buffer_retain(b) == MOO_BUFFER_OK);
    moo_buffer_drop(b);
    assert(moo_buffer_resize(b, 4) == MOO_BUFFER_OK);
    assert(moo_buffer_set(b, 1, &x) == MOO_BUFFER_OK);
    assert(moo_buffer_view(b, 1, 2, &v) == MOO_BUFFER_OK);
    assert(moo_buffer_view(v, 0, 1, &nested) == MOO_BUFFER_OK);
    assert(moo_buffer_resize(b, 5) == MOO_BUFFER_PINNED);
    assert(moo_buffer_get(nested, 0, &y) == MOO_BUFFER_OK && y == 42);
    assert(moo_buffer_pin(v, &pin, &data) == MOO_BUFFER_OK && data);
    moo_buffer_drop(nested);
    moo_buffer_drop(v);
    assert(moo_buffer_resize(b, 0) == MOO_BUFFER_PINNED);
    moo_buffer_drop(b);
    assert(*(uint32_t*)data == 42);
    moo_buffer_unpin(pin);
    assert(moo_buffer_new(2, 2, SIZE_MAX, &b) == MOO_BUFFER_OVERFLOW && !b);
    assert(moo_buffer_new(sizeof(x), _Alignof(uint32_t), 1, &b) == MOO_BUFFER_OK);
    assert(moo_buffer_view(b, 2, 0, &v) == MOO_BUFFER_BOUNDS && !v);
    assert(moo_buffer_resize(b, 2) == MOO_BUFFER_OK);
    assert(moo_buffer_get(b, 1, &y) == MOO_BUFFER_OK && y == 0);
    assert(moo_buffer_view(b, 2, 0, &v) == MOO_BUFFER_OK);
    assert(moo_buffer_resize(b, 1) == MOO_BUFFER_PINNED);
    assert(moo_buffer_resize(v, 1) == MOO_BUFFER_INVALID);
    moo_buffer_drop(v);
    assert(moo_buffer_pin(b, &pin, &data) == MOO_BUFFER_OK);
    MooBufferPin* second_pin = NULL;
    void* data2 = NULL;
    assert(moo_buffer_pin(b, &second_pin, &data2) == MOO_BUFFER_OK && data == data2);
    moo_buffer_unpin(pin);
    assert(moo_buffer_resize(b, 1) == MOO_BUFFER_PINNED);
    moo_buffer_unpin(second_pin);
    assert(moo_buffer_resize(b, 1) == MOO_BUFFER_OK);
    moo_buffer_drop(b);
    /* Same-size scalar types are never interchangeable; views keep the ID. */
    struct ScalarCase { MooBufferScalarType id; size_t size; size_t align; };
    const struct ScalarCase cases[] = {
        {MOO_BUFFER_I8, sizeof(int8_t), _Alignof(int8_t)},
        {MOO_BUFFER_U8, sizeof(uint8_t), _Alignof(uint8_t)},
        {MOO_BUFFER_I16, sizeof(int16_t), _Alignof(int16_t)},
        {MOO_BUFFER_U16, sizeof(uint16_t), _Alignof(uint16_t)},
        {MOO_BUFFER_I32, sizeof(int32_t), _Alignof(int32_t)},
        {MOO_BUFFER_U32, sizeof(uint32_t), _Alignof(uint32_t)},
        {MOO_BUFFER_I64, sizeof(int64_t), _Alignof(int64_t)},
        {MOO_BUFFER_U64, sizeof(uint64_t), _Alignof(uint64_t)},
        {MOO_BUFFER_F32, sizeof(float), _Alignof(float)},
        {MOO_BUFFER_F64, sizeof(double), _Alignof(double)}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        unsigned char input[8] = {1,2,3,4,5,6,7,8}, output[8] = {0};
        assert(cases[i].size <= sizeof(input));
        assert(moo_buffer_new_scalar(cases[i].id, 1, &b) == MOO_BUFFER_OK);
        assert(moo_buffer_set_scalar(b, cases[i].id, 0, input) == MOO_BUFFER_OK);
        assert(moo_buffer_append_scalar(b, cases[i].id, input) == MOO_BUFFER_OK);
        assert(moo_buffer_get_scalar(b, cases[i].id, 1, output) == MOO_BUFFER_OK);
        assert(memcmp(input, output, cases[i].size) == 0);
        assert(moo_buffer_view(b, 0, 1, &v) == MOO_BUFFER_OK);
        assert(moo_buffer_scalar_type(b) == cases[i].id);
        assert(moo_buffer_scalar_type(v) == cases[i].id);
        assert(moo_buffer_get_scalar(v, cases[i].id, 0, output) == MOO_BUFFER_OK);
        assert(memcmp(input, output, cases[i].size) == 0);
        assert(moo_buffer_pin(v, &pin, &data) == MOO_BUFFER_OK);
        assert((uintptr_t)data % cases[i].align == 0);
        moo_buffer_unpin(pin);
        moo_buffer_drop(b);
        assert(moo_buffer_get_scalar(v, cases[i].id, 0, output) == MOO_BUFFER_OK);
        moo_buffer_drop(v);
    }
    assert(moo_buffer_new_scalar(MOO_BUFFER_I32, 0, &b) == MOO_BUFFER_OK);
    for (int32_t i = 0; i < 10000; ++i)
        assert(moo_buffer_append_scalar(b, MOO_BUFFER_I32, &i) == MOO_BUFFER_OK);
    assert(moo_buffer_len(b) == 10000);
    for (int32_t i = 0; i < 10000; ++i) {
        int32_t got = -1;
        assert(moo_buffer_get_scalar(b, MOO_BUFFER_I32, (size_t)i, &got) == MOO_BUFFER_OK && got == i);
    }
    assert(moo_buffer_resize(b, 2) == MOO_BUFFER_OK);
    assert(moo_buffer_resize(b, 3) == MOO_BUFFER_OK);
    int32_t zeroed = -1;
    assert(moo_buffer_get_scalar(b, MOO_BUFFER_I32, 2, &zeroed) == MOO_BUFFER_OK && zeroed == 0);
    assert(moo_buffer_append_scalar(b, MOO_BUFFER_U32, &x) == MOO_BUFFER_TYPE);
    assert(moo_buffer_len(b) == 3);
    assert(moo_buffer_pin(b, &pin, &data) == MOO_BUFFER_OK);
    assert(moo_buffer_append_scalar(b, MOO_BUFFER_I32, data) == MOO_BUFFER_PINNED);
    moo_buffer_unpin(pin);
    moo_buffer_drop(b);
    assert(moo_buffer_new_scalar(MOO_BUFFER_U32, 1, &b) == MOO_BUFFER_OK);
    y = 99;
    assert(moo_buffer_get_scalar(b, MOO_BUFFER_I32, 0, &y) == MOO_BUFFER_TYPE && y == 99);
    assert(moo_buffer_set_scalar(b, MOO_BUFFER_U32, 0, &x) == MOO_BUFFER_OK);
    float wrong = 0.0f;
    assert(moo_buffer_set_scalar(b, MOO_BUFFER_F32, 0, &wrong) == MOO_BUFFER_TYPE);
    assert(moo_buffer_get_scalar(b, MOO_BUFFER_U32, 0, &y) == MOO_BUFFER_OK && y == x);
    y = 99;
    assert(moo_buffer_get_scalar(b, MOO_BUFFER_U32, 1, &y) == MOO_BUFFER_BOUNDS && y == 99);
    moo_buffer_drop(b);
    assert(moo_buffer_new_scalar(MOO_BUFFER_SCALAR_NONE, 1, &b) == MOO_BUFFER_INVALID && !b);
    assert(moo_buffer_new(sizeof(x), _Alignof(uint32_t), 1, &b) == MOO_BUFFER_OK);
    assert(moo_buffer_scalar_type(NULL) == MOO_BUFFER_SCALAR_NONE);
    assert(moo_buffer_scalar_type(b) == MOO_BUFFER_SCALAR_NONE);
    assert(moo_buffer_get_scalar(b, MOO_BUFFER_U32, 0, &y) == MOO_BUFFER_TYPE && y == 99);
    moo_buffer_drop(b);
    return 0;
}
