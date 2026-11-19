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


### Statische native Strukturwerte (P2-T124)

`@nativ statisch NAME: Struktur auf Struktur(...)` legt beschreibbaren Speicher
mit statischer Lebensdauer an. Der Konstruktor initialisiert die Felder in
Deklarationsreihenfolge; `Struktur()` initialisiert alle Felder einschließlich
verschachtelter Strukturen und Arrays mit ihrem Nullwert. Die bestehende Form
`auf 0` nullt ebenfalls statische Aggregate. Skalare statische Variablen sind
weiterhin nicht freigegeben.

```moos
struktur Verbindung:
    zustand: u64
    empfang: u64

@nativ
statisch TCP: Verbindung auf Verbindung()

@nativ
funktion empfangen(neu: u64) -> u64:
    unsicher:
        TCP.empfang = neu
        gib_zurück TCP.empfang
```

Direkte Felder, eingebettete Strukturen, Arrayfelder, Wertkopien und
`adresse_von(TCP.empfang)` verwenden denselben zielabhängigen Layoutvertrag
wie lokale Strukturwerte. Der Zugriff auf beschreibbaren gemeinsamen
statischen Speicher verlangt `unsicher`; direkte Schreibzugriffe auf native
Konstanten werden abgewiesen. Eine Strukturkopie kopiert die Feldwerte, nicht
den Speicherort der Struktur. Beleg: `sys_t124_tcp` und
`sys_t124_statisch` in `skripte/p7/lader/system_koerper.moos`, C-Gegenprobe
`system_probe.c` einschließlich aller 25 TCP-Slot-Feldpositionen.

## Puffer, Zeiger und unsicher

Ein Puffer wird mit `puffer_erstellen(i32, 3)` angelegt und per Index gelesen und geschrieben (`beispiele/nativ/04_puffer.moos`). Ein `puffer[T]` verwaltet zusammenhängenden Speicher, prüft Grenzen und hält seinen Owner per Referenzzählung. Views halten den Owner am Leben. Solange Views oder exportierte Pins aktiv sind, sind Längenänderungen und Reallokation gesperrt. Geteilte Handles teilen auch sichtbare Elementänderungen; eine tiefe Kopie wird ausdrücklich angefordert. Veränderliche Puffer sind im ersten sicheren Inkrement threadgebunden.

Pointertypen dürfen deklariert, Pointerwerte kopiert und auf null verglichen werden. Integeradressen in Pointer umwandeln, dereferenzieren, freie Pointerarithmetik, ungeprüfte Zugriffe, rohe FFI-Aufrufe und MMIO erfolgen ausdrücklich in `unsicher`/`unsafe`. Volatile-Zugriffe ersetzen keine Atomics oder Hardwarebarrieren. Ein roher Pointer muss gültig, passend ausgerichtet und für die vertragliche Dauer lebendig sein.


### Nicht besitzende native Byteansichten (P2-T124)

`byteansicht(array)` (Alias `byte_view`) leiht ein vorhandenes
`[u8; N]` einschließlich eines Struktur-Arrayfelds ohne Kopie. Das Ergebnis
hat den nativen Deskriptortyp `puffer[u8]` mit Datenzeiger und Länge.
Eine Konstante kann nicht als schreibbare Ansicht geliehen werden.

`byteansicht(zeiger, länge)` mit `ptr[u8]` und `u64` verlangt
`unsicher`. Es prüft Nullzeiger bei nichtleeren Bereichen und den Überlauf
der beschriebenen Adressspanne. Der Aufrufer verantwortet weiterhin die
Gültigkeit und Lebensdauer des RAM-Bereichs sowie nötige Synchronisation.
Die Ansicht allokiert, besitzt und pinnt keinen Speicher; insbesondere hält
sie keinen DMA-Puffer oder fremden Owner selbständig am Leben. Lokaler
Speicher darf nicht über den Aufruf hinaus entkommen. Eine native
Pufferrückgabe und ein C-/UEFI-Export mit Pufferparameter bleiben gesperrt;
interne native Aufrufe unterstützen den Deskriptor hosted und im Bare-Profil.

