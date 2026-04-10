#include "moo_runtime.h"

void* moo_alloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "moo: Speicher voll!\n");
        exit(1);
    }
    return ptr;
}

void* moo_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        fprintf(stderr, "moo: Speicher voll!\n");
        exit(1);
    }
    return new_ptr;
}

void moo_free(void* ptr) {
    free(ptr);
}
