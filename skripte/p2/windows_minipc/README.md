# Moo: realer Windows-x64-Test auf Mini-PC

Der Mini-PC ist **nur** Ausfuehrungs- und Beobachtungsziel. Moo-Quellcode,
C-Referenzen, Runner und Erkenntnisse werden im Repository versioniert.

Ziel: `Windows 11 Pro x64 / Intel N100`, ueber WinRM und Direkt-Ethernet
(konfiguriert ueber `MOO_MINIPC_HOST`).
Keine Download-Abhaengigkeit auf dem Windows-Rechner: Linux stellt
Moo-Compiler, LLVM, MinGW und den WinRM-Transfer.

## Testworkflow

```bash
MOO_COMPILER=build/p2-t48-51-cargo/release/moo-compiler bash skripte/p2/windows_minipc/test.sh
# alternativ ueber den bestehenden Mise-Task-Mechanismus:
mise run -C skripte/p2/windows_minipc test
```

1. Native Moo-Quelle `native_probe.moos` als Windows-GNU LLVM-IR ausgeben.
2. Nur benoetigte `@nativ`-Funktionen extrahieren. LLVM verify und Win64-Objekt.
3. Ein unabhaengiges C-Harness mit MinGW auf Linux zur Windows-EXE linken.
4. EXE ueber die isolierte Windows-SMB-Testfreigabe SHA256-verifiziert kopieren.
   Einmalige Bereitstellung: `minipc_winrm.py runfile skripte/p2/windows_minipc/setup_share.ps1`.
5. EXE auf echtem Windows ausfuehren (Ausgaben und Exitcode pruefen).
6. Beobachtungen und Abweichungen dokumentieren.

Diese Probe **beweist echten nativen Moo-i32-Code und Win32-FFI via
GetCurrentProcessId() auf Win64**. Zusaetzlich wird die bestehende C-Referenz
fuer 4/8/12/16/24-Byte-Strukturwerte, Mixed, Float4, Packed und Registerdruck
echt auf Windows ausgefuehrt. Die C-only-Referenz bleibt ein separater ABI-Beleg.
Der gemeinsame Win64-Struct-Wert-Test nutzt dagegen die bereits fuer
SysV geschriebene Moo-Quelle system_ffi_struct.moos und prueft 4/8/12/16/24
Bytes mit echtem Moo->C->Moo Roundtrip auf dem Mini-PC.
Der neue gemeinsame E2E-Test `skripte/p7/lader/system_ffi_struct_complex.moos`
beweist **zusaetzlich drei weitere konkrete C-ABI-Strukturformen** auf
Linux-SysV und dem echten Windows-N100 (jeweils 57/57):
- `{f64, u32}` (16 Byte, Alignment 8): SysV SSE + INTEGER,
  Win64 sret + Zeiger
- `{f32, f32, f32, f32}` (16 Byte): SysV zwei SSE-Registerteile,
  Win64 sret + Zeiger
- `@gepackt {u8, u32}` (5 Byte): SysV sret + byval,
  Win64 sret + Zeiger

Das ist ein echter Moo->separates C->Moo-Lauf, nicht bloss ein C-only-Test.
Das Moo-Programm erzeugt dabei auch Float-Werte mit +, -, *, / und wertet
IEEE-Float-Vergleiche aus. Die separate C-Referenz verifiziert Felder,
Rueckgabewerte und Groessen/Offsets. Darueber hinaus akzeptiert der
gemeinsame ABI-Adapter jetzt entweder ein oder zwei gleichartig nominale
Strukturparameter und optional bis zu sechs vorangestellte u64-Parameter.
Echt geprueft: zwei 8B-/12B-Werte, zwei Mixed-/Float4-/Packed-Werte und
sechs u64 plus ein 8B-Strukturwert mit Registerdruck.

## Neuer SysV-Registerdruck-Beweis

Die Gegenprobe benutzt Clang als unabhaengiges C-Frontend und prueft
die exakten LLVM-ABI-Signaturen. Moo klassifiziert Argumente jetzt
unabhaengig vom Rueckgabewert und zaehlt 6 Integer- sowie 8 SSE-Register
separat. Reichen die Register fuer ein mehrteiliges Aggregat nicht,
wird **die ganze Struktur** als SysV-byval-Speicherargument uebergeben,
anstatt ein Aggregat teilweise in Register zu zerlegen.

Mit demselben Moo-Programm auf Linux SysV und dem echten Windows-N100
wurden folgende neue Formen einschliesslich Rueckgabewerten und
Speicherinhalt verifiziert:
- fuenf bzw. sechs u64 + 12-Byte-Struktur
- fuenf u64 + 16-Byte-Struktur
- sechs u64 + gemischte f64/u32-Struktur
- drei u64 + zwei 12-Byte-Strukturen: die erste passt in Register,
  die zweite wird bei SysV komplett zu byval
