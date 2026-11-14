# Pointer-Import-Gates

Die Tests prüfen benannte und vollständige Modulimporte eines nativen
`ptr[u16]`-Callee. Sie verwenden den vorhandenen Release-Compiler und bauen
ihn nicht neu. Bei paralleler Compilerarbeit muss das Binary während eines
Laufs unverändert bleiben.

- `mise run -C skripte/p2/frontend_pointer_import syntax`: aufgelöste
  Import-ASTs vergleichen und C++-Adapter auf Syntax prüfen.
- `mise run -C skripte/p2/frontend_pointer_import test`: LLVM-IR für
  x86_64-bare, aarch64-bare und x86_64-unknown-uefi verifizieren und
  extrahierte Objekte erzeugen. Der x86_64-bare-Callee wird zusätzlich über
  einen C++-Adapter lokal ausgeführt.
- `mise run -C skripte/p2/frontend_pointer_import uefi`: beide
  Importformen als extrahierte Moo-Funktionen mit freistehendem C++-Entry
  unter QEMU/OVMF ausführen. Der Adapter übergibt zwei unterschiedliche
  16-Bit-Werte und prüft die Rückgaben.

Das UEFI-Gate benötigt clang++, lld-link, QEMU, OVMF, parted und mtools. Es
erstellt ausschließlich reguläre Image- und Firmwarevariablendateien unter
`build/p2-frontend-pointer-import/uefi/`. Der headless gestartete Emulator
wird nach 20 Sekunden beendet; der erwartete Timeout zählt nur dann als
erfolgreich, wenn der eindeutige PASS-Marker im Debugport-Log vorhanden und
kein FAIL-Marker vorhanden ist. Die beiden Importformen haben getrennte Logs.

Die Tests belegen diesen konkreten Zeigerlesepfad. Sie belegen keine allgemeine
Pointer-, ABI-, Namespace- oder GOP-Unterstützung und keine Ausführung auf
physischer Hardware.
