# P2-T33: abgeschlossene Testnetz-Untersuchung

Stand: 20.11.2026. Die ursprüngliche Untersuchung betraf Commit
`fa6bdfef36443b5e711a4a4d3e9b0559b7f736a2`: 460 Quellen,
24 Compilefehler und 293 unterschiedliche IR-Einträge zwischen zwei Läufen.
Die Tabelle bewahrt diese historischen Diagnosen; sie bezeichnet keine
aktuellen Fehler und keine absichtlichen Negativtests.

## Die 24 historischen Compilefehler

Pfade ohne Präfix liegen unter `beispiele/`. „Profil“ bedeutet einen
fehlenden Ziel-/Buildkontext. Die anderen Fälle waren Inkonsistenzen der
Beispiele, Imports oder APIs; ein zeitlicher Compiler-Regressionsbeginn wurde
damals nicht nachgewiesen.

| Quelle | Historische Diagnose | Klassifikation und heutige Auflösung |
|---|---|---|
| bootloader/stage2.moos | kern_outw nur bare | Profil: i386-bare |
| kernel/arm64_test.moos | kern_seriell_zeichen nur bare | Profil: aarch64-bare |
| kernel/hallo_kern.moos | kern_seriell_zeichen nur bare | Profil: x86_64-bare |
| kernel/pf_test.moos | kern_seriell_zeichen nur bare | Profil: x86_64-bare |
| kernel/stackprot_test.moos | kern_seriell_zeichen nur bare | Profil: x86_64-bare |
| uefi/hallo_uefi.moos | kern_outw nur bare | Profil: x86_64-unknown-uefi |
| coin_catcher.moos:45 | erwartete Einrückung bei Set | Beispielsyntax: mehrzeiliger wenn-Block |
| doodle_jump.moos:68 | erwartete Einrückung bei Return | Beispielsyntax: mehrzeiliger wenn-Block |
| game_2048.moos:21 | erwartete Einrückung bei Return | Beispielsyntax: Block; reservierte lokale Variable umbenannt |
| mine_run.moos:45 | erwartete Einrückung bei Set | Beispielsyntax: mehrzeiliger wenn-Block |
| pipe_puzzle.moos:32 | erwartete Einrückung bei Return | Beispielsyntax: mehrzeiliger wenn-Block |
| pong_plus.moos:76 | erwartete Einrückung bei Set | Beispielsyntax: mehrzeiliger wenn-Block |
| simon.moos:20 | erwartete Einrückung bei Return | Beispielsyntax: mehrzeiliger wenn-Block |
| snake_plus.moos:212 | erwartete Einrückung bei Set | Beispielsyntax: mehrzeiliger wenn-Block |
| tic_tac_toe.moos:220 | erwartete Einrückung bei Set | Beispielsyntax: mehrzeiliger wenn-Block |
| domain/db/bench/bench_binary.moos:14 | Methode verbinde nicht gefunden | Importkontext: selektiver Import aus ../postgres_client |
| domain/db/bench/bench_copy.moos:19 | Methode verbinde nicht gefunden | Importkontext: selektiver Import aus ../postgres_client |
| domain/db/bench/bench_pipelining.moos:17 | Methode verbinde nicht gefunden | Importkontext: selektiver Import aus ../postgres_client |
| domain/db/bench/bench_pool.moos:17 | Methode verbinde nicht gefunden | Importkontext: selektiver Import aus ../postgres_client |
| domain/db/bench/bench_prepared.moos:15 | Methode verbinde nicht gefunden | Importkontext: selektiver Import aus ../postgres_client |
| domain/db/bench/bench_baseline_perfphase.moos:9 | Modul postgres_client_lib nicht gefunden | Importpfad: ../postgres_client |
| domain/db/bench/scram_e2e_smoke.moos:1 | relativer Import nicht gefunden | Importpfad: ../postgres_client |
| domain/db/mini_db.moos:447 | Methode alle nicht gefunden | API: deutsche BTree-Adapter auf put/get/scan/dump |
| compiler/tests/test_stdlib.moos | Funktion durchschnitt nicht gefunden | API: durchschnitt in stdlib/liste, leer ergibt nichts |

