# P2-T66: hosted x86_64 Systemkoerper (Linux/Windows)

Seit Batch 32944 kompiliert der `@nativ`-Systemkoerper auch ohne Bare-Triple:
hosted x86_64 (Linux `--target x86_64`/native, Windows `x86_64-pc-windows-*`)
sowie hosted aarch64 (`--target aarch64-unknown-linux-gnu`, nur Cross-IR und
Objekt geprueft, kein Lauf; AAPCS64 ohne signext/zeroext an extern C)
fuer `unsicher:`-Koerper mit Zeigern, Strukturwerten oder mehreren Anweisungen.
Im hosted Usermode sind Port-I/O, Barrieren, `assembler`, `volatile_*` und
`registerfeld_*` gesperrt (nur Bare/UEFI). Ein einzelner skalarer extern-C-Aufruf
bleibt im bestehenden FFI-Pfad; sobald Zeiger- oder Strukturtypen in der
Signatur stehen (Parameter oder Rueckgabe), uebernimmt der Systemkoerper, der
extern C mit Zeigern, f32/f64 und allen Ganzzahlbreiten ruft; 8/16-Bit-
Parameter und -Rueckgaben tragen an der Aufrufstelle dieselben signext/zeroext-
Attribute wie Deklaration und skalarer FFI-Pfad (`ExternBinding::
extension_attributes`, Zielregel `FfiType::extension`: SysV ja, Win64/UEFI/
AArch64 nein).
Kandidat fuer den Meilenstein-Lauf: das bisher rote Zukunftsgate
`mise run -C skripte/p2/cpp_adapter handle-zukunft` (Mehrstatement-Koerper mit
`ptr[void]`-Handle) nimmt seit Batch 32944 ebenfalls den Systemkoerper.

Stufe 2a/2b-Aufrufer: hosted Systemkoerper mit REIN SKALARER Signatur (i8..u64,
f32/f64) sind aus dynamischem Moo aufrufbar, aber nur innerhalb eines
dynamischen `unsicher:`-Blocks, nur in main oder einer benannten Top-Level-
Funktion (keine Lambdas, keine Methoden), seit P2-T74 auch innerhalb von
`versuche:` (Fehler sind fangbar, siehe FEHLERVERTRAG) und seit P2-T77 auch in
Funktionen mit `aufraeumen` (Aktionen laufen genau einmal, Erstfehler bleibt;
Bare/Exporte weiterhin gesperrt); der bestehende Box/Unbox-Wrapper
uebernimmt Argumente und Rueckgabe. Schlaegt die Argumentkonvertierung in einer
benannten Funktion fehl (keine darstellbare Ganzzahl), meldet moo_throw den
Fehler: ohne aktiven Handler Prozessende ('Unbehandelter Fehler'); mit aktivem
Aufrufer-Handler kehrt die Funktion seit P2-T74 flag-basiert mit `nichts`
zurueck und das `versuche:` des Aufrufers faengt (Direktform: Aufruf als
Statement-Wurzel mit Variablen-/Literalargumenten, `native_try_direct_root`);
seit P2-T78 gilt das auch fuer die Nicht-Direktform (Aufruf in Argumentposition,
in einem Teilausdruck oder in `versuche:` der Funktion): der Adapter springt in
den Skip-Block des Aufrufs (`native.skip`), der Aufruf liefert `nichts` und der
naechste Guard/Pruefpunkt faengt (kein llvm.trap mehr; Fixture native_system_indirekt;
Guards innerhalb verschachtelter Ausdruecke und in Funktionen ohne eigenes
`versuche:` seit P2-T82, siehe unten);
in main bleibt es ohne Handler beim bisherigen `return 1`.
Reine `@nativ`-Funktionen (ohne `unsicher:`, kein FFI) folgen seit P2-T80 demselben
Argumentvertrag: dynamische Argumente sind hosted in main, in benannten Funktionen
(Direktform: Early-Return mit `nichts`; sonst Skip-Block) und in `versuche:`-
Teilausdruecken fangbar. ADR-Grenze unveraendert: Lambdas/Methoden, C-Exporte
(`@einstieg`, verlieren per set_name den `__moo_native.`-Namen und trappen), FFI-
Koerper und Bare/wasm bleiben Compilefehler bzw. Trap (frontend/dynamisch.sh:
m1_dyn_funktion und m1_dyn_try_verschachtelt positiv, m1_dyn_lambda negativ).
Seit P2-T82 zaehlen solche fangbaren Aufrufe fuer die rekursiven Statement-Guards
(`recursive_statement_expr_supported`, `native_call_fangbar`) als instrumentiert:
bei verschachtelten Operatoren/Aufrufen um einen fehlerhaften Aufruf innerhalb
`versuche:` bleibt der Erstfehler erhalten, Folgeoperationen und Seiteneffekte
auf dem `nichts`-Ergebnis unterbleiben (vorher z.B. 'Division durch Null!' aus
10 / nichts; Fixture native_verschachtelt_try). Benannte Funktionen ohne eigenes
`versuche:`/`aufraeumen`, deren Koerper solche Aufrufe enthaelt, erhalten einen
Pruefpunkt-Ausgang (`native.guard.exit`, `native_guard_exit_block`): die Guards
geben die Werte des Statements frei, die Funktion kehrt wie bei der Direktform mit
`nichts` zurueck und der Aufrufer faengt den Erstfehler (Fixture
native_verschachtelt_funktion). Der try_stack bleibt unberuehrt, FFI-/Systemkoerper-
Schranken und das T74-IR-Gate aendern sich nicht. FEHLERVERTRAG
(P2-T34, ADR f67bfcbd §3): Fehler im Koerper (Division durch 0, ungueltiger Shift,
Arrayindex ausserhalb, ungueltige Konversion, fehlende Rueckgabe) rufen in Moo-
internen hosted Koerpern (`__moo_native.*`) `moo_native_fehler` und kehren mit
Nullwert zurueck: in `versuche:` landet der Fehler im `fange`-Zweig (P2-T74), ohne
Handler beendet moo_throw den Prozess mit RC 1 und Diagnose. Bare/UEFI und
C-Exporte (`@einstieg`) trappen weiter (`llvm.trap`); Zeigerfehler verhalten
sich wie in C. Gates: probe_trap.moos (RC 1, Diagnose), positiv_dyn_in_try.moos,
positiv_dyn_in_funktion.moos und die uebrigen negativ_dyn_*-Faelle.