| Zugriff | Werttyp | Byteordnung |
|---|---|---|
| `byte_lesen_be16` / `byte_schreiben_be16` | `u16` | höchstwertiges Byte zuerst |
| `byte_lesen_le16` / `byte_schreiben_le16` | `u16` | niedrigstwertiges Byte zuerst |
| `byte_lesen_be32` / `byte_schreiben_be32` | `u32` | höchstwertiges Byte zuerst |
| `byte_lesen_le32` / `byte_schreiben_le32` | `u32` | niedrigstwertiges Byte zuerst |
| `byte_lesen_be64` / `byte_schreiben_be64` | `u64` | höchstwertiges Byte zuerst |
| `byte_lesen_le64` / `byte_schreiben_le64` | `u64` | niedrigstwertiges Byte zuerst |

Lesen erhält `(ansicht, versatz)`, Schreiben
`(ansicht, versatz, wert)`; der Byteversatz ist `u64`. Der vollständige
Wortbereich wird vor dem ersten Speicherzugriff geprüft:
`versatz <= länge` und `wortbreite <= länge - versatz`.
Ein zu kurzer Schreibbereich verändert keine Bytes. Die Zugriffe sind
unaligned-fähige normale RAM-Bytezugriffe mit Alignment 1. Sie ersetzen weder
`volatile_*` für MMIO noch Atomics oder Gerätebarrieren.

```moos
@nativ
funktion paketwort() -> u32:
    setze paket: [u8; 5] auf [0, 0, 0, 0, 0]
    setze b: puffer[u8] auf byteansicht(paket)
    byte_schreiben_be32(b, 1, als(u32, 0x12345678))
    gib_zurück byte_lesen_be32(b, 1)
```

`@liest(n)` erlaubt die Lesehelfer auf dem markierten Pufferparameter,
aber keine Schreibhelfer oder schreibbaren Ansichtsaliase. Bereichsfehler
nutzen den vorhandenen nativen Fehlervertrag (hosted fangbar, Bare-Abbruch
bzw. Trap am C-Export). `als(T, x)` bleibt bereichsgeprüft. `T(x)` behält
seinen bisherigen Emitterpfad; im Systemkörper sind solche Konversionen
ungeprüft und verlangen `unsicher`. Die Vereinheitlichung ist P2-T125.
Belege: `sys_t124_array`/`sys_t124_bytes` und das
C-Wireformat-Orakel unter `skripte/p7/lader/`, hosted Fehlerfortsetzung und
unveränderte Bytes in `compiler/tests/test_native_byteansicht.moos`.

### Native Cleanup-Bloecke (P2-T126)

`aufräumen: aufruf(...)` (Alias `defer:`) registriert einen nativen
Funktionsaufruf fuer das Ende des aktuellen Blocks. Nur tatsaechlich
erreichte Registrierungen laufen, in umgekehrter Reihenfolge. Das gilt
auch fuer `gib_zurück`, `stopp`, `weiter` und vom Compiler gepruefte
Laufzeitfehler. Bei einem Fehler in einem nativen Callee werden die
aktiven Caller-Cleanups ebenfalls ausgefuehrt.

Wie beim vorhandenen Hochsprachen-Cleanup werden die Argumentausdruecke
erst beim Aufraeumen ausgewertet; lokale Namen bezeichnen ihre dann
aktuellen Werte. Ein Rueckgabewert beziehungsweise die Kopie eines
Strukturresultats wird vorher bestimmt. Eine `unsicher:`-Berechtigung
gilt am Registrierungsort; Cleanup erlaubt keinen versteckten Rohzugriff.

```moo
unsicher:
    aufräumen: config_schliessen(port)
    config_oeffnen(port)
    wenn chip_passt(port):
        setze vorher: u8 auf ldn_lesen(port)
        aufräumen: ldn_schreiben(port, vorher)
        ldn_schreiben(port, 7)
        # LDN-Restore vor Config-Close, auch bei vorzeitigem Austritt.
```

