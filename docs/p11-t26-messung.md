# P11-T26: Parität, CRC32C und vollständiger MooFS-Scrub

Historische Ausgangsmessung. Der nachfolgende Produkt- und Leistungsstand ist
in [P11-T27](p11-t27-messung.md) dokumentiert.

Stand: Synapse-Produktbatch 35506, Messung 21.11.2026. Die Messaufgabe und
allgemeine Compilerverbesserungen sind umgesetzt. Das übergeordnete Ziel
„mindestens so schnell wie C“ ist nicht durchgehend erreicht.

## Vergleich und Verfahren

Median aus fünf Zeitmessungen pro Zeile, dezimale MB/s (1 MB = 1.000.000 Byte).
Host: Ryzen 5950X, CachyOS, CPU 28. N100: bestehende WSL2-Instanz Synapse,
Linux 6.18.54.1, CPU 2. Kein Neustart, kein Eingriff in Dienste.
Moo wurde für x86_64-unknown-linux-gnu übersetzt, LLVM O2; C-Treiber GCC O3.
Die vorherige Bare-ABI-Messung mit verpflichtender SIMD-Zustandssicherung
wurde verworfen (Thought 4f54d8d4). Vergleichspartner ist ISA-L 2.32.1,
C/ASM-Bibliothek aus dem x86_64_v3-Paket, kein selbst gebauter Linux-Kernel.

| Arbeit | Host Moo | Host ISA-L/Kontrolle | N100 Moo | N100 ISA-L/Kontrolle |
|---|---:|---:|---:|---:|
| P/Q erzeugen | 17.592,64 | 33.371,00 | 6.895,19 | 25.402,80 |
| Zwei Datenplatten retten | 16.279,82 | 34.874,96 | 3.737,79 | 23.936,70 |
| CRC32C | 22.806,71 | 17.617,03 | 3.571,54 | 23.091,40 |
| CRC32C, identischer Moo-Hardwarekörper ohne Dispatch (nur Messkontrolle) | 27.228,30 | 17.617,03 | 20.648,62 | 23.091,40 |
| Vollständiger MooFS-Scrub | 1.344,23 | 2.153,59 | 726,06 | 871,91 |

Die Scrub-Kontrolle ist derselbe vollständige MooFS-Prüfer mit ISA-L als
CRC-Backend, keine eigenständige C-Dateisystemimplementierung.
Beide 2-MiB-Abbilder sind vor dem Lauf bytegleich und bleiben unverändert.
Pro Scrub werden tatsächlich 679.936 geprüfte Blockbytes gezählt.
P/Q und Rekonstruktion verwenden 14 Datenpuffer à 4096 Byte, 4093 Runden
je Messung, 57.344 logische Datenbytes pro Runde. Zwei ausgefallene Datenplatten:
Slots 0 und 13. CRC: 4096 Byte, 65.521 Runden. Scrub: 127 Runden.
ISA-L-Tabellen werden vor der Zeitmessung initialisiert; seine Roh-API erhält
gültige Zeiger. Moo behält die öffentlichen Modus-, Quellen- und Längenprüfungen.
Alle Ergebnisbytes werden mit unabhängigen GF-/CRC-Orakeln verglichen.

Die fünf Rohzeiten stehen in den JSON-Zeilen. Besonders die N100-Rekonstruktion
streut stark (35,59–72,19 ms); der angegebene Median ist 62,79 ms. Es wurde
nicht der beste Lauf ausgewählt. WSL-Hypervisor und unterschiedliche
Dispatch-Verträge begrenzen die Übertragbarkeit auf einen Bare-Kernel.

## Codeumfang und Sicherheitsvertrag

Physische Zeilen einschließlich Kommentaren/Leerzeilen; gemeinsam genutzte
Abhängigkeiten sind ausdrücklich getrennt. Dies ist keine transitive
Zeilenzählung des gesamten Compilers oder der ISA-L-Bibliothek.

