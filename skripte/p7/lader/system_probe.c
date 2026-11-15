/* P7-T5: Gegenprobe der Moo-Systemkoerper (skripte/p7/lader/system_koerper.moos),
 * kompiliert fuer x86_64-bare (SysV) und hier echt ausgefuehrt. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef uint64_t (__attribute__((ms_abi)) *dienst_t)(uint64_t, uint64_t);
extern uint64_t sys_summe(uint64_t);
extern uint32_t sys_tabelle(uint64_t);
extern uint64_t sys_text_summe(void);
extern uint64_t sys_rufe(dienst_t, uint64_t, uint64_t);
extern int64_t sys_konv(uint64_t);
extern uint32_t sys_zeiger(uint64_t);
extern uint64_t sys_struktur(uint64_t);
extern uint64_t sys_gerade(uint64_t);
extern uint64_t sys_asm(uint64_t, uint64_t);
extern void sys_klasse(uint64_t, uint64_t);
extern uint64_t sys_puffer_index(uint64_t);
extern uint64_t sys_strukturfeld(uint64_t);
extern uint32_t sys_registerfeld(uint64_t);
extern uint32_t sys_mmio(uint64_t, uint32_t);
extern uint32_t sys_gepacktes_feld(uint64_t);
extern uint64_t sys_feld_direkt(uint64_t);
struct moo_paar { uint32_t a; uint64_t b; };
extern uint64_t sys_feld_param(struct moo_paar *);
extern uint32_t sys_feld_gepackt(uint64_t);
extern uint64_t sys_feld_verschachtelt(uint64_t);
extern uint32_t sys_innen(uint64_t);
extern uint32_t sys_struktur_array(uint64_t);
extern uint32_t sys_link_feld(uint64_t);
extern uint32_t sys_packed_innen(uint64_t);
extern uint64_t sys_array_felder(uint64_t, uint64_t);
extern uint32_t sys_array_referenzen(uint64_t);
extern uint32_t sys_matrix(uint64_t, uint64_t, uint64_t);
extern uint32_t sys_matrix_zeiger(uint64_t);
extern uint64_t sys_adresse_matrix(uint64_t);
extern uint64_t sys_adresse_zeile(uint64_t);
extern uint64_t sys_adresse_punkt(uint64_t);
extern uint64_t sys_adresse_struktur(uint64_t);
struct native_wert { uint32_t code; uint64_t wert; };
struct native_block { uint8_t prefix; struct native_wert innen; uint16_t ende; };
_Static_assert(sizeof(struct native_wert) == 16, "C-ABI native Wert");
_Static_assert(offsetof(struct native_block, innen) == 8, "C-ABI native Block");
_Static_assert(offsetof(struct native_block, ende) == 24, "C-ABI native Ende");
extern uint64_t sys_value_copy(uint64_t);
extern uint64_t sys_value_nested(uint64_t);
extern uint64_t sys_value_pointer_copy(uint64_t);
extern uint64_t sys_value_local_address(void);
extern uint64_t sys_value_field_store(uint64_t);
extern uint64_t sys_value_field_copy(uint64_t);
extern uint64_t sys_value_call(uint64_t);
extern uint64_t sys_value_nested_call(uint64_t);
/* P2-T48 E3 (Plan a71947f7). */
struct e3_messung { uint32_t werte[4]; float skala; _Bool gueltig; uint16_t marke; };
_Static_assert(sizeof(struct e3_messung) == 24, "C-ABI E3Messung");
_Static_assert(offsetof(struct e3_messung, gueltig) == 20, "C-ABI E3Messung.gueltig");
_Static_assert(offsetof(struct e3_messung, marke) == 22, "C-ABI E3Messung.marke");
extern uint64_t sys_e3_arrays(uint64_t);
extern uint64_t sys_e3_null(uint64_t);
extern uint64_t sys_e3_float(uint64_t);
extern uint64_t sys_e3_struktur_array(uint64_t);
extern uint64_t sys_e3_overlap(uint64_t);
extern uint64_t sys_e3_alias(void);
struct e3_block { _Alignas(32) uint32_t a; uint8_t b; };
_Static_assert(sizeof(struct e3_block) == 32 && _Alignof(struct e3_block) == 32, "C-ABI E3Block");
extern uint64_t sys_e3_ausgerichtet(uint64_t);
extern uint64_t sys_c_abi4(uint64_t);
extern uint64_t sys_c_abi8(uint64_t);
extern uint64_t sys_c_abi12(uint64_t);
extern uint64_t sys_c_abi16(uint64_t);
extern uint64_t sys_c_abi24(uint64_t);
struct moo_feld_eintrag { uint16_t code; uint8_t flags; };
struct moo_matrix {
    uint8_t zaehler;
    uint16_t feld[3][4];
    struct moo_feld_eintrag punkte[2][2];
};
struct moo_matrix_pointer { struct moo_feld_eintrag *ziele[2][2]; };
_Static_assert(offsetof(struct moo_matrix, feld) == 2, "C-ABI Matrixdaten");
_Static_assert(offsetof(struct moo_matrix, punkte) == 26, "C-ABI Matrix-Strukturarray");
_Static_assert(sizeof(struct moo_matrix) == 42, "C-ABI Matrix-Groesse");
struct moo_arraylayout { uint8_t marker; uint32_t werte[4]; struct moo_feld_eintrag eintraege[3]; };
struct moo_referenztabelle { struct moo_feld_eintrag *referenzen[2]; };
_Static_assert(sizeof(struct moo_feld_eintrag) == 4, "C-ABI FeldEintrag");
_Static_assert(offsetof(struct moo_arraylayout, werte) == 4, "C-ABI array-field");
_Static_assert(offsetof(struct moo_arraylayout, eintraege) == 20, "C-ABI Array-Struct-Stride");
_Static_assert(sizeof(struct moo_arraylayout) == 32, "C-ABI ArrayLayout");


