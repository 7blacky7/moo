#include "callback_reference.h"
#include <stdio.h>
typedef struct { int32_t offset; unsigned calls; } Context;
static int compare(const void *left, const void *right) {
    int32_t a = *(const int32_t *)left, b = *(const int32_t *)right;
    return (a > b) - (a < b);
}
static int32_t visit(int32_t value, void *user_data) {
    Context *context = user_data;
    context->calls++;
    return value + context->offset;
}
int main(void) {
    int32_t values[] = {4, -2, 7, 4, 0};
    const int32_t expected[] = {-2, 0, 4, 4, 7};
    moo_p2_callback_sort(values, 5, compare);
    for (unsigned i = 0; i < 5; i++) if (values[i] != expected[i]) return 1;
    Context context = {5, 0};
    if (moo_p2_callback_visit(7, visit, &context) != 12 || context.calls != 1) return 2;
    puts("C qsort and synchronous user_data PASS");
    return 0;
}
