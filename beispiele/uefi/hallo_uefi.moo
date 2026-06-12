# hallo_uefi.moo — moo als UEFI-Application (P011-E1 -> F1).
#
# Build (reproduzierbar via scripts/uefi-smoke.sh):
#   moo-compiler compile hallo_uefi.moo --no-stdlib \
#       --target x86_64-unknown-uefi -o uefi_moo.obj   (echtes PE/COFF)
#   clang --target=x86_64-unknown-windows  efi_entry.c uefi_rt.c
#   lld-link /subsystem:efi_application /entry:efi_main -> BOOTX64.EFI
#   GPT-Disk mit ESP + EFI/BOOT/BOOTX64.EFI, Boot via OVMF.
#
# Der UEFI-Entry efi_main (compiler/runtime/boot/uefi_entry.c) sendet
# 'MOO-UEFI-ENTRY', ruft dann moo_efi_haupt() (hier), und sendet danach
# 'MOO-UEFI-OK' — der zweite Marker beweist die saubere Rueckkehr aus moo.
#
# WICHTIG: Im UEFI-Boot-Services-Environment ist SSE bereits aktiv (die
# Firmware nutzt es selbst) — anders als beim bare-Stage2-Pfad (D1), wo SSE
# erst manuell eingeschaltet werden muss. Ausgabe geht ueber COM1-Port-I/O
# (kern_outw, P011-B1); OVMF/QEMU haben COM1.

funktion sende(c):
    unsicher:
        kern_outw(0x3F8, c)

# moo-Anteil der UEFI-App: sendet 'MOO-UEFI-MOO\n' als eigenen Lebensbeweis,
# damit im Seriallog sichtbar ist, dass der in moo kompilierte Code laeuft.
funktion moo_efi_haupt():
    unsicher:
        sende(77)   # M
        sende(79)   # O
        sende(79)   # O
        sende(45)   # -
        sende(85)   # U
        sende(69)   # E
        sende(70)   # F
        sende(73)   # I
        sende(45)   # -
        sende(77)   # M
        sende(79)   # O
        sende(79)   # O
        sende(10)   # \n
