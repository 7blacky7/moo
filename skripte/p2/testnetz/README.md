# P2 Testnetz (Vorbereitung)

Der IR-Runner nutzt das vorhandene Release-Binary, baut nichts und führt keine
Moo-Programme aus. Der separate Ausgabe-Runner unten führt ausgewählte
Programme ausdrücklich aus. Der IR-Runner erfasst top-level `compiler/tests/*.moos` und rekursiv
`beispiele/**/*.moos`. Lokale P6-Feintuning- und Importbeispiele sind aus Quell- und Importinventar ausgeschlossen; übrige Beispiele bleiben enthalten. Beispiele benötigen teils Imports oder besondere
Compiler-Optionen; Fehler und Zeitüberschreitungen werden ausdrücklich im
Manifest markiert und bewirken Exit 1. Ein identischer Fehler ist kein grünes
IR-Gate.

```sh
mise run -C skripte/p2/testnetz legacy-ir --output build/p2-ir/lauf1.json --profiles skripte/p2/testnetz/testmanifest.json
mise run -C skripte/p2/testnetz legacy-ir --output build/p2-ir/lauf2.json --profiles skripte/p2/testnetz/testmanifest.json --baseline build/p2-ir/lauf1.json
```

Der mise-Task ruft `legacy_ir.sh` auf. `--profiles skripte/p2/testnetz/testmanifest.json` ergänzt für
sechs belegte Bare-/UEFI-Quellen deren IR-Profilargumente. Die anderen Fälle
werden weiterhin unverändert kompiliert und Fehler nicht übersprungen.
Vergleichsläufe müssen dieselben Profile verwenden; eine Profiländerung oder
ein als driftend markierter Ausgangslauf ist keine passende Baseline. Das
Werkzeug erkennt während eines Laufs Änderungen an Binary, Dateimenge und den
direkten Quelltexten; importierte Abhängigkeiten und Toolchain erfordern
weiterhin einen separat eingefrorenen Snapshot. Optional speichert `--save-ir`
normalisiertes IR pro Quellpfad unter einem Verzeichnis in `build/p2-ir/`;
damit lassen sich spätere Hash-Abweichungen konkret untersuchen. Für eine
Baselinemessung nimmt `--snapshot-ref <40-stellige-SHA>` die vom Aufrufer
angegebene Standkennung ins JSON auf. Sie ist eine nicht vom Runner verifizierte
Angabe; Quellen-, Importgraph- und Toolchain-Bindung müssen separat belegt sein.
`--batch-ref <Nummer>` trägt stattdessen eine vom Aufrufer angegebene
Batchnummer ein; auch sie wird nicht automatisch verifiziert. Ein Hash
über das Inventar der Compiler-/Runtime-Quellen und auffindbaren Release-Runtime-Archive
wird vor und nach dem Lauf auf Drift geprüft. Er ersetzt keine vollständige
Importgraph- und Toolchain-Attestation.

Das JSON enthält den Binary-SHA256, einen Hash der direkten Quelldateien
und je Pfad den SHA256 des normalisierten
LLVM-IR bzw. einen Fehlerstatus. Modulheader werden normalisiert. Wenn der
Projektpfad anderswo im IR erscheint, meldet der Runner `unsafe_path_in_ir`,
statt möglicherweise Nutzdaten zu verändern. SSA-Namen, Konstanten und
Reihenfolge bleiben bewusst erhalten,
damit Änderungen nicht verdeckt werden. Nichtdeterminismus in diesen Bereichen
muss anhand zweier Läufe untersucht werden. Unterschiedliche Dateimengen,
Status oder Hashes ergeben Exit 1; fehlerhafte Baselines Exit 2. Der
Binary-Hash ist informativ, da gerade verschiedene Compiler verglichen werden
sollen. Ein grüner IR-Vergleich ersetzt keine Laufzeit-, Parser-, ABI- oder
Plattformtests.

## Headless-Ausgaben vor und nach einer Änderung

```sh
mise run -C skripte/p2/testnetz legacy-output build/p2-ir/vorher 30638
mise run -C skripte/p2/testnetz legacy-output build/p2-ir/nachher 30781 build/p2-ir/vorher
```

