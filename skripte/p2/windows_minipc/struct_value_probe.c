/* Unabhaengige Win64-C-Referenz: echte Moo -> C Struct-by-value Calls.
 * Die C-Callees kommen direkt aus skripte/p2/abi/struct_by_value.c.
 * Anders als struct_by_value_test.c stammen hier die Aufrufer aus Moo.
 */
#include <stdint.h>
#include <stdio.h>

extern uint64_t sys_c_abi4(uint64_t);
extern uint64_t sys_c_abi8(uint64_t);
extern uint64_t sys_c_abi12(uint64_t);
extern uint64_t sys_c_abi16(uint64_t);
extern uint64_t sys_c_abi24(uint64_t);

static unsigned pruefungen = 0;
static unsigned fehler = 0;

/** Vergleicht einen konkreten Rueckgabewert oder ein Speicherfeld. */
static void pruefe(const char *name, uint64_t ist, uint64_t soll) {
    ++pruefungen;
    if (ist != soll) {
        ++fehler;
        fprintf(stderr, "FAIL %s: %llu != %llu\n",
                name, (unsigned long long)ist, (unsigned long long)soll);
    }
}

int main(void) {
    uint32_t vier = 0;
    uint32_t acht[2] = {0};
    uint32_t zwoelf[3] = {0};
    uint32_t sechzehn[4] = {0};
    uint32_t vierundzwanzig[6] = {0};

    pruefe("4B-result", sys_c_abi4((uint64_t)(uintptr_t)&vier), 18);
    pruefe("4B-data", vier, 18);

    pruefe("8B-result", sys_c_abi8((uint64_t)(uintptr_t)acht), 36);
    pruefe("8B-word0", acht[0], 12);
    pruefe("8B-word1", acht[1], 24);

    pruefe("12B-result", sys_c_abi12((uint64_t)(uintptr_t)zwoelf), 66);
    pruefe("12B-word0", zwoelf[0], 11);
    pruefe("12B-word1", zwoelf[1], 22);
    pruefe("12B-word2", zwoelf[2], 33);

    pruefe("16B-result", sys_c_abi16((uint64_t)(uintptr_t)sechzehn), 20);
    pruefe("16B-word0", sechzehn[0], 2);
    pruefe("16B-word1", sechzehn[1], 4);
    pruefe("16B-word2", sechzehn[2], 6);
    pruefe("16B-word3", sechzehn[3], 8);

    pruefe("24B-result", sys_c_abi24((uint64_t)(uintptr_t)vierundzwanzig), 42);
    pruefe("24B-word0", vierundzwanzig[0], 2);
    pruefe("24B-word1", vierundzwanzig[1], 4);
    pruefe("24B-word2", vierundzwanzig[2], 6);
    pruefe("24B-word3", vierundzwanzig[3], 8);
    pruefe("24B-word4", vierundzwanzig[4], 10);
    pruefe("24B-word5", vierundzwanzig[5], 12);

    if (fehler) {
        fprintf(stderr, "MOO_WIN64_STRUCT_FAIL: %u/%u\n", fehler, pruefungen);
        return 1;
    }
    printf("MOO_WIN64_STRUCT_OK: %u/%u\n", pruefungen, pruefungen);
    return 0;
}
