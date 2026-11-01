#include <stdint.h>
extern int puts(const char *);
double p2_u32_nan(void) { return __builtin_nan(""); }
static uint32_t p2_u32_recorded;
void p2_u32_record(uint32_t value) { p2_u32_recorded = value; }
uint32_t p2_u32_read(void) { return p2_u32_recorded; }
uint32_t p2_u32_identity(uint32_t value) { return value; }
uint32_t p2_u32_mixed(uint32_t value, float factor) { return factor == 1.5f ? value : 0; }
uint32_t p2_u32_guard(uint32_t value) { puts("C_GUARD_AUFGERUFEN"); return value; }
