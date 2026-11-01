/* Isolated dispatch test: unrelated destructors are fatal stubs. */
#include "../../runtime/moo_runtime.h"
#include "../../runtime/moo_buffer.h"
#include "../../runtime/moo_buffer_value.h"
#include "../../runtime/moo_struct_box.h"
#include "../../runtime/moo_struct_value.h"
#include "../../runtime/moo_struct_typed.h"
#include <assert.h>
#include <stdlib.h>

#define UNUSED_FREE(name) void name(void* ptr) { (void)ptr; abort(); }
UNUSED_FREE(moo_socket_free)
UNUSED_FREE(moo_thread_free)
UNUSED_FREE(moo_channel_free)
UNUSED_FREE(moo_db_free)
UNUSED_FREE(moo_db_stmt_free)
UNUSED_FREE(moo_web_free)
UNUSED_FREE(moo_frame_free)
UNUSED_FREE(moo_gif_handle_free)
UNUSED_FREE(moo_video_handle_free)
UNUSED_FREE(moo_tensor_free)
UNUSED_FREE(moo_kamera_free)
UNUSED_FREE(moo_mikro_free)

MooValue moo_error(const char* msg) { (void)msg; abort(); }
void moo_throw(MooValue value) { (void)value; abort(); }

void moo_test_buffer_value(void) {
    MooBuffer* b = NULL;
    MooBufferPin* pin = NULL;
    void* data = NULL;
    uint32_t value = 0x42u, result = 0;
    assert(moo_buffer_new(sizeof(value), _Alignof(uint32_t), 1, &b) == MOO_BUFFER_OK);
    assert(moo_buffer_set(b, 0, &value) == MOO_BUFFER_OK);
    MooValue wrapped = { MOO_BUFFER, 0 };
    moo_val_set_ptr(&wrapped, b);
    moo_retain(wrapped);
    moo_release(wrapped);
    assert(moo_buffer_get(b, 0, &result) == MOO_BUFFER_OK && result == value);
    assert(moo_buffer_pin(b, &pin, &data) == MOO_BUFFER_OK);
    moo_release(wrapped);
    assert(*(uint32_t*)data == value);
    moo_buffer_unpin(pin);

    /* Box owns +1; borrowing transfers nothing and a pin outlives the box. */
    MooValue typed = {MOO_BOOL, 1};
    assert(moo_buffer_value_new(MOO_BUFFER_U32, 1, NULL) == MOO_BUFFER_INVALID);
    assert(moo_buffer_value_new(MOO_BUFFER_U64, SIZE_MAX, &typed) == MOO_BUFFER_OVERFLOW);
    assert(typed.tag == MOO_NONE && typed.data == 0);
    assert(moo_buffer_value_new(MOO_BUFFER_SCALAR_NONE, 1, &typed) == MOO_BUFFER_INVALID);
    assert(typed.tag == MOO_NONE && typed.data == 0);
    assert(moo_buffer_value_new(MOO_BUFFER_U32, 1, &typed) == MOO_BUFFER_OK);
    assert(typed.tag == MOO_BUFFER);
    assert(moo_buffer_value_borrow(typed, MOO_BUFFER_U32, NULL) == MOO_BUFFER_INVALID);
    MooBuffer* borrowed = NULL;
    assert(moo_buffer_value_borrow(typed, MOO_BUFFER_U32, &borrowed) == MOO_BUFFER_OK && borrowed);
    assert(moo_buffer_value_borrow((MooValue){MOO_NONE, 0}, MOO_BUFFER_U32, &borrowed) == MOO_BUFFER_INVALID && !borrowed);
    assert(moo_buffer_value_borrow(typed, MOO_BUFFER_U32, &borrowed) == MOO_BUFFER_OK && borrowed);
    assert(moo_buffer_value_borrow(typed, MOO_BUFFER_I32, &borrowed) == MOO_BUFFER_TYPE && !borrowed);
    assert(moo_buffer_value_borrow(typed, MOO_BUFFER_U32, &borrowed) == MOO_BUFFER_OK && borrowed);
    assert(moo_buffer_set_scalar(borrowed, MOO_BUFFER_U32, 0, &value) == MOO_BUFFER_OK);
    moo_retain(typed);
    moo_release(typed);
    assert(moo_buffer_pin(borrowed, &pin, &data) == MOO_BUFFER_OK);
    moo_release(typed);
    assert(*(uint32_t*)data == value);
    moo_buffer_unpin(pin);

    /* A boxed view owns shared storage after its source has been released. */
    MooValue owner = {MOO_NONE, 0}, view = {MOO_NONE, 0};
    assert(moo_buffer_value_new(MOO_BUFFER_I32, 3, &owner) == MOO_BUFFER_OK);
    assert(moo_buffer_value_view(owner, MOO_BUFFER_I32, 0, 1, NULL) == MOO_BUFFER_INVALID);
    assert(moo_buffer_value_view(owner, MOO_BUFFER_U32, 0, 1, &view) == MOO_BUFFER_TYPE);
    assert(view.tag == MOO_NONE && view.data == 0);
    assert(moo_buffer_value_view(owner, MOO_BUFFER_I32, 3, 1, &view) == MOO_BUFFER_BOUNDS);
    assert(view.tag == MOO_NONE && view.data == 0);
    assert(moo_buffer_value_view((MooValue){MOO_NONE, 0}, MOO_BUFFER_I32, 0, 1, &view) == MOO_BUFFER_INVALID);
    assert(view.tag == MOO_NONE && view.data == 0);
    assert(moo_buffer_value_view(owner, MOO_BUFFER_I32, 1, 1, &view) == MOO_BUFFER_OK);
    assert(view.tag == MOO_BUFFER);
    MooBuffer* owner_borrow = NULL;
    MooBuffer* view_borrow = NULL;
    int32_t signed_value = -42, signed_result = 0;
    assert(moo_buffer_value_borrow(owner, MOO_BUFFER_I32, &owner_borrow) == MOO_BUFFER_OK);
    assert(moo_buffer_value_borrow(view, MOO_BUFFER_I32, &view_borrow) == MOO_BUFFER_OK);
    assert(moo_buffer_set_scalar(owner_borrow, MOO_BUFFER_I32, 1, &signed_value) == MOO_BUFFER_OK);
    assert(moo_buffer_get_scalar(view_borrow, MOO_BUFFER_I32, 0, &signed_result) == MOO_BUFFER_OK && signed_result == signed_value);
    assert(moo_buffer_resize(owner_borrow, 4) == MOO_BUFFER_PINNED);
    moo_release(owner);
    signed_result = 0;
    assert(moo_buffer_get_scalar(view_borrow, MOO_BUFFER_I32, 0, &signed_result) == MOO_BUFFER_OK && signed_result == signed_value);
    moo_release(view);

    /* Typed descriptors preserve nominal identity and validate all leaves. */
    struct Pair { int32_t a, b; };
    static const MooStructField pair_fields[] = {
        {"a", offsetof(struct Pair, a), sizeof(int32_t)},
        {"b", offsetof(struct Pair, b), sizeof(int32_t)}
    };
    static const MooStructDescriptor pair_desc = {
        "Pair", sizeof(struct Pair), _Alignof(struct Pair), 2, pair_fields
    };
    static const MooStructDescriptor other_pair = {
        "Pair", sizeof(struct Pair), _Alignof(struct Pair), 2, pair_fields
    };
    static const MooBufferScalarType pair_types[] = {MOO_BUFFER_I32, MOO_BUFFER_I32};
    static const MooBufferScalarType bad_types[] = {MOO_BUFFER_I32, MOO_BUFFER_U32};
    static const MooStructTypedDescriptor pair_type = {MOO_STRUCT_TYPED_VERSION, &pair_desc, 2, pair_types};
    static const MooStructTypedDescriptor other_type = {MOO_STRUCT_TYPED_VERSION, &other_pair, 2, pair_types};
    static const MooStructTypedDescriptor bad_type = {MOO_STRUCT_TYPED_VERSION, &pair_desc, 2, bad_types};
    static const MooStructTypedDescriptor bad_version = {99, &pair_desc, 2, pair_types};
    struct Pair original = {-7, 42};
    MooValue pair_value = {MOO_NONE, 0};
    assert(moo_struct_typed_i32_new(&bad_type, &original, &pair_value) == MOO_STRUCT_INVALID);
    assert(pair_value.tag == MOO_NONE);
    assert(moo_struct_typed_i32_new(&bad_version, &original, &pair_value) == MOO_STRUCT_INVALID);
    assert(moo_struct_typed_i32_new(&pair_type, &original, &pair_value) == MOO_STRUCT_OK);
    int32_t field_value = 0;
    assert(moo_struct_typed_i32_get(pair_value, &pair_type, 0, &field_value) == MOO_STRUCT_OK && field_value == -7);
    assert(moo_struct_typed_i32_get(pair_value, &other_type, 0, &field_value) == MOO_STRUCT_TYPE);
    assert(moo_struct_typed_i32_get(pair_value, &pair_type, 2, &field_value) == MOO_STRUCT_FIELD);
    assert(moo_struct_typed_i32_set(pair_value, &pair_type, 1, 17) == MOO_STRUCT_OK);
    assert(original.b == 42);
    assert(moo_struct_typed_i32_get(pair_value, &pair_type, 1, &field_value) == MOO_STRUCT_OK && field_value == 17);
    moo_release(pair_value);

    static const MooStructField fields[] = {{"value", 0, sizeof(uint32_t)}};
    static const MooStructDescriptor desc = {"Scalar", sizeof(uint32_t), _Alignof(uint32_t), 1, fields};
    MooStructBox* box = NULL;
    assert(moo_struct_box_new(&desc, &value, &box) == MOO_STRUCT_OK);
    MooValue sv = { MOO_STRUCT, 0 };
    moo_val_set_ptr(&sv, box);
    moo_retain(sv);
    moo_release(sv);
    result = 0;
    assert(moo_struct_box_get(box, "value", &result, sizeof(result)) == MOO_STRUCT_OK && result == value);
    moo_release(sv);

    /* Boxed structure copies the source and borrows only the exact type. */
    static const MooStructDescriptor other_desc = {"Scalar", sizeof(uint32_t), _Alignof(uint32_t), 1, fields};
    MooValue typed_struct = {MOO_BOOL, 1};
    assert(moo_struct_value_new(NULL, &value, &typed_struct) == MOO_STRUCT_INVALID);
    assert(typed_struct.tag == MOO_NONE && typed_struct.data == 0);
    assert(moo_struct_value_new(&desc, &value, &typed_struct) == MOO_STRUCT_OK);
    assert(typed_struct.tag == MOO_STRUCT);
    MooStructBox* borrowed_box = NULL;
    assert(moo_struct_value_borrow(typed_struct, &desc, &borrowed_box) == MOO_STRUCT_OK && borrowed_box);
    assert(moo_struct_value_borrow(typed_struct, &other_desc, &borrowed_box) == MOO_STRUCT_TYPE && !borrowed_box);
    assert(moo_struct_value_borrow((MooValue){MOO_NONE, 0}, &desc, &borrowed_box) == MOO_STRUCT_INVALID && !borrowed_box);
    assert(moo_struct_value_borrow(typed_struct, &desc, &borrowed_box) == MOO_STRUCT_OK && borrowed_box);
    uint32_t old_value = value;
    value = 9;
    result = 0;
    assert(moo_struct_box_get(borrowed_box, "value", &result, sizeof(result)) == MOO_STRUCT_OK && result == old_value);
    moo_retain(typed_struct);
    moo_release(typed_struct);
    assert(moo_struct_box_get(borrowed_box, "value", &result, sizeof(result)) == MOO_STRUCT_OK);
    moo_release(typed_struct);
}
