# Native Moo: Migrationsleitfaden

**Stand:** 14. November 2026. Ergänzung zur Sprachreferenz (`sprachreferenz-entwurf-de.md`). Lauffähige Beispiele liegen in `beispiele/nativ/`; alles andere ist als Vorgehen beschrieben, nicht als fertiges Werkzeug.

## 1. Eine Funktion nativ machen

1. Typen festlegen: Jeder Parameter und der Rückgabetyp bekommen einen Maschinentyp (`i32`, `u8`, `f64` ...). Eine native Funktion braucht eine vollständige Signatur.
2. `@nativ` über die Funktion schreiben. Das Verhalten der übrigen, dynamischen Funktionen bleibt unverändert.
3. Randfälle prüfen, die dynamisch anders liefen: Division und Rest runden gegen null, `+` und `-` laufen modulo 2^N über, Shifts außerhalb `0..N-1` sind Fehler (`beispiele/nativ/02_ganzzahl_semantik.moos`).
4. Umwandlungen ausdrücklich schreiben: `als(T, x)` ist geprüft, `als_wrap` und `als_saettigend` wählen Überlauf oder Sättigung (`beispiele/nativ/03_casts.moos`).
5. Aufrufstellen: Native Funktionen rufen einander ohne Boxing. Der Aufruf aus dynamischem Code geht über einen Adapter, der Anzahl, Typ und Wertebereich prüft; ein Integer passiert den dynamischen Zahlenpfad nur, wenn er exakt in `f64` darstellbar ist.

## 2. Eine C-Runtime-Funktion nach nativem Moo verschieben

1. Vorher festhalten: Signatur, Ownership, Fehlerverhalten und ein Test mit erwarteter Ausgabe der C-Version.
2. Die Funktion mit gleichen Typbreiten als `@nativ` schreiben und die erwartete Ausgabe vergleichen.
3. Arithmetik gezielt prüfen: Nativ wrappt Integer-Überlauf definiert; der C-Code muss dafür nicht auf `-fwrapv` angewiesen gewesen sein. Wo die C-Version auf Überlauf hin undefiniertes Verhalten hatte, ist das neue Verhalten die Referenz.
4. Daten in Puffern statt in dynamischen Listen halten (`puffer_erstellen(i32, n)`), wenn die Funktion viele Werte liest oder schreibt.
5. Die C-Funktion erst entfernen, wenn Test und Vergleich bestanden sind.

## 3. Eine C-Bibliothek binden

1. Nur skalare Ganzzahl-/Gleitkommaparameter, Pointer und `void` verwenden. `bool`, Aggregate und Funktionstypen by value lehnt der Compiler derzeit ab; für `bool` stattdessen eine Ganzzahl (`i32` oder `u8`) in einer C-Hülle verwenden.
2. Deklaration schreiben: `@bibliothek(linux="name")` und `extern "C" funktion name(a: i8, b: u8) -> i32`. Aufrufe stehen in einer `@nativ`-Funktion innerhalb von `unsicher:`.
3. Mit einem C-Gegenprogramm je Zielsystem prüfen (Vorlage: Tests `bibliothek_ziel_*`, `ffi_i64*` in `compiler/tests/p2`).
4. Callbacks und `vec`/`mat` sind nicht freigegeben und gehören nicht in produktive Bindungen.
5. C++ nur über einen `extern "C"`-Adapter mit opakem Handle anbinden; der Adapter fängt Exceptions ab.

## 4. Dynamische Daten über die Grenze reichen

Zahlen oberhalb 2^53 sind nicht jede exakt im dynamischen Pfad darstellbar; der Übergang meldet dann einen Grenzfehler. Ein exakter Integer-Container ist erst eine spätere Stufe.

## 5. Prüfliste vor dem Merge

- Alle Dateien in `beispiele/nativ/` laufen mit `moo-compiler run` und liefern die im Kopf notierte Ausgabe.
- Bestehende dynamische Programme verhalten sich unverändert, wenn keine native Funktion benutzt wird.
- Jede FFI-Bindung hat ein C-Gegenprogramm je freigegebenem Ziel.