| Bereich | Moo-Produktcode | C/ASM-Vergleich | Prüfungen |
|---|---|---|---|
| P/Q und Wiederherstellung | array_q.moos 260 + array_gf256.moos 71 | ISA-L ec_base.c 358 + ec_highlevel_func.c 701; ausgewählte Zwei-Vektor-ASM-Körper AVX2 240 / GFNI 359 | Moo: Modus, Quellen, Slots, Längen, geprüfte SIMD-Bereiche, definierte Alias-Schreibfolge; ISA-L-Roh-API: Vorbedingungen beim Treiber |
| CRC32C | nativ_crc32c.moos 22; generischer Compilerbackend native_crc32c.rs 155 | ISA-L CRC-ASM-Körper 594 / AVX2 486 | Moo: puffer[u8], leere Bereiche, überlaufsichere Teilbereiche, Cleanup, CPU-Merkmal; beide: bytegleiche Standard-/Teil-CRC |
| MooFS-Scrub | ds_moofs_pruefen.moos 436, gemeinsamer Formatcode ds_moofs.moos 459 | gleicher Moo-Prüfer + ISA-L-CRC-Adapter | identische Baum-, Referenz-, Block- und Prüfsummenprüfungen in beiden Armen |

Weitere gemeinsame Compilerbestandteile: SIMD-Dispatch, ca. 124 Zeilen für
den allgemeinen GF-Faktorpfad, ca. 76 Zeilen SIMD-Pufferoperationen.
ISA-L-ASM-Zahlen schließen zusätzliche Makrodateien, Dispatch und Header aus.
Archiv v2.32.1: SHA256 d9f7179ab0e14a3db9b610fac22793854a1435e8423ec9ce07f4cbedc5f92f5e.
Zählbelege: Shell 1f71abe3 und 2a7ab02c; Moo-Zahlen: code_intel(tree/functions/file).

## Umgesetzte Ursachen und verbleibender Abstand

- Gleiche geschützte SIMD-Profile rufen passende Untervarianten direkt auf,
  einschließlich strukturierter Rückgaben. Anderer oder engerer ISA-Deckel
  behält den bisherigen Guard. Beleg: native_simd_dispatch.rs:40–57,
  code_intel(file), Proposal c8b6e442; Batch 35472.
- simd_gf256_mal_faktor(v, faktor:u8) bildet GF(0x11D) als einzelne
  GFNI-Affinabbildung bzw. zwei Nibble-Shuffles ab. Kein Wechsel des Polynoms.
  Beleg: native_simd.rs:174–283, code_intel(file), Proposal 7cbeed66;
  Batches 35488–35492. Faktorindizierte Tabellen sind kein Konstantzeitversprechen.
- simd_laden/simd_speichern arbeiten auf längengeprüften Byteansichten.
  Beide Grenzen werden ohne Addition mit möglichem Überlauf geprüft.
  Unaligned-Zugriff und skalare Profilgrenze bleiben erhalten.
  Beleg: native_system.rs:4533–4595, code_intel(file), Proposal c8c67f2d,
  Batches 35502–35506. Teilüberlappende Ausgaben behalten die alte
  verschachtelte Schreibfolge (array_q.moos:156–189).
- CRC32C verwendet drei unabhängige GPR-Ketten und mathematisches Zusammenfügen,
  mit portabler Tabelle als Rückfall. Kein XMM-/YMM-Zustand erforderlich.
  Beleg: native_crc32c.rs:31–155, code_intel(file); Proposals 287663e0/a4ea273f,
  Batches 35480–35487 und Hosted-O2-Fix 35501.

Vorher bei gleicher SysV-ABI: Host P/Q 3848,37, Rekonstruktion 4058,34,
CRC 536,41 MB/s; N100 506,19 / 576,80 / 331,12 MB/s.
Diese Ausgangsmessungen sind keine vollständigen Scrub-Messungen.
Belege: Shell a55da6bf und 52969419.

Der verbleibende CRC-Abstand auf N100 ist wesentlich durch den wiederholten
CPUID-Dispatch erklärbar: identischer Hardwarekörper ohne Dispatch erreicht
20.648,62 statt 3571,54 MB/s. Das ist eine kontrollierte Messung, kein
zulässiger Ersatz für den Produkt-Guard. Der Körper selbst bleibt hinter
ISA-L (23.091,40) zurück. Kein globaler Featurecache wurde eingeführt.
Beleg: native_crc32c.rs:120–135; moo-pass-io.s:79/7464 (cpuid),
8066 ff. (drei crc32q-Ketten); Shell 70f60986/8651d07c.

