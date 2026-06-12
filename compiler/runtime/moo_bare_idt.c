/**
 * moo_bare_idt.c — Interrupt-Infrastruktur x86_64 (Plan-010 K4).
 *
 * Liefert:
 *   - IDT mit 256 Gates, Default-ISRs fuer Exceptions (0-31) und IRQs.
 *   - PIC-Remap (8259), Maskierung, EOI.
 *   - PIT-Timer (8254) Kanal 0, Mode 3, mit Tick-Zaehler bei IRQ0.
 *
 * Die ISRs nutzen __attribute__((interrupt)) (GCC/Clang x86 interrupt CC),
 * sodass kein nacktes asm-Glue noetig ist. Nur __x86_64__; andere Targets
 * bekommen compile-only no-op Stubs.
 *
 * @unterbrechung-moo-Handler (eigene ISRs in moo) sind ein Folgetask — dafuer
 * braucht der Codegen ein funktion_adresse-Builtin, um kern_idt_setze(vec, fn)
 * aus moo aufzurufen. Hier wird die C-Infrastruktur bereitgestellt.
 *
 * UB-Policy: Divisor-Rechnung in uint32_t, keine signed shifts.
 */
#include "moo_bare_kern.h"

#if defined(__x86_64__)

/* ---- IDT-Strukturen ---- */
typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;     /* Code-Segment-Selektor (0x08 = GDT64 Code) */
    uint8_t  ist;          /* Interrupt Stack Table (0 = none) */
    uint8_t  type_attr;    /* 0x8E = present, ring0, 64-bit interrupt gate */
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} IdtGate;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} IdtPtr;

static IdtGate g_idt[256] __attribute__((aligned(16)));

/* Volatiler Tick-Zaehler, von IRQ0 inkrementiert. */
static volatile uint64_t g_ticks = 0;

#define KERN_CODE_SELECTOR 0x08
#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1
#define PIC_EOI    0x20

static void idt_setze_gate(int vec, void* handler, uint8_t type_attr) {
    uintptr_t addr = (uintptr_t)handler;
    IdtGate* g = &g_idt[vec];
    g->offset_low  = (uint16_t)(addr & 0xFFFFu);
    g->selector    = KERN_CODE_SELECTOR;
    g->ist         = 0;
    g->type_attr   = type_attr;
    g->offset_mid  = (uint16_t)((addr >> 16) & 0xFFFFu);
    g->offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFFu);
    g->reserved    = 0;
}

/* ---- Default-ISRs ---- */
/* x86_64 Interrupt-Frame, von der CPU gepusht (Intel SDM Vol.3 6.14.1).
 * Volle Definition (statt Forward-Decl), damit ISRs RIP lesen koennen. */
struct interrupt_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

/* Generischer Exception-Handler: Vektor + RIP seriell dumpen, dann halt.
 * general-regs-only: ISRs duerfen kein SSE clobbern (kein FXSAVE-State). */
__attribute__((interrupt, target("general-regs-only")))
static void isr_exception(struct interrupt_frame* frame) {
    (void)frame;
    kern_seriell_text("\n[KERN] EXCEPTION — System angehalten\n");
    moo_cpu_cli();
    for (;;) moo_cpu_halt();
}

/* #PF (Vektor 14, P012-A3): Page-Fault spezialisiert. Die CPU pusht bei
 * #PF zusaetzlich einen Error-Code — die interrupt-CC mit zweitem
 * uint64_t-Parameter laesst GCC/Clang den Code korrekt vor iretq
 * wegraeumen. CR2 traegt die Fault-Adresse. Ausgabe NUR ueber SSE-freie
 * C-Helfer (general-regs-only: kein FXSAVE-State in der ISR), danach
 * cli + halt wie isr_exception. Error-Code-Bits (SDM Vol.3 6.15):
 * P(0)=Schutzverletzung/nicht-praesent, W(1)=Schreiben/Lesen,
 * U(2)=User/Supervisor, RSVD(3), I(4)=Instruktions-Fetch. */
__attribute__((interrupt, target("general-regs-only")))
static void isr_page_fault(struct interrupt_frame* frame, uint64_t error_code) {
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    kern_seriell_text("\n[KERN] #PF PAGE-FAULT\n  CR2 : ");
    kern_seriell_hex_u64(cr2);
    kern_seriell_text("\n  RIP : ");
    kern_seriell_hex_u64(frame->rip);
    kern_seriell_text("\n  Code: ");
    kern_seriell_hex_u64(error_code);
    kern_seriell_text("\n  Bits: ");
    kern_seriell_text((error_code & 1u)  ? "P=1(Schutzverletzung) " : "P=0(nicht praesent) ");
    kern_seriell_text((error_code & 2u)  ? "W=1(Schreiben) "        : "W=0(Lesen) ");
    kern_seriell_text((error_code & 4u)  ? "U=1(User) "             : "U=0(Supervisor) ");
    if (error_code & 8u)  kern_seriell_text("RSVD=1 ");
    if (error_code & 16u) kern_seriell_text("I=1(Instruktions-Fetch) ");
    kern_seriell_text("\n[KERN] #PF — System angehalten\n");
    moo_cpu_cli();
    for (;;) moo_cpu_halt();
}

/* IRQ0 = Timer: Tick hochzaehlen, EOI. */
__attribute__((interrupt, target("general-regs-only")))
static void isr_timer(struct interrupt_frame* frame) {
    (void)frame;
    g_ticks++;                       /* uint64, definierter Wrap bei Overflow */
    kern_outb(PIC1_CMD, PIC_EOI);
}

