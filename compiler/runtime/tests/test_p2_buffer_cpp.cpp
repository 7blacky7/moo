#include "../moo_buffer.h"
#include "../moo_struct_box.h"
#include "../moo_struct_typed.h"
#include "../moo_defer_stack.h"
#include "../moo_defer_error.h"
#include <cstddef>
#include <cassert>
#include <cstdint>
#include <memory>

extern "C" int p2_buffer_cpp_adapter(int fail) noexcept {
    try {
        MooBuffer* raw = nullptr;
        if (moo_buffer_new_scalar(MOO_BUFFER_I32, 1, &raw) != MOO_BUFFER_OK) return -1;
        std::unique_ptr<MooBuffer, decltype(&moo_buffer_drop)> owner(raw, moo_buffer_drop);
        std::int32_t input = 42, output = 0;
        if (moo_buffer_set_scalar(owner.get(), MOO_BUFFER_I32, 0, &input) != MOO_BUFFER_OK) return -1;
        if (moo_buffer_append_scalar(owner.get(), MOO_BUFFER_I32, &input) != MOO_BUFFER_OK) return -1;
        if (fail) throw 7;
        if (moo_buffer_get_scalar(owner.get(), MOO_BUFFER_I32, 1, &output) != MOO_BUFFER_OK) return -1;
        return output;
    } catch (...) {
        return -2;
    }
}

struct P2Record { std::uint8_t a; std::uint32_t b; };
static const MooStructField p2_fields[] = {
    {"a", offsetof(P2Record, a), sizeof(std::uint8_t)},
    {"b", offsetof(P2Record, b), sizeof(std::uint32_t)}
};
static const MooStructDescriptor p2_desc = {"P2Record", sizeof(P2Record), alignof(P2Record), 2, p2_fields};

extern "C" int p2_struct_cpp_adapter(int fail) noexcept {
    try {
        P2Record input{7, 42};
        MooStructBox* raw = nullptr;
        if (moo_struct_box_new(&p2_desc, &input, &raw) != MOO_STRUCT_OK) return -1;
        std::unique_ptr<MooStructBox, decltype(&moo_struct_box_drop)> owner(raw, moo_struct_box_drop);
        input.b = 9;
        if (fail) throw 7;
        std::uint32_t output = 0;
        if (moo_struct_box_get(owner.get(), "b", &output, sizeof(output)) != MOO_STRUCT_OK) return -1;
        return static_cast<int>(output);
    } catch (...) {
        return -2;
    }
}

struct P2I32Pair { std::int32_t left, right; };
static const MooStructField p2_i32_fields[] = {
    {"left", offsetof(P2I32Pair, left), sizeof(std::int32_t)},
    {"right", offsetof(P2I32Pair, right), sizeof(std::int32_t)}
};
static const MooStructDescriptor p2_i32_base = {
    "P2I32Pair", sizeof(P2I32Pair), alignof(P2I32Pair), 2, p2_i32_fields
};
static const MooBufferScalarType p2_i32_types[] = {MOO_BUFFER_I32, MOO_BUFFER_I32};
static const MooStructTypedDescriptor p2_i32_desc = {
    MOO_STRUCT_TYPED_VERSION, &p2_i32_base, 2, p2_i32_types
};

extern "C" int p2_typed_struct_cpp_adapter(int fail) noexcept {
    try {
        P2I32Pair input{-5, 42};
        MooValue boxed{MOO_NONE, 0};
        if (moo_struct_typed_i32_new(&p2_i32_desc, &input, &boxed) != MOO_STRUCT_OK) return -1;
        auto raw = static_cast<MooStructBox*>(moo_val_as_ptr(boxed));
        std::unique_ptr<MooStructBox, decltype(&moo_struct_box_drop)> owner(raw, moo_struct_box_drop);
        if (fail) throw 7;
        std::int32_t output = 0;
        if (moo_struct_typed_i32_get(boxed, &p2_i32_desc, 1, &output) != MOO_STRUCT_OK) return -1;
        return output;
    } catch (...) {
        return -2;
    }
}

extern "C" int p2_defer_cpp_adapter(int fail) noexcept {
    try {
        MooDeferStack* raw = nullptr;
        if (moo_defer_stack_new(&raw) != MOO_DEFER_OK) return -1;
        std::unique_ptr<MooDeferStack, decltype(&moo_defer_stack_free)> owner(raw, moo_defer_stack_free);
        if (moo_defer_stack_push(owner.get(), 42) != MOO_DEFER_OK) return -1;
        if (fail) throw 7;
        std::uint32_t result = 0;
        if (moo_defer_stack_pop(owner.get(), &result) != MOO_DEFER_OK) return -1;
        return static_cast<int>(result);
    } catch (...) {
        return -2;
    }
}

extern "C" int p2_defer_oom_cpp_adapter() noexcept {
    MooValue value{MOO_NONE, 0};
    if (moo_defer_oom_value(&value) != MOO_DEFER_OK || value.tag != MOO_ERROR) return -1;
    const auto* error = static_cast<const MooError*>(moo_val_as_ptr(value));
    return error && error->refcount == 0 && error->chars[0] == 'D' ? 0 : -1;
}

int main() {
    assert(p2_buffer_cpp_adapter(0) == 42);
    assert(p2_buffer_cpp_adapter(1) == -2);
    assert(p2_struct_cpp_adapter(0) == 42);
    assert(p2_struct_cpp_adapter(1) == -2);
    assert(p2_typed_struct_cpp_adapter(0) == 42);
    assert(p2_typed_struct_cpp_adapter(1) == -2);
    assert(p2_defer_cpp_adapter(0) == 42);
    assert(p2_defer_cpp_adapter(1) == -2);
    assert(p2_defer_oom_cpp_adapter() == 0);
    MooValue status_value{MOO_NONE, 0};
    assert(moo_defer_status_value(MOO_DEFER_OVERFLOW, &status_value) == MOO_DEFER_OK);
    assert(status_value.tag == MOO_ERROR);
    const auto* status_error = static_cast<const MooError*>(moo_val_as_ptr(status_value));
    assert(status_error && status_error->refcount == 0 && status_error->chars[0] == 'D');
    return 0;
}