/* Unabhaengiger C-ABI-Vertrag zum nativen Moo-Strukturlayout. */
struct moo_inner { uint16_t code; uint8_t flags; };
struct moo_geraet { uint8_t kennung; struct moo_inner innen; uint32_t ende; };
struct moo_verweis { struct moo_inner *ziel; uint32_t nummer; };
struct __attribute__((packed)) moo_gepackt { uint8_t prefix; struct moo_inner innen; uint16_t ende; };
_Static_assert(sizeof(struct moo_inner) == 4, "C-ABI InnereRegister");
_Static_assert(sizeof(struct moo_geraet) == 12, "C-ABI GeraeteEintrag");
_Static_assert(offsetof(struct moo_geraet, innen) == 2, "C-ABI Feld innen");
_Static_assert(offsetof(struct moo_geraet, ende) == 8, "C-ABI Feld ende");
_Static_assert(offsetof(struct moo_verweis, nummer) == 8, "C-ABI Feld nummer");
_Static_assert(offsetof(struct moo_gepackt, innen) == 1, "C-ABI Packed innen");

/* Firmwaredienst im MS-x64-ABI: schreibt a*7 in den Ausgabeparameter. */
static uint64_t __attribute__((ms_abi)) dienst(uint64_t a, uint64_t aus) {
    *(uint64_t *)(uintptr_t)aus = a * 7u;
    return 1u;
}

static int fehler;
static void pruefe(const char *was, int64_t ist, int64_t soll) {
    if (ist != soll) { printf("FEHLER %s: ist %lld, soll %lld\n", was, (long long)ist, (long long)soll); fehler = 1; }
}