Der separate Runner übersetzt und startet sechs ausgewählte Bench-Beispiele mit
dem vorhandenen Binary. Er speichert Ausgaben und Exitcodes, Compilerhash,
Eingabe- und Runtime-Inventar sowie cc-/ld-Versionen. Im Vergleich müssen
Programmausgaben, Exitcodes, direkte Testquellen und Toolversionen übereinstimmen.
Jeder Compiler- oder Programmfehler und jede Drift während einer Aufnahme
führt zu Exit 1. Die Batchnummer ist eine nicht verifizierte Angabe; die sechs
Fälle belegen keine vollständige Abdeckung aller Beispiele. Zielverzeichnisse
müssen neu sein und unter build/p2-ir liegen.

## Entwurf erwarteter Compilefehler (P1-T456)

P1-T456 und `compiler/tests/run_all.sh` bleiben unverändert. Künftiger Runner:
separates `compiler/tests/must_fail/*.moos` und gleichnamige
`.expected_error`-Datei mit einem nichtleeren UTF-8-Teilstring der
Compiler-Diagnose. Compiler-Exit muss ungleich null sein, die Diagnose
(stdout+stderr) muss den Teilstring wörtlich enthalten; Timeout, Signal,
fehlender Compiler, fehlende/leere Erwartungsdatei und unerwarteter Erfolg
sind FAIL. Kein Ausführen eines möglicherweise entstandenen Programms.
Optional später eine strukturierte Zeilenangabe vereinbaren; zunächst kann
ein stabiler Zeilentext als Teilstring geprüft werden. Gegentests: korrekter
Fehler PASS, falscher Text FAIL, erfolgreiche Kompilierung FAIL. Der
bestehende `.expected`-Lauf bleibt unverändert; Konflikte beider Endungen
für denselben Test sind explizit abzulehnen.



## Versioniertes Root-Gate und Snapshotvertrag

Der Root-Task `mise run test-legacy-ir` baut zuerst das aktuelle
Release-Binary, führt die Mutations-Selbstprobe aus und vergleicht danach eine
neue Aufnahme mit `legacy-ir-baseline.json`. Die Baseline bindet drei getrennte
Inventare:

- direkte Testquellen (`legacy-ir-sources-v1`);
- alle für `find_module` sichtbaren `.moos`/`.moo`-Kandidaten in
  Quellverzeichnissen, Projekt-/Binary-Stdlib und `~/.moo/packages`
  (`moo-find-module-candidates-v1`);
- Host- und Werkzeugidentität einschließlich CPU-Modell/-Flags und Hash/Version
  der verwendeten Shell-, Hash-, Rust-, LLVM- und C-Werkzeuge
  (`legacy-ir-toolchain-v1`).

Alle Inventare werden vor und nach der Aufnahme verglichen. Ein Quellen-,
Importkandidaten-, Toolchain-, Binary- oder Produktinventarwechsel während des
Laufs ist Drift und damit FAIL. Beim Vergleich mit der eingecheckten Baseline
müssen Quellen-, Import- und Toolchain-Snapshot exakt übereinstimmen; der
Compiler-Binary-Hash und das Compiler-/Runtime-Produktinventar bleiben
informativ, weil gerade Änderungen am Compiler geprüft werden.

Die neun offenen Produktfälle werden nicht verschwiegen: Compilefehler enthalten
einen Hash der pfadnormalisierten Diagnose, IR mit sonstigem absolutem
Projektpfad einen unveränderten Roh-IR-Hash. Nur der Root-Task verwendet
`--allow-baseline-failures`; er akzeptiert diese Fälle ausschließlich bei
vollständig identischem Baselineeintrag und meldet ausdrücklich, dass dies kein
Compile-Erfolg ist. Timeout und fehlendes IR bleiben immer FAIL. T4/T33 bleiben
bis zur Produktbereinigung und drei vollständig erfolgreichen Läufen offen.

`legacy_ir_selftest.sh` verändert nur temporäre Kopien der Baseline. Es
mutiert einen IR-Hash und den Import-Snapshot und verlangt in beiden Fällen
einen abgelehnten Vergleich. Produktquellen und die eingecheckte Baseline werden
dabei nicht verändert.
