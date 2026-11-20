# C++-Adaptergenerator (P2-T24)

`bindgen-cpp` erweitert das vorhandene Compilerwerkzeug über dieselbe
Clang-JSON-/jq-Infrastruktur. Der C-Unterbefehl `bindgen` bleibt erhalten.
Der neue Pfad erzeugt C++17-Adapter mit C-ABI und Moo-Griffwrapper.

```sh
moo-compiler bindgen-cpp probe.hpp --class Zaehler --prefix tg \
  --library p2_cpp_generated --target x86_64-unknown-linux-gnu --emit cpp > adapter.cpp
moo-compiler bindgen-cpp probe.hpp --class Zaehler --prefix tg \
  --library p2_cpp_generated --target x86_64-unknown-linux-gnu --emit moo > bindung.moos
```

Header und Adapter müssen mit denselben Includes/Defines gebaut werden.
`-I` und `-D` sind wiederholbar. Abhängigkeiten werden vor/nach Clang gehasht;
Ziel, Defines, Clang-Version und Dateihashes stehen in der Ausgabe.
Klassen/Methoden werden stabil sortiert. Unzulässige öffentliche Signaturen
liefern Exit 2 und eine Positionsdiagnose, ohne Teilausgabe.

## Teilmenge

Globale, nicht-template Klassen ohne Vererbung/virtuelle Methoden oder
Attribute; mindestens ein ausdrücklich deklarierter öffentlicher Konstruktor.
Private Implementierungsfelder bleiben in C++. Konstruktoren und gewöhnliche
öffentliche Methoden haben höchstens sechs skalare Argumente, konstante
Methoden sowie skalare/void-Rückgaben sind eingeschlossen.
Überladungsnamen enthalten Argumenttypen und bei Bedarf `_const`.
Kollisionen werden abgelehnt. Statische Methoden, Operatoren, Defaultargumente,
Makromethoden, Referenzqualifikatoren, Namespaces und verschachtelte Klassen
sind außerhalb der Teilmenge.

Skalare Typen: explizit signed/unsigned char, short, int, long, long long,
float und double. Plain char/bool, Referenzen, Zeigerargumente/-rückgaben,
Funktionswerte, Aggregatwerte und STL-Typen werden abgelehnt.
Linux x64 verwendet LP64, Windows-MSVC x64 LLP64 (long ist dort i32/u32).
Weitere Ziele werden vor Clang abgelehnt.

## Besitz und Fehler

Jede Klasse erhält einen eigenen `@opak`-Typ. Ein erfolgreicher Konstruktor
liefert einen besitzenden Griff, der ausdrücklich mit `*_frei` freigegeben
wird (`@verbraucht(1)`). Es gibt keinen Finalizer. Der vorhandene Griffschutz
prüft Typ, Gebrauch nach Freigabe und Doppelfreigabe auch bei Aliasen.
Fremde oder außerhalb des Vertrags freigegebene C++-Zeiger sind kein gültiger
Input. Der Aufrufer synchronisiert Zugriffe auf dasselbe Objekt.

Alle C++-Adapter fangen Exceptions: Konstruktorfehler ergeben null (`nichts`)
und `*_fehler()` ergibt -2. Methoden liefern i32-Status: 0 Erfolg, -1
Nullargument, -2 Exception. Skalare Ergebnisse laufen getrennt über einen
Ergebniszeiger; alle Skalarwerte einschließlich U64MAX bleiben darstellbar.
Der Moo-Wrapper verlangt `puffer[T]` mit Länge 1; sonst liefert er -3 vor dem
Fremdaufruf. Ergebnisse bleiben bei Fehler unverändert. Bereits ausgeführte
Objektmutationen werden nicht zurückgerollt. Auch ein werfender Destruktor
verbraucht den Griff. Der Fehlerstatus ist pro Thread und wird beim nächsten
C++-Adapteraufruf zurückgesetzt; -3 entsteht in Moo und verändert ihn nicht.

Der Aufrufer kann Statuswerte wie beim handgeschriebenen T23-Adapter
ausdrücklich in Moo-Fehler umsetzen. C++-Exceptions verlassen den Adapter nicht.

## Gezieltes Gate

```sh
MOO_COMPILER=/pfad/zum/moo-compiler mise -C skripte/p2/cpp_adapter run generator
```

Doppelte Generierung, zwei Klassen, Konstruktoren, Overloads, const/void,
C++/Moo-Roundtrip, voller U64MAX, Exception mit unverändertem Ergebnis,
Nullargumente, explizite Freigabe, Alias-/Doppelfrei-/Typprüfung sowie elf
negative Positionsdiagnosen werden geprüft. Unter Linux instrumentiert
ASan/UBSan/LSan das C++-Adapterorakel, nicht die gesamte Moo-Runtime.
Ein Windows-Beweis darf erst nach tatsächlicher Windows-Ausführung behauptet werden.