- sechs u64 + Float4: die SSE-Register bleiben trotz GP-Druck frei

Der neue komplexe E2E-Gesamtlauf enthaelt 57 pruefbare Resultate pro
System. Das bereits vorhandene Windows-Gate testet zusaetzlich 7/7
native Moo-Funktionen und 21/21 einfache Moo-C-Strukturwertpruefungen.

Grenzen: Noch kein allgemeiner C-ABI-Classifier. 20 Byte aus fuenf
u32-Feldern, nicht belegte Mixed-Layouts, drei Strukturparameter,
verschiedene nominale Strukturparameter oder mehr als sechs vorangestellte
u64 in einem Roundtrip bleiben gesperrt. AArch64-Struct-by-value
wurde hier nicht freigegeben.

Fuer das MSVC-x64-Target werden LLVM-IR und COFF-Objekte geprueft,
aber **kein** natives MSVC-Programm auf Windows ausgefuehrt.
Der Moo-Compiler wurde noch nicht **auf** Windows gebaut; weitere
portable Ziel-ABI-Gates folgen.

Nur das konfigurierte Testzielverzeichnis wird verwendet; keine Systemdateien oder USB-Bootmedien.

## Windows-EXE direkt aus dem Moo-Compiler (P2-T75)

```bash
mise run -C skripte/p2/windows_minipc runtime-mingw          # Runtime-Archiv (MinGW)
MOO_RUNTIME_LIB_WINDOWS_GNU=$PWD/build/p2/windows_minipc/runtime/libmoo_runtime_win.a \
  moo-compiler compile prog.moos --target x86_64-pc-windows-gnu [--lib-pfad DIR] -o prog.exe
```

- Nur mit Ausgabe `*.exe` wird gelinkt; sonst entsteht wie bisher nur ein Objekt.
- Linker: `MOO_MINGW_CC` oder `x86_64-w64-mingw32-gcc`. Runtime: `MOO_RUNTIME_LIB_WINDOWS_GNU`
  oder `lib/x86_64-pc-windows-gnu/libmoo_runtime.a` neben dem Compiler, sonst das
  beim Compiler-Build erzeugte Archiv (P2-T76: build.rs ruft `runtime_mingw.sh`,
  wenn MinGW vorhanden ist; `MOO_WINDOWS_RUNTIME=0` schaltet ab; ohne MinGW gibt es
  eine Build-Warnung und `moo compile ... -o x.exe` nennt den Grund).
- `@bibliothek` wird als `-l` uebergeben. Gelinkt wird `-static` (keine
  libwinpthread-DLL auf dem Ziel); eine DLL-Importbibliothek muss deshalb
  `libNAME.a` heissen, der DLL-Import selbst bleibt dynamisch.
- MSVC-Ziel mit `.exe` wird abgelehnt (kein MSVC-Link auf Linux).

### Windows-DLL aus Moo (P2-T79)

```bash
moo-compiler compile bib.moos --target x86_64-pc-windows-gnu -o bib.dll
```

- Ausgabe `*.dll` linkt `-shared` mit derselben Runtime/denselben Bibliotheken wie `.exe`.
- Exportiert werden genau die `@nativ @einstieg`-Funktionen (Moduldefinitionsdatei,
  kein Auto-Export der Runtime); ohne solche Funktion bricht der Compiler ab.
- Die Importbibliothek entsteht als `lib<NAME>.a` neben der DLL; C/Moo-Clients linken
  sie mit `-L DIR -l<NAME>` (auch unter `-static`), der Import bleibt dynamisch.
- `@einstieg`-Exporte trappen bei Fehlern (ADR §11) und brauchen keine Laufzeit-Initialisierung.
- P2-T81: Neben der DLL entsteht `<NAME>_schnittstelle.moos` (je Export `@bibliothek("<NAME>")`
  + `extern "C" funktion ...`). Ein Moo-Verbraucher schreibt `importiere "<NAME>_schnittstelle"`
  und kompiliert mit `--lib-pfad <DLL-Verzeichnis>`; `--lib-pfad` ist dafuer auch Modul-Suchpfad.
  Nicht als C-Typ abbildbare Signaturen (z. B. `bool`, Strukturen) -> Warnung, keine Schnittstelle;
  fremde Dateien gleichen Namens werden nie ueberschrieben.
- Nachweis auf dem N100: `mise run -C skripte/p2/windows_minipc test-hosted-ffi`.
