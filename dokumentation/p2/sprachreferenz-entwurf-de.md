# Native Moo: Sprachreferenz (Entwurf)

**Stand:** 14. November 2026. Diese Referenz gleicht den P2-Sprachvertrag mit dem implementierten Compiler ab. Beispiele mit Verweis auf `beispiele/nativ/` laufen mit `moo-compiler run`; alle anderen Snippets sind Zielvertrag und nicht kompiliert. Abschnitte, die noch nicht implementiert sind, tragen den Hinweis „nicht freigegeben“ und stehen in der Statustabelle am Ende. Migrationsleitfaden: `migrationsleitfaden-de.md`.

## Geltungsbereich und Opt-in

Dynamisches Moo bleibt die Voreinstellung. Typannotationen allein ändern dessen Verhalten nicht. Eine native Funktion wird explizit mit `@nativ` oder `@native` markiert und besitzt eine vollständige Signatur einschließlich Rückgabetyp. `@nativ` macht eine Funktion nicht automatisch unsicher.

```moos
@nativ
funktion addiere(a: i32, b: i32) -> i32:
    gib_zurück a + b
```

Die gezeigte Syntax ist implementiert (`beispiele/nativ/01_native_funktionen.moos`); native Funktionen dürfen einander auch vorwärts aufrufen. Die bestehende dynamische Semantik, `als_u8` bis `als_u64`, Klassen, Datenklassen und das MooValue-Layout werden dadurch nicht umgedeutet.

## Typen und Literale

Die numerischen Typen heißen `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32` und `f64`; dazu kommt `bool`. `usize` und `isize` sind zielpointerbreit und keine portablen Wire-Typen. `void` steht für eine fehlende Rückgabe beziehungsweise einen opaken Pointer-Zieltyp, nicht für einen gewöhnlichen gespeicherten Wert. Diese Namen sind nur im Typkontext reserviert.

Ein Literal erhält direkt den erwarteten Typ, etwa aus Parameter, Feld, Zuweisung, Rückgabe oder einem kompatiblen typisierten Nachbaroperanden. Ohne Kontext ist ein Integerliteral `i64`, ein Kommaliteral `f64`. Nur Literale passen sich automatisch an; widersprüchliche Kontexte und Integer außerhalb des Zielbereichs sind Compilefehler.

Ganzzahlen werden exakt aus Quellziffern und Basis gelesen: dezimal, `0x`, `0b` und `0o`. Eine führende Null allein bedeutet dezimal. Der negative Gesamtwert wird geprüft, sodass `-9223372036854775808` in `i64` passt. Optionale Suffixe wie `5u8` sind für später vorgesehen.

Kommaliterale werden unmittelbar auf den Zieltyp gerundet: nächster Wert, bei Gleichstand gerade. Normale Rundung von `0.1` ist zulässig; ein endliches Literal, das zu Unendlich rundet, ist ein Fehler. Subnormale Werte sind zulässig. Ein mathematisch von null verschiedener Wert, der auf vorzeichenbehaftete Null rundet, erzeugt standardmäßig eine Warnung.

## Arithmetik und Konversionen

Integer-`+`, `-`, `*` und unäres Minus laufen modulo 2 hoch N über, für vorzeichenbehaftete und vorzeichenlose Typen gleich in Debug und Release. Größen-, Offset- und Allokationsberechnungen werden dagegen geprüft.

Integerdivision rundet gegen null; der Rest trägt das Vorzeichen des Dividenden. Division oder Rest durch null, `MIN / -1`, `MIN % -1` und Schiebebeträge außerhalb `0..N-1` sind Fehler. Statisch erkennbare Fehler werden beim Kompilieren gemeldet; dynamische Fehler sind hosted fangbar und ohne Handler ein definierter Abbruch. Im Bare-Profil behandelt sie ein nicht zurückkehrender Ziel-Hook. Rechtsverschiebung ist bei signed arithmetisch, bei unsigned logisch.

Der geplante Standardcast `als(T, x)` ist geprüft. Integer außerhalb des Zielbereichs und Floatwerte mit Bruchteil, NaN, Unendlich oder außerhalb des Integerzielbereichs ergeben einen Fehler. Gewolltes Wrapping, Sättigung und Rundung werden ausdrücklich angefordert; sie heißen `als_wrap(T, x)` (Wrapping) und `als_saettigend(T, x)` (Sättigung). Siehe `beispiele/nativ/02_ganzzahl_semantik.moos` und `beispiele/nativ/03_casts.moos`. Integer→Float und Float→Float erlauben dokumentierte Rundung, aber kein stilles endlich→unendlich; für exakte Rundreisen ist eine separate Prüfung vorgesehen.

