#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <stdlib.h>

extern "C" {
extern uint16_t suche(uint64_t logfn) __asm__("__moo_native.it8613_suchen");
extern void kurzlauf(uint64_t seconds, uint64_t watchdog, uint64_t hz,
                     bool safety, uint64_t logfn) __asm__("__moo_native.it8613_kurzlauf");

extern uint16_t starten(uint64_t, bool, uint64_t) __asm__("__moo_native.it8613_starten");
extern bool fuettern(uint16_t, uint8_t, uint64_t, uint64_t) __asm__("__moo_native.it8613_bedienen");
extern void abschalten(uint16_t, bool, uint64_t) __asm__("__moo_native.it8613_abschalten");

struct chip {
    uint16_t id;
    uint8_t ldn, index, key, regs[4];
    bool open, reject_config, reject_stop, reject_initial_stop, initial_stop_checked;
    unsigned opens, closes, reads, writes, loads, pre_writes, pre_readback;
    unsigned quittiert, quittung_gelesen;
    bool reject_ack, lose_clock;
    uint64_t deadline;
};
static struct chip chips[2];
static uint32_t pci;
static uint64_t ticks;
static unsigned vor_start_aus, an, bedient, aus, verweigert, stop_fehler;
static bool halt_erwartet;
static jmp_buf halt_return, reset_return;
static bool safety_probe, physical_reset;
static unsigned safety_start, safety_fallback, expirations;
static unsigned samples, reg23_samples, reg2d_samples;
static uint64_t sample_ms;
static bool count_before_expiry, count_after_expiry, status_after_expiry;

static struct chip *chip_for(uint16_t port) {
    assert(port == 0x2e || port == 0x2f || port == 0x4e || port == 0x4f);
    return &chips[port >= 0x4e];
}


// P2-T126: Ereignisspur fuer differentialen Vergleich derselben Portmodell-
// Faelle. FNV-1a verwendet absichtlichen definierten uint64_t-Wrap.
static uint64_t trace_hash = 14695981039346656037ULL;
static uint64_t trace_count = 0;
static FILE *trace_file = NULL;
static void trace_value(uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) {
        trace_hash ^= (uint8_t)value;
        trace_hash *= 1099511628211ULL;
        value >>= 8;
    }
}
static void trace_io(uint64_t kind, uint64_t port, uint64_t value) {
    ++trace_count; trace_value(kind); trace_value(port); trace_value(value);
    if (trace_file) assert(fprintf(trace_file, "%llu %llu %llu\n",
        (unsigned long long)kind, (unsigned long long)port,
        (unsigned long long)value) > 0);
}

void probe_outb(uint8_t value, uint16_t port) {
    trace_io(1, port, value);
    struct chip *c = chip_for(port);
    if (!(port & 1)) {
        if (!c->open) {
            const uint8_t keys[] = {0x87, 0x01, 0x55, static_cast<uint8_t>(port == 0x2e ? 0x55 : 0xaa)};
            assert(value == keys[c->key++]);
            if (c->key == 4) { c->open = true; c->key = 0; c->opens++; }
        } else {
            c->index = value;
        }
        return;
    }
    assert(c->open);
    if (c->index == 0x02) {
        assert(value == 2 && c->ldn == 4);
        c->open = false;
        if (c->pre_readback == 7) c->initial_stop_checked = true;
        c->closes++;
    } else if (c->index == 0x07) {
        assert(c->id == 0x8613 && (value == 7 || value == 4));
        c->ldn = value;
    } else {
        assert(c->id == 0x8613 && c->ldn == 7);
        if (c->index == 0x71) {
            assert(c->initial_stop_checked && vor_start_aus == 1 && !c->loads && !an);
            assert(!c->quittiert && value == (c->regs[0] & 0xfe));
            ++c->quittiert;
            if (!c->reject_ack) c->regs[0] = c->lose_clock ? (value & 0xf7) : value;
            return;
        }
        assert(c->index >= 0x72 && c->index <= 0x74);
        c->writes++;
        if (!c->initial_stop_checked) {
            assert(value == 0 && c->pre_writes < 3 && c->index == 0x72 + c->pre_writes);
            ++c->pre_writes;
        }
        if (c->index == 0x72) {
            assert(value == 0 || value == 0xd0);
            if (value && !c->loads) {
                assert(c->initial_stop_checked && !c->regs[2] && !c->regs[3]);
                assert(vor_start_aus == 1 && c->quittiert == 1 && c->quittung_gelesen && !(c->regs[0] & 1));
            }
            if ((c->reject_config && value) || (c->reject_stop && c->loads && !value)) return;
        } else if (c->index == 0x73) {
            assert(!value || (c->regs[1] == 0xd0 && value == 10));
            if (c->reject_stop && c->loads && !value) return;
            if (c->reject_initial_stop && !c->initial_stop_checked && !value) return;
        } else {
            assert(!value);
        }
        c->regs[c->index - 0x71] = value;
        if (c->index == 0x74) {
            c->deadline = 0;
            if (c->regs[1] == 0xd0 && c->regs[2]) {
                c->loads++;
                c->deadline = ticks + (uint64_t)c->regs[2] * 100;
            }
        }
    }
}

