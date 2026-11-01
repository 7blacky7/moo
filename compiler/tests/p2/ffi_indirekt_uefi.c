typedef unsigned char u8;
typedef signed char i8;
typedef short i16;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef u64 (*fn10)(u8*, u8*, u32, u64, u64, u64, u64, u64, u64, u64);
extern u64 p2_indirekt_zehn(fn10, u8*, u8*, u32, u64, u64, u64, u64, u64, u64, u64) __asm__("__moo_native.p2_indirekt_zehn");
extern i8 p2_i8(i8 (*)(i8), i8) __asm__("__moo_native.p2_indirekt_i8");
extern u8 p2_u8(u8 (*)(u8), u8) __asm__("__moo_native.p2_indirekt_u8");
extern i16 p2_i16(i16 (*)(i16), i16) __asm__("__moo_native.p2_indirekt_i16");
extern u16 p2_u16(u16 (*)(u16), u16) __asm__("__moo_native.p2_indirekt_u16");
static i8 call_i8(i8 x) { return x; }
static u8 call_u8(u8 x) { return x; }
static i16 call_i16(i16 x) { return x; }
static u16 call_u16(u16 x) { return x; }
extern void p2_void(void (*)(int), int) __asm__("__moo_native.p2_indirekt_void");
static int observed;
static void record(int value) { observed = value; }
static u64 target(u8* a, u8* b, u32 op, u64 x1, u64 x2, u64 x3, u64 x4, u64 x5, u64 x6, u64 x7) {
    if (*a != 2 || *b != 3 || op != 4 || x1 != 0x8000000000000000ULL ||
        x2 != 2 || x3 != 3 || x4 != 4 || x5 != 5 || x6 != 6 || x7 != 7) return 0;
    return 0x8000000000000024ULL;
}
static void marker(const char* s) {
    while (*s) { __asm__ volatile("outb %0, $0xe9" : : "a"(*s++)); }
}
u64 efi_main(void* image, void* system) {
    (void)image; (void)system;
    u8 a = 2, b = 3;
    const u64 high = 0x8000000000000000ULL;
    u64 result = p2_indirekt_zehn(target, &a, &b, 4, high, 2, 3, 4, 5, 6, 7);
    p2_void(record, -12345);
    if (observed != -12345) { marker("P2-UEFI-INDIRECT-FAIL\n"); return 1; }
    p2_void(record, 67890);
    if (observed != 67890) { marker("P2-UEFI-INDIRECT-FAIL\n"); return 1; }
    if (result != high + 36 || p2_i8(call_i8, -100) != -100 || p2_u8(call_u8, 250) != 250 ||
        p2_i16(call_i16, -30000) != -30000 || p2_u16(call_u16, 60000) != 60000) {
        marker("P2-UEFI-INDIRECT-FAIL\n"); return 1;
    }
    marker("P2-UEFI-INDIRECT-PASS\n");
    return 0;
}