Die Float-Grundsemantik folgt IEEE für `f32` und `f64`, einschließlich NaN, Inf, vorzeichenbehafteter Null und graduellem Unterlauf. Reassociation, implizite FMA-Kontraktion, Flush-to-zero und Fast-Math sind standardmäßig aus; abweichende Optimierungen erfordern ein ausdrückliches Opt-in.

## Zusammengesetzte Typen und Layout

Geplant sind `[T; N]` für feste Arrays, `puffer[T]`/`buffer[T]`, `zeiger[T]`/`ptr[T]` und Funktionstypen `fn(T, ...) -> R`.

```moos
struktur Vertex:
    position: [f32; 3]
    farbe: [u8; 4]
```

Verschachtelte, gepackte Strukturen mit festem Array-Feld kompilieren bereits (`beispiele/nativ/05_struktur_layout.moos`; das Beispiel registriert nur das Layout). `struktur`/`struct` besitzt Wertsemantik und ein vom gewählten Ziel-ABI bestimmtes Layout mit Feldreihenfolge, Größe, Ausrichtung und Offsets. Die Kopie einer Struktur kopiert ihre Werte; ein darin enthaltener Puffer-Handle bleibt gemeinsam genutzt. Das Layout ist kein universelles Dateiformat: Byteordnung und Serialisierung sind getrennte Verträge.

`@gepackt`/`@packed` bedeutet im ersten Inkrement Alignment 1 ohne implizite Feldpolsterung. Widersprüchliche Kombinationen mit expliziter höherer Ausrichtung werden abgelehnt. Nicht ausgerichtete Felder benötigen geeignete Zugriffe; eine normal ausgerichtete Referenz darf daraus nicht vorgetäuscht werden. Padding ist weder Vergleichsinhalt noch ungeprüft zu serialisierender Nutzinhalt.

## Puffer, Zeiger und unsicher

Ein Puffer wird mit `puffer_erstellen(i32, 3)` angelegt und per Index gelesen und geschrieben (`beispiele/nativ/04_puffer.moos`). Ein `puffer[T]` verwaltet zusammenhängenden Speicher, prüft Grenzen und hält seinen Owner per Referenzzählung. Views halten den Owner am Leben. Solange Views oder exportierte Pins aktiv sind, sind Längenänderungen und Reallokation gesperrt. Geteilte Handles teilen auch sichtbare Elementänderungen; eine tiefe Kopie wird ausdrücklich angefordert. Veränderliche Puffer sind im ersten sicheren Inkrement threadgebunden.

Pointertypen dürfen deklariert, Pointerwerte kopiert und auf null verglichen werden. Integeradressen in Pointer umwandeln, dereferenzieren, freie Pointerarithmetik, ungeprüfte Zugriffe, rohe FFI-Aufrufe und MMIO erfolgen ausdrücklich in `unsicher`/`unsafe`. Volatile-Zugriffe ersetzen keine Atomics oder Hardwarebarrieren. Ein roher Pointer muss gültig, passend ausgerichtet und für die vertragliche Dauer lebendig sein.

## C-Anbindung und Callbacks

```moos
@bibliothek(linux="p2_ffi_schmal")
extern "C" funktion p2_ffi_schmal(a: i8, b: u8) -> i32

@nativ
funktion schmal_probe() -> i32:
    unsicher:
        gib_zurück p2_ffi_schmal(-1, 255)
```

Das Snippet stammt aus dem Test `bibliothek_ziel_linux`; es braucht eine passende C-Bibliothek und ist deshalb kein Beispiel unter `beispiele/nativ/`. Das erste FFI-Inkrement umfasst feste skalare Integer-/Floatparameter, Pointer und `void`; die Bibliothek kann je Ziel benannt werden (`linux="..."`). Der Compiler lehnt derzeit ab: Aggregate und Funktionstypen by value (Meldung „Aggregate und Funktionstypen by value sind nicht freigegeben“) sowie `bool` (Meldung „C-bool ist bis zum ABI-Nachweis nicht freigegeben“). Variadics bleiben bis zum nachgewiesenen Ziel-ABI-Lowering abgelehnt. C-`_Bool`, C++-`bool` und Win32 `BOOL` sind getrennt zu prüfen. Zieltriple, Calling Convention und Datenlayout stehen vor der IR-Erzeugung fest.