static uint8_t probe_inb_value(uint16_t port) {
    struct chip *c = chip_for(port);
    assert((port & 1) && c->open);
    switch (c->index) {
    case 0x20: return c->id >> 8;
    case 0x21: return c->id;
    case 0x22: return 0xac;
    case 0x23: assert(safety_probe && safety_start && c->ldn == 4); return 0x48;
    case 0x2d: assert(safety_probe && safety_start && c->ldn == 7); return 0x02;
    case 0x07: return c->ldn;
    default:
        assert(c->id == 0x8613 && c->ldn == 7);
        assert(c->index >= 0x71 && c->index <= 0x74);
        c->reads++;
        if (c->index == 0x71 && c->quittiert && !c->loads) ++c->quittung_gelesen;
        if (c->pre_writes == 3 && !c->initial_stop_checked &&
            c->index >= 0x72 && !c->regs[c->index - 0x71])
            c->pre_readback |= 1u << (c->index - 0x72);
        if (c->index == 0x73 && safety_probe && c->deadline)
            return (uint8_t)((c->deadline - ticks + 99) / 100);
        return c->regs[c->index - 0x71];
    }
}

uint8_t probe_inb(uint16_t port) {
    uint8_t value = probe_inb_value(port);
    trace_io(2, port, value);
    return value;
}

void probe_outl(uint32_t value, uint16_t port) {
    trace_io(3, port, value); assert(port == 0xcf8 && value == 0x8000fc00); }
uint32_t probe_inl(uint16_t port) { trace_io(4, port, pci); assert(port == 0xcfc); return pci; }
uint64_t probe_rdtsc(void) {
    ++ticks;
    for (unsigned i = 0; i < 2; ++i) {
        if (chips[i].deadline && ticks >= chips[i].deadline) {
            assert(safety_probe && chips[i].loads == 1 && bedient == 0);
            chips[i].deadline = 0;
            chips[i].regs[0] |= 1;
            chips[i].regs[2] = 0;
            ++expirations;
            if (physical_reset) longjmp(reset_return, 1);
        }
    }
    return ticks;
}
void probe_pause(void) {}
void probe_cli(void) { assert(halt_erwartet && stop_fehler == 1 && aus == 0); }
void probe_hlt(void) { assert(halt_erwartet); longjmp(halt_return, 1); }

static void protokoll(uint64_t label, uint64_t value) {
    const char *s = (const char *)(uintptr_t)label;
    assert(!chips[0].open && !chips[1].open && chips[0].ldn == 4 && chips[1].ldn == 4);
    if (strstr(s, "CTRL QUITTIERUNG READBACK ")) {
        assert(chips[0].quittiert && chips[0].quittung_gelesen && !an && value == chips[0].regs[0]);
    }
    if (strstr(s, "SCHARF SEKUNDEN")) { assert(value == 10); ++an; }
    if (strstr(s, "BEDIENT ")) { ++bedient; assert(value == bedient); }
    if (strstr(s, " VOR START AUS ")) { assert(!value && chips[0].initial_stop_checked && !an); ++vor_start_aus; }
    else if (strstr(s, " AUS ")) { assert(!value); ++aus; }
    if (strstr(s, "SAFETY OHNE BEDIENUNG ")) {
        assert(!bedient && chips[0].loads == 1 && value == 10);
        ++safety_start;
    }
    if (strstr(s, "SAFETY MESSZEIT MS ")) {
        assert(safety_probe && safety_start == 1 && chips[0].writes == 6);
        assert(samples ? value == sample_ms + 1000 : value < 1000);
        assert(value < 45000);
        sample_ms = value;
        ++samples;
    }
    if (strstr(s, " REG 23H ")) { assert(samples && value == 0x48); ++reg23_samples; }
    if (strstr(s, " REG 2DH ")) { assert(samples && value == 0x02); ++reg2d_samples; }
    if (samples && strstr(s, "WDT TIMEOUT LSB ")) {
        if (expirations) { assert(value == 0); count_after_expiry = true; }
        else { assert(value > 0 && value <= 10); count_before_expiry = true; }
    }
    if (samples && expirations && strstr(s, "WDT CTRL ")) {
        assert(value & 1);
        status_after_expiry = true;
    }
    if (strstr(s, "SAFETY SOFTWARE FRIST SEKUNDEN ")) {
        assert(safety_probe && value == 45 && ticks >= 4500 && expirations == 1);
        assert(chips[0].writes == 6 && !bedient && chips[0].loads == 1);
        ++safety_fallback;
    }
    if (strstr(s, "VERWEIGERT ")) ++verweigert;
    if (strstr(s, "ABSCHALTEN FEHLER ")) ++stop_fehler;
}

