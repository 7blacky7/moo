# hallo_kern.moo — Bootbarer moo-Demo-Kernel (Plan-010 D1).
#
# Build (One-Shot-Pipeline, P010-C2):
#   moo-compiler compile beispiele/kernel/hallo_kern.moo \
#       --no-stdlib --target x86_64-bare --kernel -o hallo_kern.elf
#
# Boot-Test: scripts/kernel-smoke.sh (GRUB-ISO + QEMU, P010-T1).
#
# Ablauf: kern_boot_main (moo_bare_boot.c) hat VOR main() bereits
# kern_seriell_init() und den Heap (Linker-Symbole kern_heap_start/ende)
# initialisiert. Diese Demo:
#   1. sendet 'MOO-KERN-OK' seriell (Smoke-Marker 1)
#   2. gruesst auf VGA (0xB8000)
#   3. kern_alloc/kern_frei-Demo, Adresse als Hex seriell
#   4. IDT + PIC-Remap + PIT-Timer (100 Hz) + Interrupts an
#   5. 3 Sekunden lang jede Sekunde den Tick-Zaehler ausgeben
#   6. sendet 'MOO-KERN-TICKS-OK' (Smoke-Marker 2) und haelt an
#
# WICHTIG: Im bare-Pfad gibt es keine String-Literale (kein malloc) —
# Text geht als ASCII-Zeichencodes ueber kern_seriell_zeichen/kern_vga_zeichen.

# 'MOO-KERN-OK' + Zeilenumbruch, Zeichen fuer Zeichen seriell.
funktion marker_kern_ok():
    unsicher:
        kern_seriell_zeichen(77)    # M
        kern_seriell_zeichen(79)    # O
        kern_seriell_zeichen(79)    # O
        kern_seriell_zeichen(45)    # -
        kern_seriell_zeichen(75)    # K
        kern_seriell_zeichen(69)    # E
        kern_seriell_zeichen(82)    # R
        kern_seriell_zeichen(78)    # N
        kern_seriell_zeichen(45)    # -
        kern_seriell_zeichen(79)    # O
        kern_seriell_zeichen(75)    # K
        kern_seriell_zeichen(10)    # \n

# 'MOO-KERN-TICKS-OK' + Zeilenumbruch.
funktion marker_ticks_ok():
    unsicher:
        kern_seriell_zeichen(77)    # M
        kern_seriell_zeichen(79)    # O
        kern_seriell_zeichen(79)    # O
        kern_seriell_zeichen(45)    # -
        kern_seriell_zeichen(75)    # K
        kern_seriell_zeichen(69)    # E
        kern_seriell_zeichen(82)    # R
        kern_seriell_zeichen(78)    # N
        kern_seriell_zeichen(45)    # -
        kern_seriell_zeichen(84)    # T
        kern_seriell_zeichen(73)    # I
        kern_seriell_zeichen(67)    # C
        kern_seriell_zeichen(75)    # K
        kern_seriell_zeichen(83)    # S
        kern_seriell_zeichen(45)    # -
        kern_seriell_zeichen(79)    # O
        kern_seriell_zeichen(75)    # K
        kern_seriell_zeichen(10)    # \n

# 'moo kern' auf den VGA-Textmode schreiben (an Cursor-Position).
funktion vga_gruss():
    unsicher:
        kern_vga_zeichen(109)       # m
        kern_vga_zeichen(111)       # o
        kern_vga_zeichen(111)       # o
        kern_vga_zeichen(32)        # (Leerzeichen)
        kern_vga_zeichen(107)       # k
        kern_vga_zeichen(101)       # e
        kern_vga_zeichen(114)       # r
        kern_vga_zeichen(110)       # n

funktion main():
    unsicher:
        # 1. Smoke-Marker 1 (seriell laeuft schon, kern_boot_main hat initialisiert)
        marker_kern_ok()

        # 2. VGA-Gruss
        kern_vga_init()
        vga_gruss()

        # 3. Heap-Demo: 64 Bytes holen, Adresse als Hex zeigen, freigeben.
        #    (Heap kommt aus den Linker-Symbolen, von kern_boot_main initialisiert.)
        setze adresse auf kern_alloc(64)
        kern_seriell_hex(adresse)
        kern_seriell_zeichen(10)
        kern_frei(adresse)

        # 4. Interrupt-Infrastruktur: IDT, PIC auf 0x20/0x28, PIT 100 Hz.
        kern_idt_init()
        kern_pic_remap(0x20, 0x28)
        kern_timer_init(100)
        interrupts_an()

        # 5. 3 Sekunden lang (100 Ticks = 1 s) jede Sekunde den Zaehler senden.
        #    halt() schlaeft bis zum naechsten Interrupt (Einzel-hlt) — kein Busy-Spin.
        setze sekunden auf 0
        setze marke auf 100
        solange sekunden < 3:
            halt()
            setze t auf kern_ticks()
            wenn t >= marke:
                kern_seriell_dez(t)
                kern_seriell_zeichen(10)
                setze marke auf marke + 100
                setze sekunden auf sekunden + 1

        # 6. P011-B2-Beweis: CR0 + EFER (MSR 0xC0000080) als Hex seriell.
        #    Erwartet im QEMU-Long-Mode: CR0 mit PG(31)+PE(0), EFER mit LME(8)+LMA(10).
        kern_seriell_hex(kern_cr_lese(0))
        kern_seriell_zeichen(10)
        kern_seriell_hex(kern_rdmsr_lo(0xC0000080))
        kern_seriell_zeichen(10)

        # 7. Smoke-Marker 2 + sauber anhalten (cli + hlt-Schleife).
        marker_ticks_ok()
        interrupts_aus()
        solange wahr:
            halt()