Eine Bindung beschreibt Borrow oder Transfer, Lebensdauer, Nullable, Mutierbarkeit, Pinning, Deallocator und Fehlerbehandlung. Fremde Bibliotheken dürfen Puffer nur für die vereinbarte Dauer behalten. C++ wird zunächst über einen `extern "C"`-Adapter mit opakem Handle angebunden; der Adapter fängt C++-Exceptions ab.

Callbacks (Parameter vom Typ `fn(i32) -> i32` in `extern "C"`) sind nicht freigegeben: der Compiler weist sie mit derselben Meldung wie Aggregate ab. Der folgende Vertrag gilt für die spätere Freigabe. Callbacks benötigen einen C-ABI-Thunk mit exakter Signatur. Captures werden über einen expliziten `user_data`-Vertrag verwaltet. Registrierung und Abmeldung müssen laufende und spätere Aufrufe berücksichtigen. Fehler dürfen nicht unkontrolliert durch fremde C-Frames entweichen; `void`-Callbacks benötigen einen externen Fehlerkanal. Fremde Threads und Reentranz werden unterstützt oder erkennbar abgewiesen.

## Vektoren und Matrizen

Nicht freigegeben: Im Compiler existieren nur skalare LLVM-Bausteine (`native_vecmat.rs`, f32, Matrix spaltenweise); es gibt weder Moo-Syntax noch Tests. Der folgende Vertrag ist Zielzustand. `vec2`, `vec3`, `vec4` und `mat4` sind mathematische `f32`-Werttypen; explizite `f64`-Varianten sind möglich. Eine Matrix transformiert einen Spaltenvektor als Matrix mal Vektor. Matrix mal Matrix ist Matrixmultiplikation, Vektor mal Vektor komponentenweise; das Skalarprodukt ist eine benannte Operation. Ein bestimmtes SIMD- oder GPU-Layout wird nicht vorausgesetzt. Row-/Column-Major- und std140-/std430-Grenzen brauchen ausdrückliche Adapter.

## Dynamische Grenze und Migration

Ein dynamischer Aufruf nativer Funktionen verwendet einen Adapter, der Anzahl und Typen der Argumente, Wertebereiche und Ownership prüft. Native Aufrufe untereinander benötigen kein unnötiges Boxing. Ein Integer darf den bestehenden dynamischen Number-Pfad nur durchlaufen, wenn genau dieser Wert in `f64` darstellbar ist; andernfalls gibt es einen Grenzfehler. Manche Werte oberhalb 2^53 sind exakt darstellbar. Eine exakte Integer-Box ist eine spätere additive Stufe nach eigenem Runtime-Gate.

Für eine C-Funktion zuerst Ziel-ABI, Signatur, Ownership, Fehler und Tests festlegen; dann native Funktion und explizite Adapter ausarbeiten. Für eine C-Bibliothek ihre skalaren Signaturen und Linknamen deklarieren und mit C-Gegenprogrammen auf jedem freigegebenen Ziel prüfen. Keine C++-Klasse oder fremde Ressource allein aufgrund eines passenden Layouts direkt übernehmen.

## Verifikation und Status

| Bereich | Stand 14.11.2026 | Beleg |
|---|---|---|
| `@nativ`-Funktionen, Vorwärtsaufrufe | implementiert | `beispiele/nativ/01_native_funktionen.moos` |
| Ganzzahlsemantik (Division, Rest, Shifts, Wrap) | implementiert | `beispiele/nativ/02_ganzzahl_semantik.moos` |
| Casts `als`, `als_wrap`, `als_saettigend` | implementiert | `beispiele/nativ/03_casts.moos` |
| Typisierte Puffer | implementiert (Grundform) | `beispiele/nativ/04_puffer.moos` |
| Strukturen, `@gepackt`, feste Arrays | Deklaration implementiert | `beispiele/nativ/05_struktur_layout.moos` |
| `extern "C"`, `@bibliothek(ziel=...)`, `unsicher` | implementiert für skalare Signaturen und Pointer | Tests `bibliothek_ziel_*`, `ffi_*`, `zeiger_lesen*` in `compiler/tests/p2` |
| Arrays by value und Callbacks in FFI (C-`bool` und Strukturwerte sind seit P2-T19/T56 freigegeben) | abgelehnt (nicht freigegeben) | Tests `ffi_aggregat`, `ffi_callback_*` |
| `vec2/3/4`, `mat4` | nur Compiler-Bausteine, keine Sprachsyntax | `compiler/src/native_vecmat.rs` |

Geltung für Linux, Windows und Bare setzt jeweils eigene Nachweise voraus; die Beispiele wurden auf Linux einzeln ausgeführt.
