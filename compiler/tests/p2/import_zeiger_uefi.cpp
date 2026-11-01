using u16 = unsigned short;
using u64 = unsigned long long;
extern "C" u16 lesen(const u16*) asm("__moo_native.p2_modul_zeiger_u16");
static void marker(const char* text) {
    while (*text) __asm__ volatile("outb %0, $0xe9" : : "a"(*text++));
}
extern "C" u64 efi_main(void*, void*) {
    const u16 a = 0x1234, b = 0xfedc;
    if (lesen(&a) != a || lesen(&b) != b) {
        marker("P2-IMPORT-UEFI-FAIL\n");
        return 1;
    }
    marker("P2-IMPORT-UEFI-PASS\n");
    return 0;
}