int main(void) {
    uint32_t feld[2] = {5u, 0u};
    uint64_t paar[2] = {0u, 0u};
    uint8_t klasse = 0;
    pruefe("summe", (int64_t)sys_summe(10), 165);
    pruefe("summe0", (int64_t)sys_summe(0), 0);
    pruefe("tabelle2", sys_tabelle(2), 30);
    pruefe("tabelle9", sys_tabelle(9), 0);
    /* "MOO-OK" = 434, "\\EFI" + 0 = 304 */
    pruefe("text", (int64_t)sys_text_summe(), 738);
    pruefe("rufe", (int64_t)sys_rufe(dienst, 5, 3), 74);
    pruefe("konv", sys_konv(0x1FFu), -1);
    pruefe("konv2", sys_konv(0x17Fu), 127);
    pruefe("zeiger", sys_zeiger((uint64_t)(uintptr_t)feld), 82);
    pruefe("zeiger-feld", feld[1], 77);
    pruefe("struktur", (int64_t)sys_struktur((uint64_t)(uintptr_t)paar), 115);
    pruefe("struktur-feld", (int64_t)paar[1], 99);
    pruefe("gerade", (int64_t)sys_gerade(10), 5);
    pruefe("asm", (int64_t)sys_asm(4, 5), 10);
    sys_klasse((uint64_t)(uintptr_t)&klasse, 3); pruefe("klasse3", klasse, 1);
    sys_klasse((uint64_t)(uintptr_t)&klasse, 5); pruefe("klasse5", klasse, 2);
    sys_klasse((uint64_t)(uintptr_t)&klasse, 50); pruefe("klasse50", klasse, 3);
    /* P2: typisierter RAM-Zeiger und ABI-Feldzugriffe (Layout aus Moo). */
    uint32_t puffer[2] = {5u, 0u};
    uint64_t struktur[2] = {0u, 0u};
    pruefe("puffer-index", (int64_t)sys_puffer_index((uint64_t)(uintptr_t)puffer), 4665);
    pruefe("puffer-index-speicher", puffer[1], 4660);
    pruefe("strukturfeld", (int64_t)sys_strukturfeld((uint64_t)(uintptr_t)struktur), 139);
    pruefe("strukturfeld-speicher", (int64_t)struktur[1], 123);
    pruefe("registerfeld", sys_registerfeld((uint64_t)(uintptr_t)struktur), 71);
    {
        /* P2-T17 D1: speichergestuetztes Register, volatile geschrieben/gelesen. */
        volatile uint32_t reg = 0;
        pruefe("mmio", sys_mmio((uint64_t)(uintptr_t)&reg, 21), 42);
        pruefe("mmio-register", reg, 21);
    }
    pruefe("registerfeld-speicher", (int64_t)(uint32_t)struktur[0], 71);
    uint8_t gepacktes[5] = {0};
    pruefe("gepacktes-feld", sys_gepacktes_feld((uint64_t)(uintptr_t)gepacktes), 0x10203040);
    pruefe("gepackt-marke", gepacktes[0], 0);
    pruefe("gepackt-low", gepacktes[1], 0x40);
    pruefe("gepackt-high", gepacktes[4], 0x10);
    struct moo_paar direkt = {7u, 0u};
    pruefe("feld-direkt", (int64_t)sys_feld_direkt((uint64_t)(uintptr_t)&direkt), 229);
    pruefe("feld-direkt-mem", (int64_t)direkt.b, 222);
    pruefe("feld-param", (int64_t)sys_feld_param(&direkt), 340);
    pruefe("feld-param-mem", (int64_t)direkt.b, 333);
    uint8_t direkt_gepackt[5] = {0};
    pruefe("feld-gepackt", sys_feld_gepackt((uint64_t)(uintptr_t)direkt_gepackt), 0xA1B2C3D4);
    pruefe("feld-gepackt-marke", direkt_gepackt[0], 0);
    pruefe("feld-gepackt-low", direkt_gepackt[1], 0xD4);
    pruefe("feld-gepackt-high", direkt_gepackt[4], 0xA1);
    struct moo_paar *zeiger = &direkt;
    pruefe("feld-verschachtelt", (int64_t)sys_feld_verschachtelt((uint64_t)(uintptr_t)&zeiger), 333);

    /* P2-T48: verschachtelte Strukturen, native Array-Strides und Pointerfelder. */
    struct moo_geraet eintrag = {0};
    pruefe("innen-rueckgabe", sys_innen((uint64_t)(uintptr_t)&eintrag), 4665);
    pruefe("innen-code", eintrag.innen.code, 0x1234);
    pruefe("innen-flags", eintrag.innen.flags, 5);

    struct moo_geraet array[3] = {{0}};
    pruefe("array-rueckgabe", sys_struktur_array((uint64_t)(uintptr_t)array), 1332);
    pruefe("array-index-eins", array[1].ende, 555);
    pruefe("array-index-zwei", array[2].innen.code, 777);
    pruefe("array-index-null", array[0].ende, 0);

    struct moo_inner ziel = {1, 0};
    struct moo_verweis verweis = {&ziel, 9};
    pruefe("pointerfeld-rueckgabe", sys_link_feld((uint64_t)(uintptr_t)&verweis), 53);
    pruefe("pointerfeld-ziel", ziel.flags, 44);
    pruefe("pointerfeld-nummer", verweis.nummer, 9);

    uint8_t bytefeld[sizeof(struct moo_gepackt)] = {0};
    pruefe("packed-innen-rueckgabe", sys_packed_innen((uint64_t)(uintptr_t)bytefeld), 0xBEEF);
    pruefe("packed-prefix", bytefeld[0], 0);
    pruefe("packed-niederwert", bytefeld[1], 0xEF);
    pruefe("packed-oberwert", bytefeld[2], 0xBE);

    /* Spaeterer Meilenstein-Test fuer die nativen Array-Feldzugriffe. */
    struct moo_arraylayout block = {0};
    pruefe("array-rueckgabe", (int64_t)sys_array_felder((uint64_t)(uintptr_t)&block, 2), 483);
    pruefe("array-element2", block.werte[2], 70);
    pruefe("array-element0", block.werte[0], 0);
    pruefe("array-struct1", block.eintraege[1].code, 404);
    pruefe("array-struct2", block.eintraege[2].flags, 9);
    pruefe("array-marker", block.marker, 0);
    struct moo_feld_eintrag referenziert = {0};
    struct moo_referenztabelle tabelle = {{0, &referenziert}};
    pruefe("array-zeiger-rueckgabe", sys_array_referenzen((uint64_t)(uintptr_t)&tabelle), 17);
    pruefe("array-zeiger-speicher", referenziert.flags, 17);
    /* Mehrdimensionale native Arrayfelder im selben ABI wie die C-Referenz. */
    struct moo_matrix matrix = {0};
    pruefe("matrix-rueckgabe", sys_matrix((uint64_t)(uintptr_t)&matrix, 2, 3), 549);
    pruefe("matrix-zelle", matrix.feld[2][3], 321);
    pruefe("matrix-nachbar", matrix.feld[2][2], 0);
    pruefe("matrix-punkt-code", matrix.punkte[1][0].code, 222);
    pruefe("matrix-punkt-flags", matrix.punkte[1][0].flags, 6);
    pruefe("matrix-andere-zeile", matrix.punkte[0][1].code, 0);
    struct moo_feld_eintrag ziel2 = {0};
    struct moo_matrix_pointer matrix_pointer = {{{0, 0}, {0, &ziel2}}};
    pruefe("matrix-ptr-rueckgabe", sys_matrix_zeiger((uint64_t)(uintptr_t)&matrix_pointer), 27);
    pruefe("matrix-ptr-speicher", ziel2.flags, 27);
    const uint64_t matrix_basis = (uint64_t)(uintptr_t)&matrix;
    pruefe("adr-matrix-zelle", (int64_t)(sys_adresse_matrix(matrix_basis) - matrix_basis),
           (int64_t)offsetof(struct moo_matrix, feld) + (2 * 4 + 3) * (int64_t)sizeof(uint16_t));
    pruefe("adr-matrix-zeile", (int64_t)(sys_adresse_zeile(matrix_basis) - matrix_basis),
           (int64_t)offsetof(struct moo_matrix, feld) + 4 * (int64_t)sizeof(uint16_t));
    pruefe("adr-matrix-punkt", (int64_t)(sys_adresse_punkt(matrix_basis) - matrix_basis),
           (int64_t)offsetof(struct moo_matrix, punkte) + 2 * (int64_t)sizeof(struct moo_feld_eintrag));
    pruefe("adr-matrix-struktur", (int64_t)(sys_adresse_struktur(matrix_basis) - matrix_basis),
           (int64_t)offsetof(struct moo_matrix, punkte) + 2 * (int64_t)sizeof(struct moo_feld_eintrag));

    /* P2-T48: WERTE statt nur Offsets/Zeiger: echte C/LLVM-Speicherproben. */
    struct native_wert einzel = {0};
    pruefe("wert-kopie-ergebnis", (int64_t)sys_value_copy((uint64_t)(uintptr_t)&einzel), 110);
    pruefe("wert-kopie-code", einzel.code, 7);
    pruefe("wert-kopie-wert", (int64_t)einzel.wert, 99);

    struct native_block verschachtelt = {0};
    pruefe("wert-nested-ergebnis", (int64_t)sys_value_nested((uint64_t)(uintptr_t)&verschachtelt), 106);
    pruefe("wert-nested-prefix", verschachtelt.prefix, 4);
    pruefe("wert-nested-code", verschachtelt.innen.code, 5);
    pruefe("wert-nested-wert", (int64_t)verschachtelt.innen.wert, 100);
    pruefe("wert-nested-ende", verschachtelt.ende, 7);

    struct native_wert zwei[2] = {{8, 33}, {0, 0}};
    pruefe("wert-pointer-ergebnis", (int64_t)sys_value_pointer_copy((uint64_t)(uintptr_t)zwei), 75);
    pruefe("wert-pointer-original", zwei[0].code, 8);
    pruefe("wert-pointer-kopie-code", zwei[1].code, 42);
    pruefe("wert-pointer-kopie-wert", (int64_t)zwei[1].wert, 33);

    pruefe("wert-adresse-lokal", (int64_t)sys_value_local_address(), 55);
    struct native_block feldblock = {0};
    pruefe("wert-feld-speichern", (int64_t)sys_value_field_store((uint64_t)(uintptr_t)&feldblock), 88);
    pruefe("wert-feld-code", feldblock.innen.code, 3);
    pruefe("wert-feld-wert", (int64_t)feldblock.innen.wert, 88);
    pruefe("wert-feld-kopie", (int64_t)sys_value_field_copy((uint64_t)(uintptr_t)&feldblock), 108);
    pruefe("wert-feld-unveraendert", (int64_t)feldblock.innen.wert, 88);
    struct native_wert aufruf = {0};
    pruefe("moo-moo-struct-ergebnis", (int64_t)sys_value_call((uint64_t)(uintptr_t)&aufruf), 49);
    pruefe("moo-moo-struct-code", aufruf.code, 15);
    pruefe("moo-moo-struct-wert", (int64_t)aufruf.wert, 29);
    struct native_block verschachtelter_aufruf = {0};
    pruefe("moo-moo-nested-ergebnis", (int64_t)sys_value_nested_call((uint64_t)(uintptr_t)&verschachtelter_aufruf), 18);
    pruefe("moo-moo-nested-prefix", verschachtelter_aufruf.prefix, 2);
    pruefe("moo-moo-nested-code", verschachtelter_aufruf.innen.code, 3);
    pruefe("moo-moo-nested-wert", (int64_t)verschachtelter_aufruf.innen.wert, 14);
    pruefe("moo-moo-nested-ende", verschachtelter_aufruf.ende, 5);
    /* Echte, getrennt kompilierte C-Callee aus P2-T56-Referenz. */
    uint32_t aus4 = 0;
    uint32_t aus8[2] = {0, 0};
    pruefe("abi4-rueckgabe", (int64_t)sys_c_abi4((uint64_t)(uintptr_t)&aus4), 18);
    pruefe("abi4-speicher", aus4, 18);
    pruefe("abi8-rueckgabe", (int64_t)sys_c_abi8((uint64_t)(uintptr_t)aus8), 36);
    pruefe("abi8-wort0", aus8[0], 12);
    pruefe("abi8-wort1", aus8[1], 24);
    uint32_t aus12[3] = {0, 0, 0};
    uint32_t aus16[4] = {0, 0, 0, 0};
    pruefe("abi12-rueckgabe", (int64_t)sys_c_abi12((uint64_t)(uintptr_t)aus12), 66);
    pruefe("abi12-wort0", aus12[0], 11);
    pruefe("abi12-wort1", aus12[1], 22);
    pruefe("abi12-wort2", aus12[2], 33);
    pruefe("abi16-rueckgabe", (int64_t)sys_c_abi16((uint64_t)(uintptr_t)aus16), 20);
    pruefe("abi16-wort0", aus16[0], 2);
    pruefe("abi16-wort1", aus16[1], 4);
    pruefe("abi16-wort2", aus16[2], 6);
    pruefe("abi16-wort3", aus16[3], 8);
    uint32_t aus24[6] = {0, 0, 0, 0, 0, 0};
    pruefe("abi24-rueckgabe", (int64_t)sys_c_abi24((uint64_t)(uintptr_t)aus24), 42);
    pruefe("abi24-wort0", aus24[0], 2);
    pruefe("abi24-wort1", aus24[1], 4);
    pruefe("abi24-wort2", aus24[2], 6);
    pruefe("abi24-wort3", aus24[3], 8);
    pruefe("abi24-wort4", aus24[4], 10);
    pruefe("abi24-wort5", aus24[5], 12);
    /* P2-T48 E3: lokale Arrays, Arrayliteral, Nullkonstruktor, Float-Konstanten,
     * Struktur-Array-Stride, Overlap (gegen memmove) und Slot<->Zeiger-Alias. */
    struct e3_messung messung;
    memset(&messung, 0xAB, sizeof messung);
    pruefe("e3-arrays-rueckgabe", (int64_t)sys_e3_arrays((uint64_t)(uintptr_t)&messung), 141);
    pruefe("e3-arrays-werte0", messung.werte[0], 100);
    pruefe("e3-arrays-werte1", messung.werte[1], 2);
    pruefe("e3-arrays-werte3", messung.werte[3], 40);
    pruefe("e3-arrays-skala", messung.skala == 0.1f, 1);
    pruefe("e3-arrays-gueltig", messung.gueltig, 1);
    pruefe("e3-arrays-marke", messung.marke, 7);
    struct e3_messung leer;
    memset(&leer, 0xAB, sizeof leer);
    pruefe("e3-null-rueckgabe", (int64_t)sys_e3_null((uint64_t)(uintptr_t)&leer), 0);
    pruefe("e3-null-felder", leer.werte[0] | leer.werte[1] | leer.werte[2] | leer.werte[3] | leer.marke | leer.gueltig, 0);
    pruefe("e3-null-skala", leer.skala == 0.0f, 1);
    double fl[2] = {0.0, 0.0};
    pruefe("e3-float-rueckgabe", (int64_t)sys_e3_float((uint64_t)(uintptr_t)fl), 1);
    pruefe("e3-float-f32", fl[0] == (double)0.1f * 10.0 + 0.5, 1);
    pruefe("e3-float-tabelle", fl[1] == -1.25, 1);
    struct native_wert reihe[3];
    memset(reihe, 0xAB, sizeof reihe);
    pruefe("e3-strukturarray-rueckgabe", (int64_t)sys_e3_struktur_array((uint64_t)(uintptr_t)reihe), 23);
    pruefe("e3-strukturarray-code0", reihe[0].code, 1);
    pruefe("e3-strukturarray-wert0", (int64_t)reihe[0].wert, 10);
    pruefe("e3-strukturarray-code1", reihe[1].code, 2);
    pruefe("e3-strukturarray-wert1", (int64_t)reihe[1].wert, 20);
    pruefe("e3-strukturarray-code2", reihe[2].code, 3);
    pruefe("e3-strukturarray-wert2", (int64_t)reihe[2].wert, 0);
    struct native_wert ov[2], ref[2];
    memset(ov, 0, sizeof ov);
    ov[0].code = 1; ov[0].wert = 2; ov[1].code = 3; ov[1].wert = 4;
    memcpy(ref, ov, sizeof ov);
    memmove((unsigned char *)ref, (unsigned char *)ref + 8, sizeof ref[0]);
    pruefe("e3-overlap-rueckgabe", (int64_t)sys_e3_overlap((uint64_t)(uintptr_t)ov), (int64_t)ref[0].wert);
    pruefe("e3-overlap-code0", ov[0].code, ref[0].code);
    pruefe("e3-overlap-wert0", (int64_t)ov[0].wert, (int64_t)ref[0].wert);
    pruefe("e3-overlap-code1", ov[1].code, ref[1].code);
    pruefe("e3-overlap-wert1", (int64_t)ov[1].wert, (int64_t)ref[1].wert);
    pruefe("e3-alias", (int64_t)sys_e3_alias(), 606);
    struct e3_block bloecke[2];
    memset(bloecke, 0xAB, sizeof bloecke);
    pruefe("c-ausgerichtet-adresse-stride", (int64_t)sys_e3_ausgerichtet((uint64_t)(uintptr_t)bloecke), 32);
    pruefe("c-ausgerichtet-a0", bloecke[0].a, 7);
    pruefe("c-ausgerichtet-b0", bloecke[0].b, 9);
    pruefe("c-ausgerichtet-a1", bloecke[1].a, 1);
    pruefe("c-ausgerichtet-b1", bloecke[1].b, 2);
    if (fehler) return 1;
    printf("PASS system_probe: 148 Pruefungen\n");
    return 0;
}
