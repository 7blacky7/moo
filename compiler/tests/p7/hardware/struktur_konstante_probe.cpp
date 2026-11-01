// P7-T4 1A: C++-Gegenprobe fuer struktur_konstante.moos (x86_64 SysV, gleiche
// Ziel-ABI wie x86_64-unknown-none): sizeof/alignof/offsetof gegen die von Moo
// gefalteten Layoutabfragen und Bytes/Felder der nativen Konstanten.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
using u64 = std::uint64_t;
struct __attribute__((packed)) Mb2Kopf { std::uint32_t magie, arch, laenge, pruefsumme; };
struct __attribute__((packed)) Gepackt { std::uint8_t a; std::uint32_t b; std::uint16_t c; };
struct Guid { std::uint32_t d1; std::uint16_t d2, d3; std::uint8_t d4[8]; };
struct Gemischt { std::uint8_t a; u64 b; std::uint16_t c; Guid g; std::uint8_t* zeiger; u64 adresse; std::int16_t neg; };

extern "C" const Mb2Kopf mb2_kopf;
extern "C" const Gepackt gepackt;
extern "C" const Guid gop_guid;
extern "C" const Gemischt gemischt;
extern "C" const std::uint16_t tabelle[4];
extern "C" u64 groesse_gemischt() asm("__moo_native.lay_groesse_gemischt");
extern "C" u64 ausrichtung_gemischt() asm("__moo_native.lay_ausrichtung_gemischt");
extern "C" u64 versatz_b() asm("__moo_native.lay_versatz_b");
extern "C" u64 versatz_g() asm("__moo_native.lay_versatz_g");
extern "C" u64 versatz_adresse() asm("__moo_native.lay_versatz_adresse");
extern "C" u64 groesse_gepackt() asm("__moo_native.lay_groesse_gepackt");
extern "C" u64 ausrichtung_gepackt() asm("__moo_native.lay_ausrichtung_gepackt");
extern "C" u64 versatz_gepackt_c() asm("__moo_native.lay_versatz_gepackt_c");
extern "C" u64 groesse_guid() asm("__moo_native.lay_groesse_guid");
extern "C" std::int32_t plus_kopf(std::int32_t) asm("__moo_native.lay_plus_kopf");

#define PRUEFE(bed, code) do { if (!(bed)) { std::printf("FAIL %s\n", #bed); return code; } } while (0)

int main() {
    PRUEFE(groesse_gemischt() == sizeof(Gemischt), 1);
    PRUEFE(ausrichtung_gemischt() == alignof(Gemischt), 2);
    PRUEFE(versatz_b() == offsetof(Gemischt, b), 3);
    PRUEFE(versatz_g() == offsetof(Gemischt, g), 4);
    PRUEFE(versatz_adresse() == offsetof(Gemischt, adresse), 5);
    PRUEFE(groesse_gepackt() == sizeof(Gepackt) && sizeof(Gepackt) == 7, 6);
    PRUEFE(ausrichtung_gepackt() == alignof(Gepackt) && alignof(Gepackt) == 1, 7);
    PRUEFE(versatz_gepackt_c() == offsetof(Gepackt, c), 8);
    PRUEFE(groesse_guid() == sizeof(Guid), 9);
    PRUEFE(plus_kopf(4) == 20, 10);
    PRUEFE(mb2_kopf.magie == 0xE85250D6u && mb2_kopf.arch == 0 && mb2_kopf.laenge == sizeof(Mb2Kopf), 11);
    PRUEFE(std::uint32_t(mb2_kopf.magie + mb2_kopf.arch + mb2_kopf.laenge + mb2_kopf.pruefsumme) == 0, 12);
    const Gepackt gepackt_c = {0x11, 0x22334455u, 0x6677};
    PRUEFE(std::memcmp(&gepackt, &gepackt_c, sizeof(Gepackt)) == 0, 13);
    const Guid gop_c = {0x9042a9deu, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
    PRUEFE(std::memcmp(&gop_guid, &gop_c, sizeof(Guid)) == 0, 14);
    PRUEFE(gemischt.a == 0xAB && gemischt.b == ~u64(0) && gemischt.c == 0x1234, 15);
    const Guid inner_c = {1, 2, 3, {4, 5, 6, 7, 8, 9, 10, 11}};
    PRUEFE(std::memcmp(&gemischt.g, &inner_c, sizeof(Guid)) == 0, 16);
    PRUEFE(gemischt.zeiger == reinterpret_cast<const std::uint8_t*>(&mb2_kopf), 17);
    PRUEFE(gemischt.adresse == reinterpret_cast<u64>(&gop_guid), 18);
    PRUEFE(gemischt.neg == -2, 19);
    PRUEFE(tabelle[0] == 1 && tabelle[1] == 2 && tabelle[2] == 0xFFFF && tabelle[3] == sizeof(Mb2Kopf), 20);
    std::puts("PASS native Konstanten und Layoutabfragen == C++ sizeof/alignof/offsetof");
}
