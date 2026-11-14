# P2: C-ABI-Referenz und Adapterentwurf

Dieses Verzeichnis enthält eine eigenständige C-Gegenstelle und einen C-Caller
in getrennten Übersetzungseinheiten. Der aktuelle C-zu-C-Test prüft nur die
jeweilige C-Toolchain, nicht den noch nicht implementierten Moo-FFI-Pfad.
Exportierte Namen und Signaturen stehen in `ffi_reference.h`.

Der Test umfasst Argumente und Rückgaben aller festen Integerbreiten,
f32/f64, Zeiger (inklusive NULL), void mit beobachtbarer Ausgabe, gemischte
Integer-/Float-/Zeigerargumente und C-`_Bool`. Grenzwerte umfassen auch
2^53+1 und u64-max. `_Bool` ist ein eigener Fall; Win32 BOOL ist kein
Synonym. Bis zum nachgewiesenen Bool-Lowering muss Moo ihn zurückweisen.

`moo_p2_variadic` und `moo_p2_aggregate_by_value` sind bewusst gültige
C-Funktionen. Der C-Caller prüft nur die Referenz; ein späterer Moo-Negativtest
muss die entsprechenden extern-Deklarationen und Aufrufe mit Diagnose
zurückweisen. Der vorliegende Test behauptet keine solche Moo-Diagnose.

## Spätere vollständige Gegenprobe

1. Dieselben Funktionen mit identischer Ziel-Toolchain als C-Bibliothek bauen.
2. Native Moo-Aufrufe mit gleichen Werten, Grenzwerten und gemischten
   Parametern erzeugen; exakte Linknamen und getrennte Argument-/Return-
   Ergebnisse prüfen. Auf Ziel-Triple, Calling Convention und Datenlayout
   vor IR-Erzeugung achten. LLVM-Verifikation und Cross-IR ergänzen.
3. Falschtyp, falsche Arity, variadische Signatur, Aggregatwert und bis zum
   Nachweis Bool als negative Compiletests prüfen. Dynamische Grenztests
   prüfen tatsächliche f64-Exaktheit des jeweiligen Integerwertes.
4. Linux x86_64 nativ und Windows x64 nativ auf einem hashgebundenen
   Snapshot mit benannter Toolchain testen. Ein Cross-Objekt ist kein
   Windows-Laufzeitnachweis. Weitere Targets erst nach eigener Abnahme.

Beispiel für lokale Referenz, falls kein eigener mise-Task existiert:

```sh
mkdir -p build/p2-abi
mise exec -- cc -std=c11 -O2 -Wall -Wextra -Werror -pedantic -c skripte/p2/abi/ffi_reference.c -o build/p2-abi/ffi_reference.o
mise exec -- cc -std=c11 -O2 -Wall -Wextra -Werror -pedantic skripte/p2/abi/ffi_reference_test.c build/p2-abi/ffi_reference.o -o build/p2-abi/ffi_reference_test
build/p2-abi/ffi_reference_test
```

## C++-Adapter und Entwurf der Moo-Anbindung

`cpp_adapter.h`, `cpp_adapter.cpp` und `cpp_adapter_test.c` bilden einen
kleinen C++17-Adapter und C-Caller. Lokal ausführen mit
`mise run p2-abi-cpp`. Der Test prüft C/C++-Interop, noch nicht Moo-Interop.

Die folgende Moo-Anbindung ist ein Entwurf, keine fertige Moo-Syntax:

Ein C-kompatibler Header deklariert einen opaken Handle und Funktionen mit
`extern "C"`: erstellen über Status plus Out-Handle, Operation über
geliehenen Handle, freigeben über denselben Adapter. Eine C++17-Datei hält
die tatsächliche Klasse privat. Jede exportierte Funktion fängt C++-
Exceptions ab und übersetzt sie in dokumentierte Fehlercodes; keine
Exception passiert die C-Grenze. Fehlgeschlagene Erstellung setzt den
Out-Handle definiert auf NULL.

Zeiger und Länge bilden einen Vertrag: Borrow oder Transfer, Nullable,
Mutierbarkeit, Zuständigkeit für Freigabe und Gültigkeitsdauer sind explizit.
Bei geliehenem Moo-Puffer hält ein Pin den Owner bis zum tatsächlichen Ende
der Fremdnutzung; solange keine Größenänderung oder Reallokation. Asynchrone
Nutzung benötigt explizite Abmeldung/Quieszenz vor Freigabe. Keine STL-Typen
oder C++-Objekte by value übergeben. Fehler bei Callbacks haben Status oder
Fallback plus separaten Fehlerkanal, bei void zwingend out-of-band.

Der endgültige Link muss C++-Objekt/Archiv UND passende C++-Runtime für die
jeweilige Toolchain einschließen. Windows- und Linux-Nachweise einschließlich
Lifecycle/Fehlerpfad und passendem Sanitizer stehen noch aus.