P/Q behält mehrere vollständige Quell-/Zielpassagen sowie Prüfpfade
(array_q.moos:126–153); erzeugte ASM enthält weiterhin Fehlerflagabfragen
(moo-pass-io.s:48–72) und geschützte SIMD-Einstiege (6777/6916/7046).
Das belegt Mehrarbeit, jedoch keine gemessene prozentuale Kostenaufteilung.
Ein Fusionsversuch verschlechterte die Messung und wurde mit Batch 35506
entfernt; seine Alias-Orakel bleiben. Kein perf-Profil wurde erhoben.
Die geprüfte als()-Semantik, Fehlerpfade und CPU-Sicherungen wurden für
die Messung nicht abgeschwächt. Das Restziel „überall mindestens C“ bleibt offen.

## Nachweise

Messstand: Compiler ea8c9f575d805471dfcd421b0690db54c06964c81011ad1a17f558d8584e2cf6
(no-default-features), Benchmarkbinärdatei
000234867c0aa65e65302cad7e831cad54feebcce98129c1bbf37be5c8c92bad.
Die spätere Core-Runde baute denselben Source mit Default-Features:
4df05036a5c2f792e615829981ea8446a8dd6e2b2048e5dd7b767ac82aa0b37f.
Legacy und QEMU beziehen sich auf diesen späteren Compiler.

- Host: Shell 0e1c0740, host-pass-io.jsonl SHA256
  5513e50767154e6be00132f50eaa56814ce2c7af4c3d59023b18c927dbd47b89.
- N100: Shell 69cddbec, n100-pass-io.txt SHA256
  8ee7631288fdf91f6eb30a2f6272611971522d9966a9f7c50ba79104e1b83345.
- ASM moo-pass-io.s SHA256
  29cd98b51fe414d58fea8fe77489f05e46c41b97417facc9dd321df33288c262.
- ISA-L libisal.so.2 SHA256
  0e95c577003b384e9471a135c02354fab10f19a4259b4faa5ee002e8ae50ddec.
- CRC-Gate 629c3e71: 9222 Fälle jeweils automatisch/erzwungener Rückfall,
  Guardpages, vier Bereichsfehler, acht Typ-/Arity-Negative, Shadowing,
  Host/Bare/UEFI/AArch64-IR und ASM ohne XSTATE.
- GF-Faktor 46087676 und N100 40ae960f: alle 65.536 Byte-/Faktorpaare pro
  Lane, fünf Profilgrenzen, Scalar-/GFNI128-ASM, Typnegative.
- SIMD-Puffer d3316321 und N100 8d56aeb6: Guardpages, 16/32 Byte, fünf
  Profile, 64 Offsets, Überlauf/Ende, @liest, Shadowing, Cleanup-Markierung 17.
- Q-Matrix d51d5122: 54 Längen-/Moduskombinationen, 4239 Ausfallfälle.
  Alias-Gate 0e1c0740: 49 Erzeugungs- und 49 Zwei-Ausgabe-Orakelfälle.
- MooFS 04efc60f: vollständiger Produktprüfer, Generationen/Restart/Bare/Cleanup.
- Core 4ae91960: 137 PASS, 0 FAIL, 9 deklarierte SKIP.
- UEFI/Kernel-QEMU a196ad4e: vier CPUs, vollständiges Array-/MooFS-Profil,
  KERN.ELF a7585b9e9f8da3d50fe226a953871c4da11e4ae43faf379ce168bc597b316a64.
- Strenges Legacy-Gate 41bae9c1: Sources 576, IR 576, failures 0,
  Comparison IDENTICAL, ohne --allow-baseline-failures.
  Erwartung semantisch aus alter Baseline: 556 bestehende Hashes unverändert,
  18 betroffene Hashes, zwei bereits abgenommene MooFS-CLI zusätzlich,
  fünf aktualisierte Metadatenfelder. Synapse-Erwartungsbatch 35507.
  strict-final.json SHA256
  6860a0d8af699a73d7b291d54c37c2cc46ebcc06e1b05bda3c43a3cf596ce479.
  Gemeinsame P7-Baseline wurde nicht verändert.

Reproduktion: mise run -C skripte/p11/array-q vergleich; für alle Jobs eigenes
CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target.
Kernel-/CI-Veröffentlichung erfolgt erst im koordinierten Meilenstein.