/* Generischer IRQ-Handler (IRQ1-15): nur EOI. */
__attribute__((interrupt, target("general-regs-only")))
static void isr_irq_master(struct interrupt_frame* frame) {
    (void)frame;
    kern_outb(PIC1_CMD, PIC_EOI);
}
__attribute__((interrupt, target("general-regs-only")))
static void isr_irq_slave(struct interrupt_frame* frame) {
    (void)frame;
    kern_outb(PIC2_CMD, PIC_EOI);   /* EOI an Slave UND Master */
    kern_outb(PIC1_CMD, PIC_EOI);
}

static void idt_lade(void) {
    IdtPtr ptr;
    ptr.limit = (uint16_t)(sizeof(g_idt) - 1);
    ptr.base  = (uint64_t)(uintptr_t)g_idt;
    __asm__ volatile("lidt %0" : : "m"(ptr) : "memory");
}

MooValue kern_idt_init(void) {
    /* Exceptions 0-31. */
    for (int v = 0; v < 32; ++v) idt_setze_gate(v, (void*)isr_exception, 0x8E);
    /* #PF (Vektor 14) spezialisiert: CR2/Error-Code-Dump (P012-A3). */
    idt_setze_gate(14, (void*)isr_page_fault, 0x8E);
    /* IRQs 32-47 (nach PIC-Remap). */
    idt_setze_gate(32, (void*)isr_timer, 0x8E);            /* IRQ0 */
    for (int v = 33; v < 40; ++v) idt_setze_gate(v, (void*)isr_irq_master, 0x8E);
    for (int v = 40; v < 48; ++v) idt_setze_gate(v, (void*)isr_irq_slave, 0x8E);
    /* Rest: Exception-Handler als sicheres Default. */
    for (int v = 48; v < 256; ++v) idt_setze_gate(v, (void*)isr_exception, 0x8E);
    idt_lade();
    return moo_none();
}

MooValue kern_pic_remap(MooValue offset1, MooValue offset2) {
    uint8_t o1 = (uint8_t)kern_as_double(offset1);
    uint8_t o2 = (uint8_t)kern_as_double(offset2);
    uint8_t m1 = kern_inb(PIC1_DATA);   /* Masken merken */
    uint8_t m2 = kern_inb(PIC2_DATA);

    kern_outb(PIC1_CMD, 0x11); kern_outb(PIC2_CMD, 0x11);  /* ICW1: init + ICW4 */
    kern_outb(PIC1_DATA, o1);  kern_outb(PIC2_DATA, o2);   /* ICW2: Vektor-Offsets */
    kern_outb(PIC1_DATA, 0x04); kern_outb(PIC2_DATA, 0x02);/* ICW3: Master/Slave-Topologie */
    kern_outb(PIC1_DATA, 0x01); kern_outb(PIC2_DATA, 0x01);/* ICW4: 8086-Modus */

    kern_outb(PIC1_DATA, m1); kern_outb(PIC2_DATA, m2);    /* Masken wiederherstellen */
    return moo_none();
}

MooValue kern_pic_maskiere(MooValue irq) {
    uint8_t line = (uint8_t)kern_as_double(irq);
    uint16_t port = (line < 8) ? PIC1_DATA : PIC2_DATA;
    if (line >= 8) line = (uint8_t)(line - 8u);
    uint8_t v = kern_inb(port);
    v = (uint8_t)(v | (uint8_t)(1u << line));
    kern_outb(port, v);
    return moo_none();
}

MooValue kern_pic_demaskiere(MooValue irq) {
    uint8_t line = (uint8_t)kern_as_double(irq);
    uint16_t port = (line < 8) ? PIC1_DATA : PIC2_DATA;
    if (line >= 8) line = (uint8_t)(line - 8u);
    uint8_t v = kern_inb(port);
    v = (uint8_t)(v & (uint8_t)~(1u << line));
    kern_outb(port, v);
    return moo_none();
}

MooValue kern_pic_eoi(MooValue irq) {
    uint8_t line = (uint8_t)kern_as_double(irq);
    if (line >= 8) kern_outb(PIC2_CMD, PIC_EOI);
    kern_outb(PIC1_CMD, PIC_EOI);
    return moo_none();
}

MooValue kern_timer_init(MooValue hz) {
    uint32_t freq = (uint32_t)kern_as_double(hz);
    if (freq == 0) freq = 100u;
    uint32_t divisor = 1193182u / freq;
    if (divisor == 0) divisor = 1u;
    if (divisor > 0xFFFFu) divisor = 0xFFFFu;

    kern_outb(0x43, 0x36);                              /* Kanal 0, lo/hi, Mode 3 */
    kern_outb(0x40, (uint8_t)(divisor & 0xFFu));        /* Divisor lo */
    kern_outb(0x40, (uint8_t)((divisor >> 8) & 0xFFu)); /* Divisor hi */

    /* IRQ0 freischalten (Bit 0 demaskieren). */
    uint8_t mask = kern_inb(PIC1_DATA);
    mask = (uint8_t)(mask & (uint8_t)~1u);
    kern_outb(PIC1_DATA, mask);
    return moo_none();
}

MooValue kern_ticks(void) {
    return moo_number((double)g_ticks);
}

#else  /* compile-only Stubs fuer aarch64/riscv64 */

MooValue kern_idt_init(void) { return moo_none(); }
MooValue kern_pic_remap(MooValue a, MooValue b) { (void)a; (void)b; return moo_none(); }
MooValue kern_pic_maskiere(MooValue irq) { (void)irq; return moo_none(); }
MooValue kern_pic_demaskiere(MooValue irq) { (void)irq; return moo_none(); }
MooValue kern_pic_eoi(MooValue irq) { (void)irq; return moo_none(); }
MooValue kern_timer_init(MooValue hz) { (void)hz; return moo_none(); }
MooValue kern_ticks(void) { return moo_number(0.0); }

#endif