Hosted bleibt der erste Fehler erhalten, auch wenn eine Cleanup-Aktion
selbst fehlschlaegt. Die uebrigen Aktionen laufen weiter. Bare/UEFI
arbeitet die aktive native Cleanup-Kette vor dem terminalen Trap ab.
Die Frames liegen auf dem Stack; es gibt keine Heap-Allokation fuer
native Registrierungen. Hosted ist die Kette thread-lokal; Bare nutzt
den bestehenden einzelnen Kernel-Ausfuehrungskontext. Fremdes
`abort`/`noreturn`, CPU-Faults und physische Geraetefehler sind keine
vom Compiler weitergegebenen Sprachfehler.

Der IT8613-Treiber benutzt innere Bloecke, damit Config-Close weiterhin
vor den Logaufrufen erfolgt. Portfolge und Registerwerte bleiben explizit.

### Benannte Registerwrites (P2-T126)

Die bestehenden Writes bleiben normale Wertwrites. Die Suffixe `_w1c`
und `_write0` benennen Statusquittierungen auf denselben Port-/MMIO-Pfaden:

| Zugriff | Schreibwert |
|---|---|
| `port_schreibe(port, wert)` | `wert` |
| `port_schreibe_w1c(port, maske)` | `maske` |
| `port_schreibe_write0(port, vorher, maske)` | `vorher & ~maske` |

Die Varianten gelten ebenso fuer `port_schreibe16/32`,
`volatile_schreiben(zeiger[, index], ...)` und
`registerfeld_schreiben(Struktur, Feld, adresse, ...)`; EN-Basisnamen
`port_out*`, `volatile_write` und `register_field_write` tragen dieselben Suffixe.
Snapshot und Maske brauchen exakt denselben vorzeichenlosen Registertyp
(`u8/u16/u32/u64`; Ports nur 8/16/32 Bit). Alle Zugriffe verlangen
`unsicher` und das Bare-/UEFI-Profil.

Jede Operation erzeugt genau einen Schreibzugriff. Sie liest das Register
nicht selbst. Der Treiber liest einen benoetigten Snapshot ausdruecklich
zuvor; so kann Write0 beim IT8613 das Statusbit 0 quittieren und Taktbit 3
erhalten. W1C schreibt die uebergebene Maske direkt. Registervertrag,
Schreibreihenfolge und Barrieren bleiben beim Treiber sichtbar.

## C-Anbindung und Callbacks

```moos
@bibliothek(linux="p2_ffi_schmal")
extern "C" funktion p2_ffi_schmal(a: i8, b: u8) -> i32

@nativ
funktion schmal_probe() -> i32:
    unsicher:
        gib_zurück p2_ffi_schmal(-1, 255)
```

Das Snippet stammt aus dem Test `bibliothek_ziel_linux`; es braucht eine passende C-Bibliothek und ist deshalb kein Beispiel unter `beispiele/nativ/`. Das erste FFI-Inkrement umfasst feste skalare Integer-/Floatparameter, Pointer und `void`; die Bibliothek kann je Ziel benannt werden (`linux="..."`). Strukturen by value (Parameter und Rückgabe, `beispiele/nativ/11_struktur_by_value.moos`) und C-`bool` sind freigegeben (P2-T19/T56). Weiterhin abgelehnt sind feste Arrays und bloße Funktionstypen by value (Meldung „Aggregate und Funktionstypen by value sind nicht freigegeben“); Funktionszeiger gehen nur als `ptr[fn(...) -> R]` mit `@rückruf` (siehe unten). Variadics bleiben bis zum nachgewiesenen Ziel-ABI-Lowering abgelehnt. C-`_Bool`, C++-`bool` und Win32 `BOOL` sind getrennt zu prüfen. Zieltriple, Calling Convention und Datenlayout stehen vor der IR-Erzeugung fest.

Eine Bindung beschreibt Borrow oder Transfer, Lebensdauer, Nullable, Mutierbarkeit, Pinning, Deallocator und Fehlerbehandlung. Fremde Bibliotheken dürfen Puffer nur für die vereinbarte Dauer behalten. C++ wird zunächst über einen `extern "C"`-Adapter mit opakem Handle angebunden; der Adapter fängt C++-Exceptions ab.

