# P2: C++-Adapter mit C-ABI

Das Beispiel unter `compiler/tests/p2/m1_cpp_adapter.{h,cpp}` hält die
C++-Klasse hinter einem opaken `void *`. Moo kennt nur C-Symbole, keine
C++-ABI. Der Adapter wird als C++17-Objekt gebaut; `m1_cpp.moos` bindet
die statische Bibliothek und auf Linux `stdc++` über `@bibliothek` ein.

## Vertrag

- `p2_cpp_create(value)` erzeugt einen Handle; bei ungültigem Wert oder
  einer C++-Exception liefert es null. Der Aufrufer besitzt einen erfolgreichen
  Handle und muss ihn genau einmal an `p2_cpp_destroy` geben.
- `p2_cpp_value(handle)` liest den Wert; `p2_cpp_destroy(handle)` gibt das
  Objekt frei und liefert null bei Erfolg. Ein Null-Handle liefert -1.
- `p2_cpp_example(value)` ist die skalare M1-Brücke: Sie erzeugt, verwendet
  und zerstört den Handle intern. Bei Fehler liefert sie -1. Der Beispieltyp
  erlaubt nur nichtnegative Werte; -1 ist deshalb eindeutig ein Fehlercode.
- Jeder C++-Einstieg fängt Exceptions, sodass keine Exception die C-Grenze
  überschreitet. Gültige Handles dürfen nicht nach ihrer Freigabe oder mehrfach
  freigegeben werden. Fremde oder beschädigte Zeiger sind kein gültiger Input.

- `p2_cpp_vector_sum(values, length)` kopiert bis zu 4096 `int32_t`-Werte
  in einen lokalen C++-Vektor und gibt ihre 64-Bit-Summe zurück. Der Aufrufer
  behält seinen Speicher während des Aufrufs gültig; der Adapter speichert
  keinen Zeiger. Bei null mit positiver Länge, zu großer Länge oder Exception
  wird `INT64_MIN` zurückgegeben. Ein Nullzeiger mit Länge null ist gültig.
  Der Wert kann bei höchstens 4096 Eingaben nicht als Summe auftreten.

Der aktuelle Moo-Aufruf benutzt die skalare Brücke, weil direkte Übergabe
opaker Zeiger durch Moo noch nicht freigegeben ist. Das separate Beispiel
`m1_cpp_throw.moos` wandelt den Fehlercode anschließend in `wirf` um und
fängt ihn; der FFI-Aufruf selbst erfolgt vorher auf oberster Ebene, nicht in
`try` oder einer dynamischen Funktion. Der Vektor-Einstieg ist bislang
nur vom C-Test aufgerufen; die Zeiger-/Längenübergabe aus Moo bleibt offen.
`std::string`, Puffer, Templates, Overloads und RTTI sind nicht abgedeckt.

## Tests

```sh
mise run -C skripte/p2/cpp_adapter test
mise run -C skripte/p2/cpp_adapter sanitize
mise run -C skripte/p2/cpp_adapter integration
mise run -C skripte/p2/cpp_adapter throw
mise run -C skripte/p2/cpp_adapter bindgen
```

`test` prüft das C-ABI, Handle-Lebensdauer, Null- und Exceptionfälle.
`sanitize` instrumentiert den isolierten C/C++-Test mit ASan, LSan und
UBSan; es instrumentiert nicht den gesamten Moo-Prozess. `integration`
prüft Moo-Ausgaben und den nativen i32-C-Call im LLVM-IR. `bindgen` prüft
zweifache deterministische Generierung aus dem tatsächlichen Adapterheader.
`throw` prüft die explizite Umsetzung des C-Fehlercodes in einen gefangenen
Moo-Fehler, einschließlich erfolgreichem Aufruf und Fortsetzung danach.

`mise run -C skripte/p2/cpp_adapter handle-zukunft` ist ein separates, derzeit
rotes Zukunftsgate: Moo soll einen opaken Handle erzeugen, lesen und freigeben.
Es setzt native Pointer und FFI in Funktionen voraus. Im geplanten Lauf werden
C++-Adapter und -Harness mit ASan/UBSan instrumentiert, das extrahierte Moo-IR
aber nicht; ASan/LSan prüft unter anderem Lecks des Adapters. Das ist keine
vollständige Instrumentierung des Moo-Codes oder der Moo-Runtime.
Der Integrationslauf benötigt einen Compiler mit dem nativen FFI-Slice.
Das Beispiel ist auf Linux getestet; Windows ist damit nicht abgedeckt.