static void setup(uint16_t id1, uint16_t id2) {
    memset(chips, 0, sizeof(chips));
    chips[0].id = id1;
    chips[1].id = id2;
    for (unsigned i = 0; i < 2; ++i) { chips[i].ldn = 4; chips[i].regs[0] = 8; }
    pci = 0x54a38086;
    ticks = 0;
    vor_start_aus = an = bedient = aus = verweigert = stop_fehler = 0;
    halt_erwartet = safety_probe = physical_reset = false;
    safety_start = safety_fallback = expirations = 0;
    samples = reg23_samples = reg2d_samples = 0;
    sample_ms = 0;
    count_before_expiry = count_after_expiry = status_after_expiry = false;
}
int main(void) {
    const char *trace_path = getenv("P126_TRACE_FILE");
    if (trace_path) { trace_file = fopen(trace_path, "wb"); assert(trace_file); }
    uint64_t logfn = (uint64_t)(uintptr_t)protokoll;

    setup(0x8613, 0x8613);
    assert(suche(logfn) == 0x2e && chips[0].reads == 4 && !chips[0].writes && !chips[1].opens);
    assert(chips[0].opens == 1 && chips[0].closes == 1);
    setup(0x8705, 0x8613);
    assert(suche(logfn) == 0x4e && !chips[0].reads && chips[1].reads == 4);
    assert(chips[0].closes == 1 && chips[1].closes == 1 && !chips[1].writes);
    setup(0x8613, 0x8613);
    pci = 0xffffffff;
    assert(!suche(logfn) && !chips[0].opens && !chips[1].opens);
    puts("PASS Diagnose 2E, Fallback 4E, fremder Chip/PCH. LDN restauriert, Config geschlossen, keine WDT-Datenwrites.");

    setup(0x8613, 0xffff);
    chips[0].regs[0] = 9;
    chips[0].regs[1] = 0x20;
    chips[0].regs[2] = 0x38;
    kurzlauf(25, 10, 100, false, logfn);
    assert(vor_start_aus == 1 && an == 1 && bedient >= 23 && aus == 1 && !verweigert && !stop_fehler);
    assert(chips[0].regs[0] == 8 && !chips[0].regs[1] && !chips[0].regs[2] && !chips[0].regs[3]);
    assert(chips[0].loads == bedient + 1 && chips[0].opens == chips[0].closes);
    printf("PASS CFG20/0038: drei Null-Writes und ihr Readback vor D0/LSB/MSB, %u Reloads, finaler Stopp. CTRL 09->08.\n", bedient);
    setup(0x8613, 0xffff);
    kurzlauf(25, 10, 100, false, logfn);
    assert(vor_start_aus == 1 && an == 1 && bedient >= 23 && aus == 1);
    puts("PASS Ausgangs-Timeout null, gleicher verifizierter Vorstopp.");

    setup(0x8613, 0xffff);
    chips[0].regs[1] = 0xd0;
    chips[0].regs[2] = 0x38;
    kurzlauf(25, 10, 100, false, logfn);
    assert(verweigert == 1 && !an && !aus && !chips[0].writes && chips[0].regs[2] == 0x38);

    setup(0x8613, 0xffff);
    chips[0].regs[1] = 0x20;
    chips[0].regs[2] = 0x38;
    chips[0].reject_initial_stop = true;
    kurzlauf(25, 10, 100, false, logfn);
    assert(verweigert == 1 && !vor_start_aus && !an && !aus && !chips[0].loads);
    assert(chips[0].regs[1] == 0 && chips[0].regs[2] == 0x38);
    puts("PASS Initialer Stop-Readbackfehler unterbindet jede Scharfschaltung.");
    setup(0x8613, 0xffff);
    chips[0].reject_config = true;
    kurzlauf(25, 10, 100, false, logfn);
    assert(verweigert == 1 && vor_start_aus == 1 && !an && aus == 1 && !chips[0].loads);
    puts("PASS Aktiver Fremdtimer unveraendert. CFG-Readbackfehler laedt keine Zeit und deaktiviert sicher.");

    setup(0x8613, 0xffff);
    chips[0].reject_stop = true;
    halt_erwartet = true;
    if (!setjmp(halt_return)) {
        kurzlauf(25, 10, 100, false, logfn);
        assert(!"Rueckkehr ohne bestaetigtes Abschalten");
    }
    assert(stop_fehler == 1 && aus == 0 && !chips[0].open);
    puts("PASS Modus 5: Fehlender Stop-Readback sperrt die Software-Rueckkehr.");

    setup(0x8613, 0xffff);
    chips[0].regs[1] = 0x20;
    chips[0].regs[2] = 0x38;
    safety_probe = physical_reset = true;
    if (!setjmp(reset_return)) {
        kurzlauf(45, 10, 100, true, logfn);
        assert(!"Hardware-Reset im Modell ausgeblieben");
    }
    assert(expirations == 1 && safety_start == 1 && !bedient && !safety_fallback && !aus);
    assert(chips[0].writes == 6 && chips[0].opens == chips[0].closes);
    assert(samples == 10 && reg23_samples == samples && reg2d_samples == samples && count_before_expiry);
    puts("PASS Modus 7: 10 lesende Proben vor simuliertem Reset, keine Bedienung oder Softwarefrist.");

    setup(0x8613, 0xffff);
    safety_probe = true;
    kurzlauf(45, 10, 100, true, logfn);
    assert(expirations == 1 && safety_start == 1 && safety_fallback == 1 && aus == 1 && !stop_fehler);
    assert(!bedient && chips[0].loads == 1 && chips[0].writes == 9);
    assert(samples == 45 && reg23_samples == samples && reg2d_samples == samples);
    assert(count_before_expiry && count_after_expiry && status_after_expiry);
    puts("PASS Modus 7: 45 lesende Proben inklusive modelliertem Ablauf, kein Reload, Softwarefrist weiterhin 45 s.");

    setup(0x8613, 0xffff);
    safety_probe = true;
    chips[0].reject_stop = true;
    kurzlauf(45, 10, 100, true, logfn);
    assert(safety_fallback == 1 && stop_fehler == 1 && !aus && !bedient);
    puts("PASS Modus 7: Softwarefrist bleibt auch bei fehlendem Stopp-Readback wirksam.");
    setup(0x8613, 0xffff);
    chips[0].regs[0] = 9;
    uint16_t port = starten(10, false, logfn);
    assert(port == 0x2e && an == 1 && chips[0].regs[0] == 8);
    assert(fuettern(port, 10, 1, logfn) && bedient == 1);
    abschalten(port, false, logfn);
    assert(aus == 1 && !chips[0].deadline && chips[0].quittiert == 1);
    puts("PASS gemeinsamer IT8613-Start/Reload/Stopp fuer normalen Kernlauf und Rueckfall.");
    for (unsigned safety = 0; safety < 2; ++safety) {
        setup(0x8613, 0xffff);
        chips[0].regs[0] = 9;
        chips[0].reject_ack = true;
        kurzlauf(45, 10, 100, safety, logfn);
        assert(verweigert == 1 && !an && !chips[0].loads && chips[0].quittung_gelesen);
    }
    setup(0x8613, 0xffff);
    chips[0].regs[0] = 9;
    chips[0].lose_clock = true;
    assert(starten(10, false, logfn) == 0);
    assert(verweigert == 1 && !an && !chips[0].loads && chips[0].quittung_gelesen);
    puts("PASS gesetztes Bit 0 oder abweichendes Taktbit im Readback sperren die Scharfschaltung.");
    printf("IO-TRACE %llu %016llx\n", (unsigned long long)trace_count, (unsigned long long)trace_hash);
    if (trace_file) assert(fclose(trace_file) == 0);
    return 0;
}
}