NICHT freigegeben: Zeiger-/Struktur-Parameter oder -Rueckgaben aus dynamischem
Code (`Nativer Zeigerfunktionsaufruf ...`), Aufrufe aus Lambdas/Methoden oder
aus Funktionen mit `aufraeumen`. Erreichbar bleiben solche Funktionen
native->native und per `@einstieg` aus C.

Gate: `MOO_COMPILER=... mise run -C skripte/p2/hosted test-hosted-linux`
(Argumente gehen an die Mat4-Probe: [runden] [bench_n] [bench_laeufe]).
Quelle/Orakel fuer Mat4 read-only aus `skripte/p2/grafik`. Der Differenzlauf
isoliert die Exporte aus der `--emit-ir`-Ausgabe (vor den Paessen) und
optimiert sie selbst mit `opt -O2`; Schritt 8 prueft zusaetzlich das echte
Produktobjekt.

IR-O2 im Produktobjekt (`write_object`): hosted x86_64 Linux/Windows faehrt
`default<O2>` genau dann, wenn das Programm mindestens einen hosted
Systemkoerper enthaelt. Der dynamische Teil (main, Userfunktionen, Lambdas,
Klassen) ist waehrend der Paesse mit `optnone`/`noinline` geschuetzt und wird
danach wie bisher uebersetzt; optimiert werden nur die `@nativ`-Funktionen.
Programme ohne Systemkoerper (reines Legacy, skalare Pure-/FFI-Funktionen)
nehmen weiterhin den unveraenderten Objektpfad. `--emit-ir` liegt vor den
Paessen (Legacy-IR-Gate unberuehrt).


Float-% und Float-** (P2-T58 D2c): native Funktionen rufen dafuer
`moo_native_fmod`/`moo_native_fmodf`/`moo_native_pow`/`moo_native_powf` aus
`compiler/runtime/moo_native_math.c` (freestanding, kein libm, kein LLVM-`frem`).
Die Moo-Runtime (build.rs) und die Kernel-Pipeline (main.rs KERNEL_SOURCES)
enthalten die Datei bereits. Wer ein natives Objekt ohne Moo-Runtime linkt
(C-Export per `@einstieg`, `llvm-extract`), muss `moo_native_math.o` mitlinken,
sobald das Objekt Float-% oder Float-** benutzt; sonst bleiben genau diese
Symbole offen (Beispiel: `skripte/p2/windows_minipc/test.sh`, Zahlenprobe).
