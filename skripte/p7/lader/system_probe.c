/* P7-T5: Gegenprobe der Moo-Systemkoerper (skripte/p7/lader/system_koerper.moos),
 * kompiliert fuer x86_64-bare (SysV) und hier echt ausgefuehrt. */
#include <stdint.h>
#include <stdio.h>

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
    if (fehler) return 1;
    printf("PASS system_probe: 17 Pruefungen\n");
    return 0;
}
