# pf_test.moo — P012-A3: Intentionaler Page-Fault-Test fuer den #PF-Handler.
#
# Build (One-Shot-Pipeline):
#   moo-compiler compile beispiele/kernel/pf_test.moo \
#       --no-stdlib --target x86_64-bare --kernel -o pf_test.elf
# Boot-Test: scripts/pf-smoke.sh (GRUB-ISO + QEMU).
#
# Die Identity-Map des Boot-Trampolins (moo_bare_boot.c) deckt genau die
# ersten 1 GB ab (PML4[0] -> PDPT[0] -> PD 512x2MB). Adresse 0x40000000
# (= exakt 1 GB) ist NICHT gemappt: speicher_lesen darauf loest #PF aus.
# Der spezialisierte Handler (moo_bare_idt.c, P012-A3) dumpt CR2/RIP/
# Error-Code seriell und haelt an. Erwartete Bits: P=0 (nicht praesent),
# W=0 (Lesen), U=0 (Supervisor).
#
# Erreicht die Ausfuehrung den Code NACH dem Lesezugriff, hat der #PF
# nicht gefeuert -> Miss-Marker MOO-PF-MISS (pf-smoke.sh wertet das als
# FAIL). Im bare-Pfad gibt es keine String-Literale — Marker gehen als
# ASCII-Codes ueber kern_seriell_zeichen.

# 'MOO-PF-START' + \n
funktion marker_start():
    unsicher:
        kern_seriell_zeichen(77)   # M
        kern_seriell_zeichen(79)   # O
        kern_seriell_zeichen(79)   # O
        kern_seriell_zeichen(45)   # -
        kern_seriell_zeichen(80)   # P
        kern_seriell_zeichen(70)   # F
        kern_seriell_zeichen(45)   # -
        kern_seriell_zeichen(83)   # S
        kern_seriell_zeichen(84)   # T
        kern_seriell_zeichen(65)   # A
        kern_seriell_zeichen(82)   # R
        kern_seriell_zeichen(84)   # T
        kern_seriell_zeichen(10)   # \n

# 'MOO-PF-MISS' + \n — unerreichbar, wenn der Handler korrekt feuert.
funktion marker_miss():
    unsicher:
        kern_seriell_zeichen(77)   # M
        kern_seriell_zeichen(79)   # O
        kern_seriell_zeichen(79)   # O
        kern_seriell_zeichen(45)   # -
        kern_seriell_zeichen(80)   # P
        kern_seriell_zeichen(70)   # F
        kern_seriell_zeichen(45)   # -
        kern_seriell_zeichen(77)   # M
        kern_seriell_zeichen(73)   # I
        kern_seriell_zeichen(83)   # S
        kern_seriell_zeichen(83)   # S
        kern_seriell_zeichen(10)   # \n

unsicher:
    marker_start()
    # IDT laden -> spezialisiertes #PF-Gate (Vektor 14) ist aktiv.
    # Kein PIC-Remap/Timer noetig — wir bleiben mit cli unterwegs.
    kern_idt_init()
    # Intentionaler Fault: 8-Byte-Lesen ab 1 GB (erste unmapped Adresse).
    setze wert auf speicher_lesen(0x40000000, 8)
    # Ab hier unerreichbar bei korrektem Handler:
    marker_miss()
    solange wahr:
        halt()