Callbacks sind freigegeben (P2-T21/T57). Eine `@nativ`-Funktion auf oberster Ebene mit `@rückruf` erhält einen C-ABI-Thunk und wird als `ptr[fn(...) -> R]` übergeben (`beispiele/nativ/10_rueckruf.moos`, dort mit libc-`qsort`). Ohne `@rückruf` ist eine native Funktion kein C-Funktionszeiger; Lambdas, Methoden und dynamische Funktionen sind nicht zulässig, stattdessen gilt das `user_data`-Muster. Fehlbare Rückrufe brauchen einen vereinbarten Fehlerwert (`@rückruf(0)`, Test `rueckruf_fehler`): C läuft zu Ende, der Moo-Aufrufer prüft unmittelbar danach. Aufrufe von einem fremden C-Thread aus sind getestet (`rueckruf_sicher`). Im Einzelnen: Callbacks benötigen einen C-ABI-Thunk mit exakter Signatur. Captures werden über einen expliziten `user_data`-Vertrag verwaltet. Registrierung und Abmeldung müssen laufende und spätere Aufrufe berücksichtigen. Fehler dürfen nicht unkontrolliert durch fremde C-Frames entweichen; `void`-Callbacks benötigen einen externen Fehlerkanal. Fremde Threads und Reentranz werden unterstützt oder erkennbar abgewiesen.

## Vektoren und Matrizen

Freigegeben (P2-T16): `vec2`, `vec3`, `vec4` und `mat4` sind eingebaute Sprachtypen (`beispiele/nativ/06_vec_mat.moos`). Es gibt Operatoren, `punkt`, `kreuz`, `betrag` und `normalisiere` (englisch `dot`, `cross`, `magnitude`, `normalize`), Feldzugriff wie `m.s3.x`, und die Typen sind als Parameter, Rückgabe, Strukturfeld, Arrayelement und in `puffer[vec3]` nutzbar. Ganzzahlkomponenten und Mischungen werden abgelehnt (der Skalar muss `f32` sein); `länge` bleibt die Elementanzahl. Eine eigene `struktur vec3` überdeckt die eingebaute (Test `vecmat_eigene`). `vec2`, `vec3`, `vec4` und `mat4` sind mathematische `f32`-Werttypen; explizite `f64`-Varianten sind möglich. Eine Matrix transformiert einen Spaltenvektor als Matrix mal Vektor. Matrix mal Matrix ist Matrixmultiplikation, Vektor mal Vektor komponentenweise; das Skalarprodukt ist eine benannte Operation. Ein bestimmtes SIMD- oder GPU-Layout wird nicht vorausgesetzt. Row-/Column-Major- und std140-/std430-Grenzen brauchen ausdrückliche Adapter.

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
| Strukturen by value und C-`bool` in FFI | implementiert (P2-T19/T56) | `beispiele/nativ/11_struktur_by_value.moos` |
| Feste Arrays und bloße Funktionstypen by value in FFI | abgelehnt (nicht freigegeben) | Test `ffi_aggregat` |
| C-Rückrufe `@rückruf` | implementiert (P2-T21/T57) | `beispiele/nativ/10_rueckruf.moos` |
| Opake Griffe (`@opak`, `@verbraucht`), C++ über extern-C-Adapter | implementiert (P2-T23/T53) | `beispiele/nativ/12_opaker_griff.moos`, Tests `cpp_griff*` |
| Zeigerarithmetik, Umdeutung `zeiger(p)` | implementiert (P2-T17/T51/T55) | `beispiele/nativ/07_zeiger.moos` |
| Leihgabe `@liest` (Puffer, Tensor, Frame ohne Kopie) | implementiert (P2-T83) | `beispiele/nativ/08_leihgabe_liest.moos` |
| Native Aufrufe aus Methoden und Lambdas | implementiert (P2-T85) | `beispiele/nativ/09_aufruf_aus_methoden.moos` |
| Strukturen in dynamischem Code | implementiert (P2-T49) | `beispiele/nativ/13_struktur_dynamisch.moos` |
| `vec2/3/4`, `mat4`, `betrag`/`magnitude` | implementiert (P2-T16) | `beispiele/nativ/06_vec_mat.moos` |

Geltung für Linux, Windows und Bare setzt jeweils eigene Nachweise voraus; die Beispiele wurden auf Linux einzeln ausgeführt.