Alle Profile enthalten zusätzlich `--no-stdlib`; das Manifest führt die
heute benötigten Zielprofile vollständig. DB-Importe übernehmen Definitionen,
keine ausführbaren Client-Testteile. Historische Präfix-Zusammenbauten sind
für diese Quellen nicht mehr erforderlich. Ein IR-Erfolg der DB-Beispiele
belegt keinen Lauf gegen einen PostgreSQL-Server.

Quellbelege im Abschlussstand:
`beispiele/domain/db/bench/bench_binary.moos:1–3`,
`bench_baseline_perfphase.moos:9–11`, `scram_e2e_smoke.moos:1–3`;
`beispiele/domain/db/mini_db.moos:220–233`;
`stdlib/liste.moos:15–20`.
Die historischen Parserdiagnosen bezogen sich auf die damaligen Quellzeilen,
nicht auf unveränderte Zeilennummern im heutigen Stand.

## Die 293 historischen IR-Abweichungen

| Klasse | Nachmessung / Grenze | Behandlung |
|---|---|---|
| Zufällige lokale Freigabereihenfolge | Zwei unabhängige Nachmessungen derselben 293 Quellen: 287 bzw. 288 Paare unterschieden sich ausschließlich bei local_exit-Loads; 6 bzw. 5 Paare waren diesmal gleich | Compiler sortiert lokale Namen vor Pointer-Deduplizierung und Release |
| Auswahl aus ungeordneten Methoden-/Fallback-Mengen | Damals statisch benanntes Risiko; kein geändertes Callziel in den beiden Nachmessungen beobachtet | Keine historische Anzahl oder Ursache erfunden; vollständige IR-Vergleiche bleiben maßgeblich |
| Modulheader | Bereits normalisiert; kein belegter Auslöser der 293 Befunde | Nur ModuleID/source_filename werden vereinheitlicht |
| Nutzliterale / absolute Projektpfade | Zwei spätere unsafe_path_in_ir-Fälle waren echte absolute Literale in elf_reader/x86dis, getrennt von den 293 Ursachen | Quellen wurden projektrelativ korrigiert; weitere absolute Projektpfade bleiben FAIL |

Die Nachmessungen klassifizieren nicht rückwirkend jedes ursprüngliche Paar.
Release-Reihenfolgen können Ressourcenfreigaben beeinflussen und werden
nicht sortierend aus dem IR entfernt. Aktueller Produktbeleg:
`compiler/src/codegen.rs:4543–4566`, `release_function_locals`.
Die Normalisierung in `legacy_ir.sh:201–224` ändert nur Modulheader und
Zeilenenden; Nutzliteral-, SSA-, Call- und Freigabedifferenzen bleiben sichtbar.

## Reproduzierbarer Abschluss

Auf dem isolierten Commit `7aa6351b0d6da3448d3ee6554fa489e9e369d7d8`
wurden drei Läufe hintereinander mit demselben Release-Compiler ausgeführt:

1. `mise run test-legacy-ir` (Build, Mutationsprobe und erste Aufnahme).
2. `mise run -C skripte/p2/testnetz legacy-ir --output build/p2-ir/t33-lauf-2.json --baseline skripte/p2/testnetz/legacy-ir-baseline.json --profiles skripte/p2/testnetz/testmanifest.json`.
3. Derselbe Befehl mit `t33-lauf-3.json`.

Dabei galt jeweils
`CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target`.
Jeder Lauf: **493 Quellen, 493 IR-Dateien, 0 Fehler, Comparison: IDENTICAL,
Exit 0**. Die vollständigen JSON-Dateien
`build/p2-ir/root-gate.json`, `t33-lauf-2.json` und `t33-lauf-3.json`
sind bytegleich, jeweils SHA256:

`7aaf28575fa14d760881afd103cf50b4b647dc741f18ff6fed0b104bdfbd9b2a`

Dies umfasst die normalisierten IR-Hashes, exakten Quellen-/Importinventare,
Profile, Toolchain und Driftprüfung. Der Vertrag bleibt
`module-header-root-v2`; keine Baseline-Fehlerausnahme wurde verwendet.
Die Mutationsprobe lehnt sowohl einen veränderten IR-Hash als auch ein
verändertes Importinventar ab. Codebelege:
`legacy_ir.sh:136–173,176–224,246–275`,
`legacy_ir_compare.sh:7–35` und `legacy_ir_selftest.sh:18–34`.
Für diesen Abschluss wurden keine Compilerdateien geändert.
