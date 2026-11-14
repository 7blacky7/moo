# Bare-Metal: Multiboot2-C-Referenz

`mise run -C skripte/p2/bare header-reference` kompiliert das tatsächliche
x86-64-Boot-C als freestanding ELF-Objekt, prüft Maschinentyp und Alignment der
Section `.multiboot2` und vergleicht ihre 24 Bytes inklusive Checksumme und
Endtag mit der kodierten Referenz. Erzeugte Artefakte liegen in
`build/p2-bare-header/`. Benötigt werden cc, readelf und objcopy. Das
Test-Include `boot_test_decls.h` deklariert den später vom Moo-Kernel gelieferten
Entry `main`, damit der isolierte C-Objektbau mit aktueller GCC-Version gelingt.

`mise run -C skripte/p2/bare kernel-reference` startet separat den bestehenden
Kernel-Smoke mit vorhandenem Compiler und headless QEMU/GRUB; ein fehlender
Compiler oder fehlendes QEMU/GRUB-Tool ist ein Fehler, kein stiller Erfolg.

Dies ist eine C-Objektprüfung auf Linux, kein nativer Moo-Header, kein
Linker-/GRUB-/QEMU-Boot und kein Nachweis der vollständigen ersten 32 KiB im
gelinkten Kernel. Diese Gates bleiben für P2-T28 separat offen.
