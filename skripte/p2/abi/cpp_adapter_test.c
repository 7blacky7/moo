#include "cpp_adapter.h"
#include <stdint.h>
#include <stdio.h>

static int failures;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); ++failures; } } while (0)

int main(void) {
    MooP2CppHandle *handle = NULL;
    int32_t values[] = { 5, 7, 11 };
    int64_t result = 0;
    CHECK(moo_p2_cpp_create(values, 3, &handle) == MOO_P2_CPP_OK);
    CHECK(handle != NULL);
    values[0] = 999; /* create copies the borrowed input */
    CHECK(moo_p2_cpp_sum(handle, &result) == MOO_P2_CPP_OK && result == 23);
    CHECK(moo_p2_cpp_destroy(&handle) == MOO_P2_CPP_OK && handle == NULL);
    CHECK(moo_p2_cpp_destroy(&handle) == MOO_P2_CPP_INVALID);
    CHECK(moo_p2_cpp_create(NULL, 1, &handle) == MOO_P2_CPP_INVALID && handle == NULL);
    CHECK(moo_p2_cpp_create(values, 1025, &handle) == MOO_P2_CPP_INVALID && handle == NULL);
    CHECK(moo_p2_cpp_create(NULL, 0, &handle) == MOO_P2_CPP_OK);
    CHECK(moo_p2_cpp_sum(handle, &result) == MOO_P2_CPP_OK && result == 0);
    CHECK(moo_p2_cpp_destroy(&handle) == MOO_P2_CPP_OK);
    int32_t throws[] = { 5, -1 };
    CHECK(moo_p2_cpp_create(throws, 2, &handle) == MOO_P2_CPP_OK);
    result = 123;
    CHECK(moo_p2_cpp_sum(handle, &result) == MOO_P2_CPP_EXCEPTION && result == 123);
    CHECK(moo_p2_cpp_destroy(&handle) == MOO_P2_CPP_OK);
    if (failures) return 1;
    puts("P2 C++ adapter: PASS");
    return 0;
}
