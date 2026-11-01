#include <cstdint>
#include <cstdio>
using Funktion = std::int32_t (*)(std::int32_t);
extern "C" std::int32_t ffi_rufe_indirekt(Funktion, std::int32_t) asm("__moo_native.ffi_rufe_indirekt");
using FirmwareFunktion = std::uint64_t (*)(std::uint8_t*, std::uint16_t*);
extern "C" std::uint64_t ffi_rufe_firmware(FirmwareFunktion, std::uint8_t*, std::uint16_t*) asm("__moo_native.ffi_rufe_firmware");
extern "C" std::uint64_t p2_firmware(std::uint8_t* kontext, std::uint16_t* text) { return *kontext + text[0]; }
using SignedFunktion = std::int8_t (*)(std::int8_t);
using UnsignedFunktion = std::uint8_t (*)(std::uint8_t);
extern "C" std::int8_t ffi_rufe_i8(SignedFunktion, std::int8_t) asm("__moo_native.ffi_rufe_i8");
extern "C" std::uint8_t ffi_rufe_u8(UnsignedFunktion, std::uint8_t) asm("__moo_native.ffi_rufe_u8");
extern "C" std::int8_t p2_i8(std::int8_t wert) { return wert; }
extern "C" std::uint8_t p2_u8(std::uint8_t wert) { return wert; }
using VoidFunktion = void (*)(std::int32_t);
extern "C" void ffi_rufe_void(VoidFunktion, std::int32_t) asm("__moo_native.ffi_rufe_void");
static std::int32_t beobachtet = 0;
extern "C" void p2_aufzeichnen(std::int32_t wert) { beobachtet = wert; }
struct P2FfiPaket { std::int32_t wert; };
using PaketFunktion = std::int32_t (*)(P2FfiPaket*);
extern "C" std::int32_t ffi_rufe_paket(PaketFunktion, P2FfiPaket*) asm("__moo_native.ffi_rufe_paket");
extern "C" std::int32_t p2_paket(P2FfiPaket* paket) { return paket->wert + 1; }
using ZehnFunktion = std::uint64_t (*)(std::uint8_t*, std::uint8_t*, std::uint32_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
extern "C" std::uint64_t ffi_rufe_zehn(ZehnFunktion, std::uint8_t*, std::uint8_t*, std::uint32_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) asm("__moo_native.ffi_rufe_zehn");
extern "C" std::uint64_t p2_zehn(std::uint8_t* a, std::uint8_t* b, std::uint32_t op, std::uint64_t x1, std::uint64_t x2, std::uint64_t x3, std::uint64_t x4, std::uint64_t x5, std::uint64_t x6, std::uint64_t x7) {
    return *a + *b + op + x1 + x2 + x3 + x4 + x5 + x6 + x7;
}
extern "C" std::int32_t p2_doppelt(std::int32_t wert) { return wert * 2; }
extern "C" std::int32_t p2_addiere(std::int32_t wert) { return wert + 7; }
int main() {
    if (ffi_rufe_indirekt(&p2_doppelt, 13) != 26) return 1;
    if (ffi_rufe_indirekt(&p2_addiere, -3) != 4) return 2;
    std::uint8_t kontext = 5;
    std::uint16_t text[] = {77, 0};
    if (ffi_rufe_firmware(&p2_firmware, &kontext, text) != 82) return 3;
    if (ffi_rufe_i8(&p2_i8, -100) != -100) return 4;
    if (ffi_rufe_u8(&p2_u8, 250) != 250) return 5;
    ffi_rufe_void(&p2_aufzeichnen, 37);
    if (beobachtet != 37) return 6;
    P2FfiPaket paket{41};
    if (ffi_rufe_paket(&p2_paket, &paket) != 42) return 7;
    std::uint8_t a = 2, b = 3;
    if (ffi_rufe_zehn(&p2_zehn, &a, &b, 4, 1, 2, 3, 4, 5, 6, 7) != 37) return 8;
    std::puts("PASS indirekter Fremdaufruf aus Moo");
    return 0;
}
