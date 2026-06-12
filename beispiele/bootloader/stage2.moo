# stage2.moo — moo-Anteil des Legacy-BIOS-Stage2-Loaders (P011-D1 -> F1).
#
# Wird vom 16-bit-Stage1 nach 0x8000 geladen und im 32-bit Protected Mode
# (NICHT Long Mode!) ausgefuehrt. Der Entry stage2_entry.S aktiviert SSE
# und ruft haupt(). Ausgabe via kern_outw (P011-B1) an COM1.
#
# Marker MOO-S2-OK beweist: in moo kompilierter 32-bit-Code laeuft als
# Bootloader-Stage2 unter echtem QEMU (GRUB-frei, von Disk geladen).

funktion sende(c):
    unsicher:
        kern_outw(0x3F8, c)

funktion haupt():
    unsicher:
        sende(77)   # M
        sende(79)   # O
        sende(79)   # O
        sende(45)   # -
        sende(83)   # S
        sende(50)   # 2
        sende(45)   # -
        sende(79)   # O
        sende(75)   # K
        sende(10)   # \n
