#include <stdint.h>
extern int puts(const char *);
double p2_f64_nan(void) { return __builtin_nan(""); }
double p2_f64_inf(void) { return __builtin_inf(); }
int64_t p2_i64_identity(int64_t value) { return value; }
uint64_t p2_u64_identity(uint64_t value) { return value; }
int64_t p2_i64_inexact(void) { puts("C_RETURN_AUFGERUFEN"); return INT64_C(9007199254740993); }
uint64_t p2_u64_inexact(void) { puts("C_RETURN_AUFGERUFEN"); return UINT64_MAX; }
int64_t p2_i64_guard(int64_t value) { puts("C_GUARD_AUFGERUFEN"); return value; }
uint64_t p2_u64_guard(uint64_t value) { puts("C_GUARD_AUFGERUFEN"); return value; }
int32_t p2_u64_is_exact_literal(uint64_t value) { return value == UINT64_C(9007199254740993); }
int32_t p2_u64_is_max(uint64_t value) { return value == UINT64_MAX; }
int64_t p2_i64_high_exact(void) { return INT64_MAX - INT64_C(1023); }
uint64_t p2_u64_high_exact(void) { return UINT64_MAX - UINT64_C(2047); }
int64_t p2_i64_nearmin_inexact(void) { puts("C_RETURN_AUFGERUFEN"); return INT64_MIN + INT64_C(1); }
