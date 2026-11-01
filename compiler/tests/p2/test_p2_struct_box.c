#include "../../runtime/moo_struct_box.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct { uint8_t a; uint32_t b; } Record;
static const MooStructField fields[] = {
    {"a", offsetof(Record, a), sizeof(uint8_t)},
    {"b", offsetof(Record, b), sizeof(uint32_t)}
};
static const MooStructDescriptor record = {"Record", sizeof(Record), _Alignof(Record), 2, fields};
static const MooStructDescriptor other = {"Record", sizeof(Record), _Alignof(Record), 2, fields};

int main(void) {
    Record input = {7, 42}, result;
    MooStructBox* box = NULL;
    uint32_t n = 0, replacement = 99;
    assert(moo_struct_box_new(&record, &input, &box) == MOO_STRUCT_OK);
    input.b = 1;
    assert(moo_struct_box_get(box, "b", &n, sizeof(n)) == MOO_STRUCT_OK && n == 42);
    assert(moo_struct_box_set(box, "b", &replacement, sizeof(replacement)) == MOO_STRUCT_OK);
    assert(moo_struct_box_get(box, "missing", &n, sizeof(n)) == MOO_STRUCT_FIELD);
    assert(moo_struct_box_get(box, "b", &n, 1) == MOO_STRUCT_FIELD);
    assert(moo_struct_box_copy(box, &other, &result) == MOO_STRUCT_TYPE);
    memset(&result, 0xff, sizeof(result));
    assert(moo_struct_box_copy(box, &record, &result) == MOO_STRUCT_OK);
    assert(result.a == 7 && result.b == 99);
    for (size_t i = sizeof(result.a); i < offsetof(Record, b); ++i)
        assert(((unsigned char*)&result)[i] == 0); /* padding is not copied */
    assert(moo_struct_box_retain(box) == MOO_STRUCT_OK);
    moo_struct_box_drop(box);
    assert(moo_struct_box_get(box, "b", &n, sizeof(n)) == MOO_STRUCT_OK && n == 99);
    moo_struct_box_drop(box);

    MooStructField bad = {"x", sizeof(Record) - 1, 2};
    MooStructDescriptor invalid = {"bad", sizeof(Record), _Alignof(Record), 1, &bad};
    box = NULL;
    assert(moo_struct_box_new(&invalid, &input, &box) == MOO_STRUCT_INVALID && !box);
    MooStructField duplicate[] = {{"x", 0, 1}, {"x", 1, 1}};
    invalid.fields = duplicate;
    invalid.field_count = 2;
    assert(moo_struct_box_new(&invalid, &input, &box) == MOO_STRUCT_INVALID && !box);
    MooStructField overlap[] = {{"x", 0, 2}, {"y", 1, 1}};
    invalid.fields = overlap;
    assert(moo_struct_box_new(&invalid, &input, &box) == MOO_STRUCT_INVALID && !box);
    return 0;
}
