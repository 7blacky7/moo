# moo lernen

**moo** ist eine universelle Programmiersprache, die jeder versteht.
Du schreibst auf Deutsch oder Englisch — moo übersetzt in Python, JavaScript und mehr.

---

## Inhaltsverzeichnis

1. [Installation](#installation)
2. [Erstes Programm](#erstes-programm)
3. [Variablen & Konstanten](#variablen--konstanten)
4. [Datentypen](#datentypen)
5. [Rechnen](#rechnen)
6. [Ausgabe](#ausgabe)
7. [Eingabe](#eingabe)
8. [Bedingungen](#bedingungen)
9. [Schleifen](#schleifen)
10. [Listen](#listen)
11. [List Comprehensions](#list-comprehensions)
12. [Map und Filter](#map-und-filter)
13. [String-Methoden](#string-methoden)
14. [Dictionaries](#dictionaries)
15. [Funktionen](#funktionen)
16. [Lambdas](#lambdas)
17. [Optional Chaining & Nullish Coalescing](#optional-chaining--nullish-coalescing)
18. [Standardbibliothek (Stdlib)](#standardbibliothek-stdlib)
19. [JSON](#json)
20. [HTTP](#http)
21. [Kryptografie & Sicherheit](#kryptografie--sicherheit)
22. [Datenbank](#datenbank)
23. [Multithreading](#multithreading)
24. [Klassen & Objekte](#klassen--objekte)
25. [Vererbung](#vererbung)
26. [Fehlerbehandlung](#fehlerbehandlung)
27. [Match / Switch](#match--switch)
28. [Module & Imports](#module--imports)
29. [Schlüsselwort-Tabelle](#schlüsselwort-tabelle)
30. [CLI-Befehle](#cli-befehle)
31. [Nativer Compiler](#nativer-compiler)

---

## Installation

moo hat zwei Wege, Programme auszuführen:

- **Nativer Compiler `moo-compiler`** (Hauptweg): bauen und ausführen wie in
  [KI_MIT_MOO.md, Kapitel 2 und 3](KI_MIT_MOO.md#2-bauen) beschrieben, dann
  `moo-compiler run hallo.moos`.
- **Transpiler `moo`** (nach Python/JavaScript, siehe [CLI-Befehle](#cli-befehle)):

```bash
uv sync
uv run moo run hallo.moos   # oder nach Aktivieren von .venv einfach: moo run hallo.moos
```

Die Beispiele unten funktionieren mit beiden Wegen.

## Erstes Programm

Erstelle eine Datei `hallo.moos`:

```
zeige "Hallo Welt!"
```

Ausführen (Transpiler via `uv`, oder nativ mit `moo-compiler run hallo.moos`):

```bash
uv run moo run hallo.moos
```

Ausgabe:
```
Hallo Welt!
```

Das gleiche auf Englisch:

```
show "Hello World!"
```

Beide Versionen machen exakt dasselbe.

---

## Variablen & Konstanten

### Variablen

```
# Deutsch
setze name auf "Anna"
setze alter auf 25

# English
set name to "Anna"
set age to 25
```

Variablen können jederzeit geändert werden:

```
setze x auf 10
setze x auf 20
```

### Konstanten

Konstanten können nicht geändert werden:

```
# Deutsch
konstante PI auf 3.14159

# English
const PI to 3.14159
```

### Kurzschreibweise

```
x += 5     # x = x + 5
x -= 3     # x = x - 3
```

---

## Datentypen

| Typ | Beispiel (DE) | Beispiel (EN) |
|-----|--------------|---------------|
| Zahl (Ganzzahl) | `42` | `42` |
| Zahl (Dezimal) | `3.14` | `3.14` |
| Text (String) | `"Hallo"` | `"Hello"` |
| Wahrheitswert | `wahr` / `falsch` | `true` / `false` |
| Nichts (null) | `nichts` | `none` |
| Liste | `[1, 2, 3]` | `[1, 2, 3]` |
| Dictionary | `{"a": 1}` | `{"a": 1}` |

---

## Rechnen

```
setze a auf 10 + 5      # 15  — Addition
setze b auf 10 - 3      # 7   — Subtraktion
setze c auf 4 * 3       # 12  — Multiplikation
setze d auf 10 / 3      # 3.3 — Division
setze e auf 10 % 3      # 1   — Rest (Modulo)
setze f auf 2 ** 10     # 1024 — Potenz
```

Klammern setzen die Reihenfolge:

```
setze x auf (2 + 3) * 4    # 20, nicht 14
```

---

## Ausgabe

```
# Deutsch
zeige "Hallo!"
zeige 42
zeige "Ergebnis: " + ergebnis

# English
show "Hello!"
show 42
```

---

## Eingabe

Mit `eingabe()` (oder `input()` auf Englisch) kannst du Text von der Tastatur lesen:

```
# Deutsch
setze name auf eingabe("Wie heißt du? ")
zeige "Hallo, " + name + "!"

# English
set name to input("What's your name? ")
show "Hello, " + name + "!"
```

Die Funktion zeigt den Prompt-Text an und wartet, bis der Benutzer etwas eingibt und Enter drückt. Der eingegebene Wert wird immer als String zurückgegeben.

```
setze alter auf eingabe("Wie alt bist du? ")
setze alter auf alter + 0    # in Zahl umwandeln
wenn alter >= 18:
    zeige "Du bist volljährig!"
```

---

## Bedingungen

### Einfache Bedingung

```
wenn alter >= 18:
    zeige "Erwachsen"
```

### Mit Alternative

```
wenn punkte >= 50:
    zeige "Bestanden!"
sonst:
    zeige "Durchgefallen."
```

### Mehrere Bedingungen

```
wenn note == 1:
    zeige "Sehr gut!"
sonst wenn note == 2:
    zeige "Gut!"
sonst wenn note <= 4:
    zeige "Bestanden"
sonst:
    zeige "Nicht bestanden"
```

### Vergleichsoperatoren

| Operator | Bedeutung |
|----------|-----------|
| `==` | ist gleich |
| `!=` | ist nicht gleich |
| `<` | kleiner als |
| `>` | größer als |
| `<=` | kleiner oder gleich |
| `>=` | größer oder gleich |

### Logische Operatoren

```
# Deutsch
wenn alter >= 18 und name != "":
    zeige "OK"

wenn a oder b:
    zeige "Eins von beiden"

wenn nicht fertig:
    zeige "Noch nicht fertig"

# English
if age >= 18 and name != "":
    show "OK"
```

---

## Schleifen

### Solange-Schleife (while)

```
setze i auf 0
solange i < 10:
    zeige i
    i += 1
```

### Für-Schleife (for)

```
für zahl in [1, 2, 3, 4, 5]:
    zeige zahl
```

### Stopp und Weiter (break / continue)

```
setze i auf 0
solange i < 10:
    wenn i == 5:
        stopp                # Schleife beenden

    wenn i == 3:
        i += 1
        weiter               # Rest überspringen

    zeige i
    i += 1
```

---

## Listen

```
# Erstellen
setze tiere auf ["Hund", "Katze", "Maus"]

# Zugriff (zählt ab 0)
zeige tiere[0]         # "Hund"
zeige tiere[2]         # "Maus"

# Ändern
tiere[1] = "Hamster"

# Durchlaufen
für tier in tiere:
    zeige tier

# Methoden
tiere.append("Vogel")
zeige tiere.length
```

---

## List Comprehensions

Listen lassen sich elegant mit Comprehensions erzeugen:

```
# Deutsch
setze quadrate auf [x * x für x in 0..10]
zeige quadrate    # [0, 1, 4, 9, 16, 25, 36, 49, 64, 81]

# Mit Bedingung
setze gerade auf [x für x in 0..20 wenn x % 2 == 0]
zeige gerade      # [0, 2, 4, 6, 8, 10, 12, 14, 16, 18]

# English
set squares to [x * x for x in 0..10]
set evens to [x for x in 0..20 if x % 2 == 0]
```

---

## Map und Filter

Listen können mit `map` und `filter` transformiert werden:

```
setze zahlen auf [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]

# Jedes Element verdoppeln
setze doppelt auf zahlen.map((x) => x * 2)
zeige doppelt     # [2, 4, 6, 8, 10, 12, 14, 16, 18, 20]

# Nur Werte größer als 5 behalten
setze gross auf zahlen.filter((x) => x > 5)
zeige gross       # [6, 7, 8, 9, 10]

# Kombiniert: verdoppeln, dann filtern
setze ergebnis auf zahlen.map((x) => x * 2).filter((x) => x > 10)
zeige ergebnis    # [12, 14, 16, 18, 20]
```

---

## String-Methoden

Strings haben eingebaute Methoden zur Textverarbeitung. Jede Methode gibt es auf Deutsch und Englisch:

### Groß-/Kleinschreibung

```
setze text auf "hallo welt"

zeige text.gross()          # "HALLO WELT"
zeige text.upper()          # "HALLO WELT"

setze laut auf "LEISE BITTE"
zeige laut.klein()          # "leise bitte"
zeige laut.lower()          # "leise bitte"
```

### Leerzeichen entfernen

```
setze eingabe auf "  Hallo  "
zeige eingabe.trimmen()     # "Hallo"
zeige eingabe.trim()        # "Hallo"
```

### Text aufteilen

```
setze satz auf "Eins,Zwei,Drei"
setze teile auf satz.teilen(",")    # ["Eins", "Zwei", "Drei"]
setze teile auf satz.split(",")     # ["Eins", "Zwei", "Drei"]

# Standardmäßig wird an Leerzeichen getrennt
setze wörter auf "Hallo schöne Welt".teilen()   # ["Hallo", "schöne", "Welt"]
```

### Text ersetzen

```
setze text auf "Hallo Welt"
zeige text.ersetzen("Welt", "moo")    # "Hallo moo"
zeige text.replace("Welt", "moo")     # "Hallo moo"
```

---

## Dictionaries

Dictionaries speichern Schlüssel-Wert-Paare:

```
# Erstellen
setze person auf {"name": "Max", "alter": 30, "stadt": "Berlin"}

# Zugriff
zeige person["name"]       # "Max"

# Ändern
person["alter"] = 31

# Leeres Dictionary
setze daten auf {}
```

---

## Funktionen

### Definition

```
# Deutsch
funktion addiere(a, b):
    gib_zurück a + b

# English
func add(a, b):
    return a + b
```

### Aufruf

```
setze summe auf addiere(3, 7)
zeige summe                        # 10
```

### Standard-Werte (Defaults)

```
funktion begrüße(name, gruß = "Hallo"):
    gib_zurück gruß + ", " + name + "!"

zeige begrüße("Anna")                  # "Hallo, Anna!"
zeige begrüße("Anna", "Servus")        # "Servus, Anna!"
```

### Funktionen ohne Rückgabewert

```
funktion sage_hallo(name):
    zeige "Hallo " + name

sage_hallo("Welt")
```

---

## Lambdas

Kurze Einmal-Funktionen (auch anonyme Funktionen genannt):

```
setze verdopple auf (x) => x * 2
zeige verdopple(21)      # 42

setze addiere auf (a, b) => a + b
zeige addiere(3, 4)      # 7
```

Lambdas eignen sich besonders als Argumente für andere Funktionen:

```
setze f auf (x) => x * 2
setze zahlen auf [1, 2, 3, 4, 5]
```

---

## Optional Chaining & Nullish Coalescing

### Optional Chaining

Mit `?.` greifst du sicher auf Eigenschaften zu. Wenn das Objekt `nichts` ist, gibt es `nichts` zurueck statt einen Fehler:

```
# Deutsch
setze person auf {"adresse": {"stadt": "Berlin"}}
zeige person?.adresse?.stadt       # "Berlin"
zeige person?.telefon?.nummer      # nichts (kein Crash!)

# English
set person to {"address": {"city": "Berlin"}}
show person?.address?.city         # "Berlin"
show person?.phone?.number         # none (no crash!)
```

### Nullish Coalescing

Mit `??` kannst du einen Standardwert angeben, falls ein Wert `nichts` ist:

```
# Deutsch
setze name auf eingabe ?? "Unbekannt"
zeige name    # Wert von eingabe, oder "Unbekannt" wenn nichts

# English
set name to input ?? "Unknown"

# Kombiniert mit Optional Chaining
setze stadt auf person?.adresse?.stadt ?? "Nicht angegeben"
```

---

## Standardbibliothek (Stdlib)

moo bringt nützliche Funktionen mit, die direkt verfügbar sind — ohne Import. Jede Funktion gibt es auf Deutsch und Englisch.

### Mathematik

```
zeige abs(-42)              # 42 — Betrag

zeige wurzel(16)            # 4.0
zeige sqrt(16)              # 4.0

zeige runde(3.7)            # 4
zeige round(3.14)           # 3

zeige boden(3.9)            # 3 — abrunden
zeige floor(3.9)            # 3

zeige decke(3.1)            # 4 — aufrunden
zeige ceil(3.1)             # 4

zeige min(3, 7)             # 3
zeige max(3, 7)             # 7
```

### Zufall

```
setze zahl auf zufall()         # Zufallszahl zwischen 0 und 1
setze zahl auf random()         # dasselbe auf Englisch
```

### Typ prüfen

```
zeige typ_von(42)               # "Zahl"
zeige type_of(42)               # "Zahl"
zeige typ_von("Hallo")          # "Text"
zeige typ_von([1, 2, 3])        # "Liste"
zeige typ_von(wahr)             # "Wahrheitswert"
```

---

## JSON

JSON-Daten lesen und schreiben:

```
# Deutsch
setze daten auf json_lesen('{"name": "Anna", "alter": 25}')
zeige daten["name"]         # "Anna"

setze text auf json_text(daten)
zeige text                  # '{"name": "Anna", "alter": 25}'

# English
set data to json_parse('{"name": "Anna", "age": 25}')
show data["name"]           # "Anna"

set text to json_string(data)
show text                   # '{"name": "Anna", "age": 25}'
```

---

## HTTP

HTTP-Anfragen senden:

```
# Deutsch
setze antwort auf http_hole("https://api.example.com/daten")
zeige antwort

setze ergebnis auf http_sende("https://api.example.com/senden", {"name": "Anna"})
zeige ergebnis

# English
set response to http_get("https://api.example.com/data")
show response

set result to http_post("https://api.example.com/submit", {"name": "Anna"})
show result
```

---

## Kryptografie & Sicherheit

### Hashing und Zufall

```
# SHA-256 Hash erzeugen
setze hash auf sha256("geheimer Text")
zeige hash

# Sicheren Zufall erzeugen (16 Bytes)
setze token auf sichere_zufall(16)
setze token auf secure_random(16)    # English
```

### Kodierung

```
setze kodiert auf base64_encode("Hallo Welt")
zeige kodiert                        # "SGFsbG8gV2VsdA=="

setze original auf base64_decode(kodiert)
zeige original                       # "Hallo Welt"
```

### Bereinigung (Sanitizing)

```
# HTML-Tags entfernen / entschaerfen
setze sicher auf html_bereinigen("<script>alert('hack')</script>")
zeige sicher    # "&lt;script&gt;alert('hack')&lt;/script&gt;"

# SQL-Injection verhindern
setze sicher auf sql_bereinigen("'; DROP TABLE users; --")
```

---

## Datenbank

SQLite- und andere Datenbanken verwenden:

```
# Deutsch
setze db auf db_verbinde("sqlite:///meine_daten.db")

# Tabelle erstellen
db_ausführen(db, "CREATE TABLE IF NOT EXISTS nutzer (id INTEGER PRIMARY KEY, name TEXT)")

# Daten einfuegen
db_ausführen(db, "INSERT INTO nutzer (name) VALUES ('Anna')")

# Daten abfragen
setze ergebnis auf db_abfrage(db, "SELECT * FROM nutzer")
für zeile in ergebnis:
    zeige zeile

# Verbindung schliessen
db_schliessen(db)

# English
set db to db_connect("sqlite:///my_data.db")
db_execute(db, "INSERT INTO users (name) VALUES ('Anna')")
set result to db_query(db, "SELECT * FROM users")
db_close(db)
```

---

## Multithreading

### Channels

Channels ermoeglichen die sichere Kommunikation zwischen Threads:

```
# Deutsch
setze k auf kanal(16)       # Channel mit Puffer fuer 16 Nachrichten

# In einem Thread senden
k.senden("Hallo vom Thread!")

# In einem anderen Thread empfangen
setze nachricht auf k.empfangen()
zeige nachricht              # "Hallo vom Thread!"

# English
set ch to channel(16)
ch.send("Hello from thread!")
set message to ch.receive()
show message
```

### Threads starten und warten

Regel: **Ein veränderlicher Wert gehört genau einem Thread.** Teilen friert ihn
tief ein. Wer geteilt verändern will, braucht eine ausdrückliche Zelle (Atom oder
Sperre). Stille Sperren gibt es nicht.

`starte(funktion, a, b, ...)` / `spawn(...)` startet einen Thread und ruft die
Funktion mit allen Argumenten danach auf (keins bis acht; jedes Argument wird
nach derselben Regel geteilt, ein veränderlicher Wert ist im Thread also
eingefroren). `t.warten()` / `t.wait()` liefert das Ergebnis. Ein Kanal als
geteilte Konstante verbindet die Threads:

```
geteilt konstante K auf kanal(4)

funktion arbeite(n):
    K.senden(n * n)
    gib_zurück n + 1

setze t auf starte(arbeite, 7)
zeige K.empfangen()     # 49
zeige t.warten()        # 8
```

Das Ergebnis gehört dem **ersten Wartenden**. Wartet ein anderer Thread
danach auf denselben Thread, wirft `warten` einen fangbaren Fehler („das Ergebnis
gehört dem ersten Wartenden“); das Ergebnis dann per Kanal weitergeben. Der
Thread, der zuerst gewartet hat, darf erneut warten und bekommt jedes Mal
dasselbe Ergebnis:

```
funktion rechne(n):
    gib_zurück n * 2

funktion zweiter(t):
    versuche:
        t.warten()
        gib_zurück 1
    fange e:
        gib_zurück 0

setze t auf starte(rechne, 21)
zeige t.warten()          # 42
setze u auf starte(zweiter, t)
zeige u.warten()          # 0 (fremder Wartender, Fehler gefangen)
zeige t.warten()          # 42 (dasselbe Ergebnis)
```

Ein unbehandelter Fehler in einem Worker beendet den Prozess nicht sofort, wird aber
gemeldet; das Programm endet am Schluss mit Exit-Code 1.

### Globale gehören dem Hauptthread

Code, der von `starte` aus erreichbar ist, darf keine veränderliche Globale lesen
oder schreiben. Wo der Compiler das beweisen kann, ist es der Fehler `M-P001`:

```
setze stand auf 0

funktion arbeite(n):
    setze stand auf stand + n     # Fehler M-P001
    gib_zurück stand

setze t auf starte(arbeite, 1)
t.warten()
```

Erlaubt ohne Markierung sind nur nie neu gesetzte `konstante`n mit Zahl, Text,
Wahrheitswert oder `nichts`. Alles andere markierst du mit `geteilt` (nur auf oberster
Ebene; englisch `shared`, z. B. `shared konstante GRENZE auf 3`).
Der Wert wird bei der Initialisierung tief eingefroren und kann nicht mehr neu
gesetzt werden (`M-P003`):

```
geteilt konstante GRENZE auf 3
geteilt konstante NAMEN auf ["anna", "ben"]

funktion pruefe(x):
    gib_zurück x < GRENZE und länge(NAMEN) == 2

setze t auf starte(pruefe, 2)
zeige t.warten()        # wahr
```

Methoden werden anhand der nachgewiesenen lokalen Klasse aufgelöst, auch über
Aliase und Vererbung. Eine Typangabe allein gilt dabei nicht als Beweis: Entscheidend
sind die tatsächlichen Zuweisungen. Bei unbekanntem oder wechselndem Empfängertyp
bleibt die Prüfung vorsichtig. Lässt sich der Zugriff nicht beweisen, prüft das
Programm zur Laufzeit und bricht mit Meldung ab; `--typ-streng` lehnt es stattdessen
mit `M-P002` ab. Funktionsadressen, die `@nativ`-Code als C-Rückruf weitergibt,
werden ebenfalls als mögliche Aufrufe aus fremden Threads geprüft.

### Atom und Sperre: geteilt verändern

**Atom** (`atom` / `atomic`): eine Zahl, die Threads ohne Sperre ändern.
Funktionen: `atom_laden`/`atomic_load`, `atom_speichern`/`atomic_store`,
`atom_addieren`/`atomic_add` (liefert den alten Wert), `atom_tauschen`/`atomic_swap`,
`atom_vergleichen_und_tauschen`/`atomic_compare_exchange` (wahr/falsch).

```
geteilt konstante Z auf atom(0)

funktion zaehle(n):
    setze i auf 0
    solange i < 1000:
        atom_addieren(Z, 1)
        setze i auf i + 1
    gib_zurück n

setze t1 auf starte(zaehle, 1)
setze t2 auf starte(zaehle, 2)
t1.warten()
t2.warten()
zeige atom_laden(Z)                              # 2000
zeige atom_vergleichen_und_tauschen(Z, 2000, 7)  # wahr
zeige atomic_load(Z)                             # 7
```

**Sperre** (`sperre` / `lock`) schützt einen veränderlichen Inhalt. Zugriff nur im
Block `mit S als k:` / `with S als k:`; die Sperre wird am Blockende, bei `wirf`
und bei Fehlern in aufgerufenen Funktionen wieder freigegeben. `gib_zurück`,
`stopp`/`abbrechen` und `weiter` aus dem Block heraus sind ein Compilefehler. Fangbare Fehler:
verschachteltes `mit` auf dieselbe Sperre, Inhalt nach dem Block noch referenziert.

Unterelemente, die außerhalb der Sperre noch referenziert sind, werden tief und
dauerhaft eingefroren, bevor der Block die Sperre freigibt; das gilt auch bei
Fehlern. Bereits beim Erstellen der Sperre vorhandene Aliase auf Unterelemente
werden ebenso geschützt. Rein interne Mehrfachreferenzen bleiben veränderlich.
Bei Puffern schützt das Einfrieren auch andere Ansichten auf denselben Speicher.
Lesender nativer Zugriff bleibt mit `@liest(...)` möglich; schreibender Zugriff
wirft einen fangbaren Fehler. Zum Ändern eines eingefrorenen Werts muss ein neuer
Wert eingesetzt werden. Diese Prüfung durchsucht den erreichbaren Inhalt beim
Erstellen der Sperre und beim Verlassen jedes `mit`-Blocks.

```
geteilt konstante KONTO auf sperre({"stand": 0})

funktion buche(n):
    setze i auf 0
    solange i < 1000:
        mit KONTO als k:
            k["stand"] = k["stand"] + n
        setze i auf i + 1
    gib_zurück 0

setze t1 auf starte(buche, 1)
setze t2 auf starte(buche, 2)
t1.warten()
t2.warten()
mit KONTO als k:
    zeige k["stand"]        # 3000
```

### Besitz übergeben: übergib

Ein Puffer kann ohne Kopie und ohne Einfrieren den Besitzer wechseln:
`kanal.senden(übergib p)` (auch `uebergib` / `transfer`). Danach ist `p` beim Sender
`nichts`. Der Empfänger darf ihn ändern. Hat der Puffer weitere Referenzen oder Sichten,
ist das ein fangbarer Fehler und der Puffer bleibt beim Sender. `übergib` gilt nur für
Puffer und nur als Argument von `senden`.

```
geteilt konstante HIN auf kanal(2)
geteilt konstante ZURUECK auf kanal(2)

funktion verdoppeln(x):
    setze p auf HIN.empfangen()
    setze i auf 0
    solange i < länge(p):
        p[i] = p[i] * 2
        setze i auf i + 1
    ZURUECK.senden(übergib p)
    gib_zurück 0

setze p auf puffer_erstellen(i32, 4)
p[0] = 21
setze t auf starte(verdoppeln, 0)
HIN.senden(übergib p)
zeige p                  # nichts
t.warten()
setze q auf ZURUECK.empfangen()
zeige q[0]               # 42
```

Englisch: `spawn`, `wait`, `atomic(...)`, `lock(...)`, `with S as k:`, `transfer`.

---

## Klassen & Objekte

### Klasse definieren

```
# Deutsch
klasse Auto:
    funktion erstelle(marke, farbe):
        selbst.marke = marke
        selbst.farbe = farbe

    funktion beschreibung():
        gib_zurück selbst.farbe + " " + selbst.marke

# English
class Car:
    func create(brand, color):
        this.brand = brand
        this.color = color

    func describe():
        return this.color + " " + this.brand
```

### Objekte erstellen

```
setze meinAuto auf neu Auto("BMW", "rot")
zeige meinAuto.beschreibung()     # "rot BMW"
zeige meinAuto.marke              # "BMW"
```

### Eigenschaften ändern

```
meinAuto.farbe = "blau"
zeige meinAuto.beschreibung()     # "blau BMW"
```

---

## Vererbung

Eine Klasse kann von einer anderen erben:

```
klasse Tier:
    funktion erstelle(name):
        selbst.name = name

    funktion vorstellen():
        gib_zurück "Ich bin " + selbst.name

klasse Hund(Tier):
    funktion erstelle(name):
        selbst.name = name

    funktion bellen():
        gib_zurück selbst.name + " sagt: Wuff!"

setze rex auf neu Hund("Rex")
zeige rex.vorstellen()     # "Ich bin Rex"  (von Tier geerbt)
zeige rex.bellen()         # "Rex sagt: Wuff!"
```

---

## Fehlerbehandlung

```
# Deutsch
versuche:
    setze x auf 10 / 0
fange fehler:
    zeige "Ein Fehler ist passiert: " + fehler

# English
try:
    set x to 10 / 0
catch error:
    show "An error occurred: " + error
```

### Fehler werfen

```
funktion teile(a, b):
    wenn b == 0:
        wirf "Division durch Null!"
    gib_zurück a / b

versuche:
    zeige teile(10, 0)
fange fehler:
    zeige fehler
```

### Unbehandelte Fehler

Wird ein Fehler nirgends mit `versuche:` gefangen, beendet moo das Programm sofort
mit Rückgabewert (Exit-Code) **1** und einer Meldung auf der Fehlerausgabe (stderr).
Ein Fehler wird nie still verschluckt; Skripte und CI erkennen ihn am Exit-Code.

```
funktion lade():
    wirf "Datei fehlt"

funktion start():
    lade()
    zeige "weiter geht's"

start()
```

Ausgabe (auf stderr, Exit-Code 1; „weiter geht's“ erscheint nicht):

```
Unbehandelter Fehler: Datei fehlt
  geworfen in lade (programm.moos:2)
  nicht behandelt: kein versuche: im Aufrufpfad – Abbruch mit Exit-Code 1
```

Dasselbe gilt für Laufzeitfehler, z. B. eine Listenmethode auf einem Text
(`"abc".sortieren()`) oder eine Textmethode auf einer Liste (`[1].gross()`).
`teste_alle()` endet mit Exit-Code 1, wenn mindestens ein Test FAIL meldet.

Gibt es weiter außen ein `versuche:`, fängt es den Fehler wie gewohnt. Eine
Funktion dazwischen, die selbst kein `versuche:` hat, läuft nach dem Fehler aber
weiter. moo meldet die Stelle einmal:

```
Hinweis: unbehandelter Fehler „Datei fehlt“
  geworfen in lade (programm.moos:2)
  nicht behandelt in start beim Aufruf von lade (programm.moos:5) – start läuft weiter
```

- Jeder Fehler wird nur einmal gemeldet, auch wenn er durch mehrere Funktionen läuft.
- Tritt derselbe Fehler dabei in einer Schleife immer wieder auf, erscheint der Hinweis
  einmal. Am Programmende steht dann, wie oft es insgesamt passiert ist.
- `nichts` als Rückgabe (z. B. „nichts gefunden“) ist kein Fehler und erzeugt keinen Hinweis.

**Streng-Modus:** Mit `MOO_STRENG=1` bricht das Programm zusätzlich schon an der
Zwischenstelle ab (die Funktion ohne eigenes `versuche:`), auch wenn weiter außen
ein `versuche:` den Fehler fangen würde. Unbehandelte Fehler beenden das Programm
dagegen immer mit Exit-Code 1, mit und ohne `MOO_STRENG`.

```bash
MOO_STRENG=1 ./programm
```

---

### Compilerdiagnosen

Der Compiler meldet jeden Fehler einheitlich mit Code, Ort und Korrekturhilfe und
sammelt alle Syntaxfehler eines Laufs:

```
programm.moos:2:15: Fehler M-S001: Doppelpunkt ':' erwartet, aber Zeilenende gefunden. ...
    2 |     wenn x > 1
      |               ^
```

`moo-compiler pruefe programm.moos` prüft nur (ohne Binary), `--diagnose=json` liefert
dieselben Diagnosen maschinenlesbar, `moo-compiler erklaere M-S001` erklärt einen Code.
Hinweise (`Hinweis M-H…`) brechen nichts ab. Zu den Sprachmitteln für Threads
gehören `M-P001` bis `M-P003` (Globale und `geteilt`, siehe „Multithreading“). Katalog und JSON-Format: `docs/AI_WITH_MOO.md`, Abschnitt 9.

## Match / Switch

Prüfe einen Wert gegen mehrere Möglichkeiten:

```
# Deutsch
setze farbe auf "rot"
prüfe farbe:
    fall "rot":
        zeige "Stopp!"
    fall "gelb":
        zeige "Achtung!"
    fall "grün":
        zeige "Los!"
    standard:
        zeige "Unbekannte Farbe"

# English
set color to "red"
match color:
    case "red":
        show "Stop!"
    case "yellow":
        show "Caution!"
    case "green":
        show "Go!"
    default:
        show "Unknown color"
```

---

## Module & Imports

```
# Ganzes Modul importieren
# Deutsch
importiere mathe

# Mit Alias
importiere mathe als m

# Einzelne Funktionen
aus mathe importiere wurzel, runden

# English
import math
import math as m
from math import sqrt, round
```

Verfügbare Module unter `stdlib/`:
- Basis: `mathe`, `liste`, `text`, `mini`, `primzahl`
- Netzwerk: `dns` (RFC-1035-DNS-Client), `websocket` (RFC-6455-Client), `netz_frame` (TCP-Framing mit CRC32)
- UI: `ui`, `ui_moo` (mit `ui_aktionen`, `ui_baue`, `ui_bind`, `ui_komponenten`, `ui_layout`, `ui_leinwand`, `ui_liste`, `ui_introspect`, `ui_test`)
- KI & 3D: `ki_bild_easy` (`BildModell`), `welt` / `world`

---

## Schlüsselwort-Tabelle

Die komplette Referenz aller Schlüsselwörter:

| Deutsch | English | Bedeutung |
|---------|---------|-----------|
| `setze` | `set` | Variable erstellen/ändern |
| `auf` | `to` | Zuweisungsoperator |
| `konstante` | `const` | Unveränderbare Variable |
| `zeige` | `show` | Ausgabe auf dem Bildschirm |
| `wenn` | `if` | Bedingung |
| `sonst` | `else` | Alternative |
| `solange` | `while` | Solange-Schleife |
| `für` | `for` | Für-jedes-Schleife |
| `in` | `in` | In (für Schleifen) |
| `funktion` | `func` | Funktion definieren |
| `gib_zurück` | `return` | Wert zurückgeben |
| `stopp` | `break` | Schleife beenden |
| `weiter` | `continue` | Nächster Durchlauf |
| `und` | `and` | Logisches UND |
| `oder` | `or` | Logisches ODER |
| `nicht` | `not` | Logisches NICHT |
| `wahr` | `true` | Wahrheitswert: ja |
| `falsch` | `false` | Wahrheitswert: nein |
| `nichts` | `none` | Kein Wert (null) |
| `klasse` | `class` | Klasse definieren |
| `neu` | `new` | Neues Objekt erstellen |
| `selbst` | `this` | Aktuelles Objekt |
| `versuche` | `try` | Fehler abfangen (Start) |
| `fange` | `catch` | Fehler abfangen (Handler) |
| `wirf` | `throw` | Fehler werfen |
| `prüfe` | `match` | Wert prüfen (Switch) |
| `fall` | `case` | Ein Fall im Match |
| `standard` | `default` | Standard-Fall |
| `importiere` | `import` | Modul laden |
| `aus` | `from` | Import aus Modul |
| `exportiere` | `export` | Funktion/Klasse exportieren |
| `als` | `as` | Alias beim Import |
| `eingabe` | `input` | Benutzereingabe lesen |
| `für ... wenn` | `for ... if` | List Comprehension mit Bedingung |
| `?.` | `?.` | Optional Chaining |
| `??` | `??` | Nullish Coalescing (Standardwert) |
| `json_lesen` | `json_parse` | JSON-Text in Objekt umwandeln |
| `json_text` | `json_string` | Objekt in JSON-Text umwandeln |
| `http_hole` | `http_get` | HTTP-GET-Anfrage senden |
| `http_sende` | `http_post` | HTTP-POST-Anfrage senden |
| `sha256` | `sha256` | SHA-256 Hash berechnen |
| `sichere_zufall` | `secure_random` | Kryptografisch sicheren Zufall erzeugen |
| `base64_encode` | `base64_encode` | Base64-Kodierung |
| `base64_decode` | `base64_decode` | Base64-Dekodierung |
| `html_bereinigen` | `html_bereinigen` | HTML-Tags entschaerfen |
| `sql_bereinigen` | `sql_bereinigen` | SQL-Injection verhindern |
| `db_verbinde` | `db_connect` | Datenbankverbindung oeffnen |
| `db_abfrage` | `db_query` | Datenbank abfragen (SELECT) |
| `db_ausführen` | `db_execute` | Datenbank-Befehl ausfuehren |
| `db_schliessen` | `db_close` | Datenbankverbindung schliessen |
| `kanal` | `channel` | Channel fuer Multithreading |
| `senden` | `send` | Nachricht in Channel senden |
| `empfangen` | `receive` | Nachricht aus Channel empfangen |

---

## CLI-Befehle

```bash
# Nativer Compiler — direkt ausführen (Standardweg)
moo-compiler run datei.moos

# Nativer Compiler — native Binary erzeugen
moo-compiler compile datei.moos

# Weitere Subcommands von moo-compiler:
#   repl                  Interaktive REPL
#   targets               Verfügbare Cross-Targets & Boards anzeigen
#   paket / package       Paketverwaltung (install/list/remove)
#   ast / layout          AST- bzw. Layout-Diagnose
#   bindgen               C-Header-Bindungsentwurf erzeugen
# Wichtige compile-Optionen: -o/--output, --emit-ir, --target, --lib-pfad,
#   --profile, --no-stdlib, --linker-script, --entry, --erlaube-kern,
#   --kernel, --emit elf|flat|sector, --board

# Transpiler (über Python-Toolchain / uv) — Programm ausführen
uv run moo run datei.moos

# Nach Python übersetzen
uv run moo build datei.moos -t python

# Nach JavaScript übersetzen
uv run moo build datei.moos -t javascript

# In Datei speichern
uv run moo build datei.moos -t python -o ausgabe.py
uv run moo build datei.moos -t javascript -o ausgabe.js
```

---

## Nativer Compiler

Neben dem Transpiler (der moo-Code nach Python oder JavaScript übersetzt) gibt es einen **nativen Compiler**. Dieser erzeugt direkt ausführbare Binärdateien — ohne Python oder Node.js.

### Unterschied: Transpiler vs. Nativer Compiler

| | Transpiler (`moo`) | Nativer Compiler (`moo-compiler`) |
|---|---|---|
| **Ausgabe** | Python-/JS-Quellcode | Native Binary |
| **Laufzeit nötig** | Ja (Python/Node) | Nein |
| **Geschwindigkeit** | Interpretiert | Nativ kompiliert |
| **Anwendung** | Entwicklung, Prototyping | Produktion, Auslieferung |

### Direkt ausführen

```bash
moo-compiler run hallo.moos
```

Kompiliert und führt das Programm in einem Schritt aus. Praktisch für die Entwicklung.

### Binary erzeugen

```bash
moo-compiler compile hallo.moos
```

Erzeugt eine eigenständige Binary-Datei, die ohne moo ausgeführt werden kann:

```bash
./hallo
```

### Alle nativen Features

Der native Compiler unterstützt alle in diesem Dokument beschriebenen Features:
Variablen, Bedingungen, Schleifen, Funktionen, Lambdas, Klassen, Vererbung,
String-Methoden, Eingabe, Stdlib-Funktionen, Fehlerbehandlung und Match/Switch.

### Native Zahlen (`@nativ`)

Normale moo-Zahlen sind dynamisch (64-Bit-Gleitkomma). Für Engine-, Grafik-
und Systemcode gibt es zusätzlich **native Funktionen** mit echten
Maschinentypen. Sie werden mit `@nativ` markiert und brauchen für jeden
Parameter und die Rückgabe einen Typ:

```moo
@nativ
funktion mittel(a: i32, b: i32) -> i32:
    gib_zurück (a + b) / 2

zeige mittel(7, 4)        # 5  (Ganzzahldivision rundet zur Null)
```

Typen: `i8 i16 i32 i64` (mit Vorzeichen), `u8 u16 u32 u64` (ohne),
`f32 f64` (Gleitkomma), `bool`. Ein Zahlenliteral nimmt den Typ aus dem
Kontext; ohne Kontext ist eine Ganzzahl `i64`, eine Kommazahl `f64`.

**Ganzzahlen** rechnen in der festen Breite: `+ - *` und das Vorzeichen
wrappen modulo 2^N (`i8`: 127 + 1 = -128). Division rundet zur Null, der
Rest hat das Vorzeichen des Dividenden (`-7 / 3 = -2`, `-7 % 3 = -1`).
`<<` und `>>` arbeiten mit der Breite des linken Operanden; `>>` ist bei
Vorzeichentypen arithmetisch, sonst logisch.

**Fehlerfälle** sind definiert, nie undefiniertes Verhalten: Division oder
Rest durch 0, `MIN / -1`, `MIN % -1` und Schiebeweiten außerhalb `0..N-1`.
Ist der Fall schon beim Kompilieren erkennbar (`x / 0`, `x << 8` bei `u8`),
ist es ein Compilefehler. Zur Laufzeit ist es ein normaler moo-Fehler, den
`versuche:` fängt; ohne Handler endet das Programm mit Meldung und
Rückgabewert 1:

```moo
@nativ
funktion teile(a: i32, b: i32) -> i32:
    gib_zurück a / b

versuche:
    zeige teile(7, 0)
fange fehler:
    zeige "Division durch Null gefangen"
```

Wer ohne Fehler weiterrechnen will (etwa im Kernel, wo ein Laufzeitfehler die
CPU anhält), prüft vorher: `teilbar(a, b)` ist `wahr`, wenn `a / b` und
`a % b` definiert sind; `verschiebbar(a, n)` prüft die Schiebeweite. Englisch:
`divisible`, `shiftable`.

```moo
@nativ
funktion teile_oder(a: i32, b: i32, ersatz: i32) -> i32:
    unsicher:
        wenn teilbar(a, b):
            gib_zurück a / b
        gib_zurück ersatz
```

Ein `unsicher:`-Körper mit Rückgabetyp muss auf jedem Weg ein `gib_zurück`
erreichen. Fehlt `gib_zurück` ganz, lehnt der Compiler die Funktion ab. Läuft
ein Körper, der nur auf manchen Wegen zurückkehrt, bis ans Ende, ist das
derselbe fangbare Laufzeitfehler (Meldung „endet ohne Rückgabewert“); im
Kernel-Profil und in C-Exporten hält wie bei jedem nativen Fehler die CPU an.

Ein `unsicher:`-Körper darf auch in `versuche:` aufgerufen werden, in `main`
wie in einer benannten Funktion: Sein Fehler landet im `fange`-Zweig wie bei
jeder anderen nativen Funktion, auch in Funktionen mit `aufräumen:` (die
Aufräumaktionen laufen genau einmal, der erste Fehler bleibt erhalten). Der
Aufruf selbst steht weiterhin in `unsicher:`, weil der Körper C-artig rechnet
(Zeiger, Überlauf wickelt).

```moo
@nativ
funktion halbiere(a: i32, b: i32) -> i32:
    unsicher:
        gib_zurück a / b

unsicher:
    versuche:
        zeige halbiere(8, 0)
    fange fehler:
        zeige "gefangen"        # das Programm läuft weiter
```

**Gleitkomma** (`f32`, `f64`) folgt IEEE: `1.0 / 0.0` ist unendlich,
`0.0 / 0.0` ist NaN, `NaN == NaN` ist `falsch`. Es werden keine
Fast-Math-Umordnungen vorgenommen.

**Konversionen** sind immer ausdrücklich. `als(Typ, wert)` ist der geprüfte
Standardcast: Ganzzahlen außerhalb des Zielbereichs, Kommazahlen mit
Bruchteil, NaN und Unendlich sind Fehler (fangbar wie oben). Wer anderes
Verhalten will, sagt es beim Namen:

```moo
@nativ
funktion byte(a: i32) -> u8:
    gib_zurück als(u8, a)             # 300 -> Fehler, 200 -> 200

@nativ
funktion byte_wrap(a: i32) -> u8:
    gib_zurück als_wrap(u8, a)        # -1 -> 255 (Bitmuster)

@nativ
funktion byte_satt(a: f64) -> u8:
    gib_zurück als_saettigend(u8, a)  # 300.7 -> 255, -5.0 -> 0, NaN -> Fehler

@nativ
funktion gerundet(a: i32) -> i64:
    gib_zurück als(i64, als(f32, a))  # Ganzzahl -> Float rundet, Float -> Ganzzahl prueft
```

Englische Schreibweisen: `as`, `as_wrap`, `as_saturating`.

**Vergleiche und Logik** liefern `bool`; `und` und `oder` werten den rechten
Operanden nur bei Bedarf aus (Kurzschluss), auch seine Fehler bleiben dann
aus:

```moo
@nativ
funktion sicher(a: i32, b: i32) -> bool:
    gib_zurück b != 0 und a / b > 2   # b = 0 -> falsch, kein Fehler

zeige sicher(7, 0)        # falsch
zeige sicher(7, 2)        # wahr
```

**Grenze zum dynamischen Code:** Argumente werden beim Aufruf geprüft, nicht
umgedeutet. Eine Zahl für einen `u8`-Parameter muss ganzzahlig und im
Bereich sein, ein `bool`-Parameter braucht `wahr`/`falsch`; sonst gibt es
einen fangbaren Fehler. Das gilt auch, wenn der Aufruf in einer Funktion
mitten in einem Ausdruck steht (etwa als Argument einer anderen Funktion):
der fehlerhafte Aufruf liefert `nichts`, der Fehler landet im nächsten
`fange`. Das gilt für Systemkörper wie für reine `@nativ`-Funktionen; nur aus
Lambdas und Methoden heraus sind dynamische Argumente weiterhin ein Compilefehler. Rückgaben kommen als moo-Zahl bzw. Wahrheitswert
zurück, 64-Bit-Werte nur, wenn sie exakt darstellbar sind.

**Ohne moo-Laufzeit** (Bare-Metal/UEFI sowie Funktionen mit `@einstieg`,
die aus C aufgerufen werden) gibt es keinen fangbaren Fehler: dort hält ein
Laufzeitfehler die CPU sofort an (`llvm.trap`), damit kein falscher Wert
weiterverwendet wird. Die bestehenden Builtins `als_u8` bis `als_u64` für
dynamischen Code bleiben unverändert (Bitmuster-Konversion).

### Ganzzahlvektoren und `@simd`

Auf x86-64 stehen in typisierten nativen Funktionen `u8x16`, `u8x32`
und `u64x4` bereit. Die Zahl hinter `x` ist die Anzahl der Bahnen.
Ein Konstruktor ohne Argument setzt alle Bahnen auf null; ein skalares
Argument wird einmal ausgewertet und auf alle Bahnen verteilt.
Eine Liste oder einzelne Argumente müssen genau die Bahnanzahl haben.

```moo
@nativ
@simd
funktion byte_summe(a: u8x16, b: u8x16) -> u8x16:
    gib_zurück a + b
```

`+ - *`, unäres Minus sowie `& | ^ ~` arbeiten bahnweise.
Ganzzahlrechnungen wrappen in der jeweiligen Bahnbreite:
`u8x16(250) + u8x16(9)` enthält sechzehnmal `3`.
Beide Operanden brauchen denselben Vektortyp.
`==` und `!=` vergleichen den gesamten Wert und liefern `bool`.
Über `v[i]` liest oder schreibt man eine einzelne Bahn; der Index ist
`u64` und wird gegen die Bahnanzahl geprüft.

`simd_mischen(quelle, maske)` unterstützt beide Bytevektortypen.
Jedes Maskenbyte wählt mit seinen unteren vier Bits ein Byte innerhalb
derselben 16-Byte-Hälfte; gesetztes Bit 7 erzeugt null.
Bei `u8x32` bleiben die beiden Hälften getrennt.

`simd_gf256_mal(a, b)` multipliziert Bytevektoren bahnweise im endlichen
Körper mit Polynom `x^8+x^4+x^3+x^2+1` (`0x11D`), etwa `128 * 2 = 29`.
Die skalare Variante und die GFNI-Variante verwenden dasselbe Polynom.

`@simd` wählt zur Laufzeit eine verfügbare skalare, SSSE3-, AVX2-
oder GFNI-Variante. AVX benötigt auch die Freigabe durch das Betriebssystem.
Mit `@simd("skalar")`, `@simd("ssse3")`, `@simd("avx2")`,
`@simd("gfni128")` oder `@simd("gfni")` begrenzt man die erlaubte ISA.
`gfni128` verwendet höchstens 128-Bit-Instruktionen, auch auf AVX2-CPUs.
Fehlt ein Merkmal, bleibt die passende Rückfallvariante verfügbar.

Im Bare-/UEFI-Profil braucht Ganzzahlvektorrechnung `@simd`.
Der Funktionsrahmen sichert den Erweiterungszustand vor der Rechnung
und stellt ihn bei Rückgaben wieder her. Die Kernel-Timer sichern
auch den YMM-Zustand bei Präemption. Cleanup-Aufrufe behalten das ISA-Profil.
Der Bereich ist für Ganzzahlrechnung vorgesehen; skalare
Fließkommaparameter und Fließkommarechnung erhalten eine Diagnose.
Assembler innerhalb von `unsicher:` braucht eine eigene CPU-Prüfung.

Vektorwerte nutzen die vorhandene native Struktur-Speicher-ABI;
bei C-Aufrufen gelten die Strukturregeln. Gleichnamige eigene Typen und
Funktionen behalten Vorrang vor den eingebauten Namen.

### Native Schnittstellen

Eine native Struktur kann eine `schnittstelle` mit vollständig typisierten
Methoden `implementiert` deklarieren. Der erste Parameter ist der Empfänger;
im Strukturblock erhält `selbst` automatisch den Typ `ptr[Strukturname]`.
Die Methoden im Strukturblock sind nativ:

```moo
schnittstelle Zaehler:
    funktion wert(selbst, zusatz: u64) -> u64

struktur Basis implementiert Zaehler:
    start: u64
    funktion wert(selbst, zusatz: u64) -> u64:
        unsicher:
            gib_zurück selbst.start + zusatz

@nativ
funktion benutze(p: ptr[dyn Zaehler]) -> u64:
    unsicher:
        gib_zurück p.wert(5)

@nativ
funktion probe() -> u64:
    unsicher:
        setze basis: Basis auf Basis(20)
        setze p: ptr[Basis] auf zeiger(adresse_von(basis))
        gib_zurück benutze(p)   # 25
```

Ein `ptr[Basis]` ruft die bekannte Implementierung direkt auf.
`ptr[dyn Zaehler]` enthält Daten- und Vtablezeiger; die Vtable enthält
typisierte `fn[sysv64]`-Einträge. Die Übergabe eines passenden `ptr[Basis]`
an einen solchen Parameter oder eine entsprechend typisierte Variable erzeugt
die geliehene Ansicht. Dieselbe Interfacevariable kann anschließend auf eine
andere Implementierung zeigen.

Die Parameteranzahl, Ganzzahlbreite und Vorzeichen, vollständige Zeigerziele
und der Rückgabetyp müssen mit der Schnittstelle übereinstimmen. Fehlende
Methoden und Konvertierungen ohne registrierte Implementierung sind
Compilefehler. Die Vtable-Signaturen verwenden die vorhandenen SysV-C-Werttypen:
Skalare, Zeiger und native Strukturwerte.

Das Dateneigentum bleibt beim Aufrufer. Er hält den Speicher für jeden Zugriff
über die Ansicht am Leben. Methoden werden auf `ptr[T]` oder `ptr[dyn S]`
aufgerufen; Speicherzugriffe im Methodenkörper stehen wie gewohnt in
`unsicher:`. Native Schnittstellen mit SysV-Vtable sind für x86-64 vorgesehen.

### Native Ergebniswerte

`ergebnis[T, F]` ist ein nativer Wert mit Erfolgsnutzlast `T` oder
Fehlernutzlast `F`. Beide Typen sind feste native Werttypen; `void` ist für
eine Variante ohne Nutzwert erlaubt. Der Compiler erzeugt den Speicher im
Aufrufrahmen. Das Ergebnis selbst benötigt keine Heapallokation.

```moo
@nativ
funktion eingabe_pruefen(x: u64) -> ergebnis[u64, u32]:
    wenn x > 1000:
        gib_zurück fehlschlag(1)
    gib_zurück erfolg(x)

@nativ
funktion naechster_wert(x: u64) -> ergebnis[u64, u32]:
    setze wert: u64 auf weitergeben(eingabe_pruefen(x))
    gib_zurück erfolg(wert + 1)

@nativ
funktion auswerten(x: u64) -> u64:
    setze r: ergebnis[u64, u32] auf naechster_wert(x)
    wenn ist_erfolg(r):
        gib_zurück ergebnis_wert(r)
    gib_zurück als(u64, ergebnis_fehler(r))
```

`erfolg(wert)` und `fehlschlag(fehler)` erhalten ihren vollständigen Ergebnistyp
aus der Rückgabe- oder Variablenannotation. Für `void` wird der entsprechende
Konstruktor ohne Argument aufgerufen, etwa `erfolg()`.
`ist_erfolg(r)` prüft die Variante. `ergebnis_wert(r)` und
`ergebnis_fehler(r)` packen den zugehörigen Nutzwert aus; die falsche Variante
führt zum nativen Fehlerpfad. Die internen Felder des Ergebnisses sind opak.

`weitergeben(r)` liefert bei Erfolg den Nutzwert. Bei Fehler kehrt die
aktuelle Funktion sofort mit diesem Fehlerwert zurück und führt dabei ihre
registrierten `aufräumen`-Aktionen aus. Der Fehlertyp muss exakt mit dem
Fehlertyp der aktuellen Funktion übereinstimmen. Der Erfolgstyp darf sich
ändern. Bei `ergebnis[void, F]` steht die Weitergabe als eigene Anweisung.

Ergebniswerte müssen auf jedem folgenden Kontrollflusspfad geprüft,
ausgepackt oder weitergegeben werden. Verwerfen eines Aufrufresultats,
ungelesenes Überschreiben und ein unbenutztes Ergebnis in `aufräumen` sind
Compilefehler. Eine Nutzung ausschließlich im Schleifenkörper genügt nicht,
weil die Schleife null Iterationen ausführen kann. Die Leihprüfung gilt auch
für Zeiger in beiden Varianten und für den impliziten Fehlerrückweg der
Weitergabe. Ergebniswerte werden innerhalb typisierter `@nativ`-Körper
verarbeitet. Der dynamische MooValue-Adapter führt kein implizites Boxing aus.

**Layout und ABI:** Der Compiler senkt den Typ auf eine private native Struktur
ab: zuerst ein `u64`-Tag (`0` für Erfolg, `1` für Fehler), danach die
Erfolgs- und Fehlernutzlast in dieser Reihenfolge. Für `void` entfällt das
entsprechende Feld. Inaktive Nutzlastfelder werden mit null initialisiert;
Größe, Padding und Ausrichtung folgen dem nativen Strukturlayout.

Moo-interne Aufrufe verwenden den vorhandenen Strukturwertvertrag: Der Aufrufer
stellt einen exklusiven `sret`-Puffer, dessen Zeiger der erste Parameter ist
(x86-64 SysV: `RDI`; Win64: `RCX`). Die Funktion gibt auf LLVM-Ebene
`void` zurück. Strukturparameter werden über exklusive Aufruferkopien
übergeben. Schnittstellen-Vtables und `@rueckruf` verwenden die vorhandenen
typisierten C-ABI-Thunks. Beispielsweise ist `ergebnis[u32, u32]` 16 Byte
groß: SysV führt den C-Rückgabewert in `RAX/RDX`, Win64 verwendet einen
Ergebnispuffer. `ergebnis[u64, u32]` benötigt 24 Byte und verwendet auch
im SysV-C-ABI einen Ergebnispuffer.

### Endian-Felder und geprüfte Datenansichten

Native Strukturen können `u16be`, `u16le`, `u32be`, `u32le`, `u64be`
und `u64le` als Feldtypen verwenden. Der Feldzugriff liefert bzw. erwartet
den entsprechenden normalen Ganzzahltyp, beispielsweise `u32` bei `u32be`.
Konstruktoren und Feldzuweisungen kodieren die Bytes; Lesen dekodiert sie.
Die Bytefolge hängt damit nicht von der Rechnerarchitektur ab. Für ein
Wireformat ohne Padding wird die Struktur ausdrücklich mit `@gepackt` versehen.

```moo
@gepackt
struktur Paketkopf:
    laenge: u16be
    kennung: u32le

@nativ
funktion paket_schreiben(bytes: puffer[u8], versatz: u64) -> u32:
    setze kopf: puffer[Paketkopf] auf sicht(bytes, Paketkopf, versatz)
    kopf[0].laenge = 512
    kopf[0].kennung = 0x11223344
    gib_zurück kopf[0].kennung
```

`sicht(bytes, Strukturtyp, versatz)` liefert eine geliehene
`puffer[Strukturtyp]`-Ansicht mit genau einem Element. Vor der Erstellung
prüft der Compiler zur Laufzeit den vollständigen Strukturbereich gegen die
Bytepufferlänge, auch bei sehr großen Versätzen. Ein Grenzverstoß folgt dem
nativen Fehlerpfad und kann im gehosteten Aufrufer mit `versuche/fange`
behandelt werden. Ansicht und Feldzugriffe benötigen kein `unsicher:`;
die Erstellung einer Byteansicht aus einem rohen Zeiger benötigt es weiterhin.

Der Speicher gehört weiterhin dem Besitzer des Bytepuffers und muss während
aller Zugriffe leben. Ansichten auf lokale Arrays dürfen die Funktion nicht
verlassen. Für sichere Byteansichten sind Bytefelder, Endian-Felder, eingebettete
solche Strukturen und Bytearrays erlaubt. Zeiger, `bool`, `atom[T]` und
hostabhängige Wortfelder werden abgewiesen. Endianbezeichnungen sind native
Speicherfeldtypen; sie sind keine eigenständigen Rechentypen oder Casts und
stehen derzeit nicht unmittelbar als Arrayelementtyp zur Verfügung.

### Hardware-Syntax (Bare-Metal/UEFI)

Hardwarezugriff steht typisiert in `@nativ`-Funktionen, innerhalb von `unsicher:`:
`volatile_lesen`/`volatile_schreiben`, Barrieren, `port_lese`/`port_lese16`/`port_lese32`,
`port_schreibe`/`port_schreibe16`/`port_schreibe32` (EN `port_in*`/`port_out*`),
`assembler`, `@unterbrechung`, `@nackt`, `@aufrufkonvention`. Port-Operanden dürfen
Umwandlungen wie `u16(0xCF8)` sein; passt der Typ nicht, nennt die Meldung den erwarteten Typ.

```moo
@nativ
funktion pci_lese(adresse: u32) -> u32:
    unsicher:
        port_schreibe32(u16(0xCF8), adresse)
        gib_zurück port_lese32(u16(0xCFC))
```

Das Beispiel läuft nur im Bare-/UEFI-Profil (`--no-stdlib --target x86_64-bare`); auf
dem Host meldet der Compiler „Hardware-Intrinsics sind nur im Bare-Profil verfügbar“.
Ein reiner Hardwarekörper mit skalarer Signatur ist auch aus dynamischem Moo aufrufbar
(Beleg: `compiler/tests/p7/nativ_uefi_dyn.moos`). Nicht aufrufbar sind `@unterbrechung`,
`@nackt`, fremde `@aufrufkonvention` und `ptr[T]`-Parameter; die Meldung nennt den Grund.

**Veraltet:** `kern_inw`/`kern_outw`/`kern_inl`/`kern_outl` sind Übergangsaliase
für `port_lese16`/`port_schreibe16`/`port_lese32`/`port_schreibe32`; sie melden den
Hinweis `M-H008` und entfallen später. `speicher_lesen`/`speicher_schreiben` bleiben
(Rohzugriff auf gewöhnlichen Speicher; Geräteregister über `volatile_*`).

---

## Moo-Threads im nativen Kernel

Für x86-64 wählt `--kernel` mit Standardruntime den nativen Thread-Träger.
Dieselben Aufrufe für `starte`, `warten`, Kanäle, `atom` und `sperre`
verwenden dann den Moo-Kernelplaner, seine Mutexe und Bedingungen:

```bash
moo compile compiler/tests/p2/rc_geteilt/atom_zaehler.moos \
    --target x86_64-bare --kernel -o atom-kern.elf
```

Das ELF wird hinter dem Moo-UEFI-Loader gestartet. Der Programmeinstieg erfolgt
nach dem Start von GS-Zustand und Planer. `--no-stdlib` wählt weiterhin das
Minimalprofil für Loader und reine Hardwareprogramme; dort stehen die
typisierten nativen Atom-Intrinsics zur Verfügung.

Fehlerzustand und `versuche` gehören jeweils zum aktuellen Faden. Der erste
Wartende erhält das Thread-Ergebnis und darf erneut darauf warten; ein anderer
Faden erhält einen fangbaren Fehler. Auch die Freigabe eines Ergebnisses aus
einem fremden Faden wird an dessen Besitzer übergeben. Der Laufzeitheap umfasst
16 MiB; die verfügbaren Fadenplätze bestimmt der Kernelplaner. Sind seine Plätze
belegt, meldet `starte` einen fangbaren Fehler.

## Native Benutzerprogramme unter MoOS

Das Ziel `x86_64-moos` erzeugt ein statisches x86-64-ELF; zum Linken
wird GNU ld benötigt. Das native Hauptprogramm exportiert
`benutzer_haupt() -> u64` und importiert den Start aus `moos_syscall`:

```bash
moo compile beispiele/moos/hallo_benutzer.moos \
    --target x86_64-moos -o HALLO.ELF
```

Der Rückgabewert beendet den Prozess. `moos_schreiben`, `moos_abgeben`
und `moos_schlafen` verwenden die Systemaufrufe des Kerns. Der Lader prüft
Segmentgrenzen, überlappende Seitenspannen und W^X. Jeder Prozess erhält
einen privaten, nicht ausführbaren Stack mit Wächterseite; seine Daten und
Seitentabellen werden nach dem letzten Kontextwechsel freigegeben.


Der Träger `beispiele/moos/moos_laufzeit.moos` stellt im Benutzerraum dieselben
Moo-Operationen `starte`, `warten`, `atom`, `sperre` und `kanal` bereit.
Die Beispielhülle importiert diesen Träger und die unveränderte Konto-Fixture:

```bash
moo compile skripte/p8/smp/benutzer_sperre_konto.moos \
    --target x86_64-moos -o KONTO.ELF
```

Fehlerzustand und Besitzübergaben gehören zum einzelnen Benutzerfaden. Der
erste Wartende besitzt das Ergebnis und kann erneut warten; ein fremder
Wartender erhält den gleichen fangbaren Fehler wie im gehosteten Programm.
Der Benutzer-Träger verwendet die gemeinsame Moo-Laufzeit und einen privaten
16-MiB-Heap. Pro Prozess stehen ein Hauptfaden und bis zu 15 weitere Fäden mit
je 16-KiB-Stack, Wächterseite und eigener TLS-Seite bereit.

Die native Schnittstelle `moos_faden_neu(einstieg, daten)` liefert einen
prozessgebundenen Griff. Der Einstieg erhält `daten` nach SysV in RDI und endet
mit `moos_ende(kode)`. `moos_faden_warten(griff)` verbraucht diesen nativen
Griff einmal und liefert den Exitcode; vor einer Wiederverwendung des Platzes
ist auch der letzte Kernelstackwechsel abgeschlossen. Alte Griffgenerationen
sind ungültig.

`moos_futex_warten(adresse, erwartet)` prüft ein ausgerichtetes, schreibbares
32-Bit-Wort und reiht den Faden ohne verlorenes Wecken ein.
`moos_futex_wecken(adresse, anzahl)` weckt höchstens diese Anzahl im selben
Prozess. Ein geänderter Wert liefert `-11`; nach dem Aufwachen prüft die
Benutzerschleife ihre Bedingung erneut. Ungültige Adressen liefern `-14`,
ungültige Griffe/Argumente `-22`, unbekannte Aufrufe `-38`, jeweils als
`u64`-Bitmuster. `moos_tls()` liefert die eigene TLS-Basis; die ersten
32 Bytes sind für den Startvertrag reserviert.

## Funktionen als Werte (First-Class)

Funktionsnamen sind in moo gewöhnliche Werte. Du kannst sie einer Variable
zuweisen, in Listen legen oder an andere Funktionen übergeben:

```moo
funktion verdopple(x):
    gib_zurück x * 2

setze f auf verdopple
zeige f(21)               # 42

# An Thread übergeben:
setze t auf starte(verdopple, 21)
zeige t.warten()          # 42
```

## Lambdas und Closures

Lambdas sind anonyme Funktionen in Kurzform. Sie können Variablen aus dem
umgebenden Scope **einfangen** (Closure):

```moo
# Einfaches Lambda
setze quadrat auf (x) => x * x
zeige quadrat(5)          # 25

# Closure — das Lambda faengt `base` ein
setze base auf 100
setze addiere auf (x) => x + base
zeige addiere(5)          # 105
```

Closures funktionieren auch in Threads — das Environment bleibt erhalten:

```moo
setze offset auf 50
setze t auf starte((x) => x + offset, 100)
zeige t.warten()          # 150
```

## Einzeiler: Method-Chaining mit Lambdas

`list.map` und `list.filter` akzeptieren jeden First-Class-Funktionswert —
Inline-Lambdas, benannte Funktionen oder Variablen:

```moo
# Inline
zeige [1,2,3].map((x) => x * 2)                       # [2, 4, 6]

# Benannte Funktion als Callback
funktion ist_gerade(x):
    gib_zurück x % 2 == 0
zeige [1,2,3,4,5].filter(ist_gerade)                  # [2, 4]

# Chaining in einer Zeile
zeige [1,2,3,4,5].filter((x) => x > 2).map((x) => x * 10)   # [30, 40, 50]
```

---

## Dein erstes neuronales Netz (KI)

moo hat einen eingebauten KI-Baukasten. Du kannst ein echtes neuronales Netz
bauen, trainieren und benutzen — in 5 Zeilen, ohne irgendetwas zu installieren.

### Stufe 1: Ein Netz lernt XOR

XOR ist die "Hallo Welt"-Aufgabe der KI: das Netz soll lernen, dass
`[0,1]` und `[1,0]` eine `1` ergeben, aber `[0,0]` und `[1,1]` eine `0`.

```moo
setze daten auf tensor_aus_liste([[0,0],[0,1],[1,0],[1,1]])
setze ziele auf tensor_aus_liste([[0],[1],[1],[0]])
setze netz auf ki_netz([schicht_dicht(2, 8, "tanh"), schicht_dicht(8, 1, "sigmoid")])
netz.trainiere(daten, ziele, {"epochen": 400, "rate": 0.05})
zeige netz.vorhersage(daten).zu_liste()
```

Das war's! `trainiere` druckt dir bei jeder Epoche den Fehler — du siehst
live zu, wie das Netz besser wird. Am Ende liegen die Vorhersagen ganz
nah an `0, 1, 1, 0`.

**Was passiert da?** `schicht_dicht(2, 8, "tanh")` ist eine Schicht aus
8 künstlichen Neuronen, die 2 Eingaben bekommen. `ki_netz` stapelt die
Schichten. `trainiere` zeigt dem Netz die Beispiele immer wieder und
korrigiert nach jedem Durchgang die Verbindungen ein kleines Stück.

### Stufe 2: Optionen, Genauigkeit, Speichern

Die Optionen übergibst du als Dict — alles hat sinnvolle Standardwerte:

```moo
setze verlauf auf netz.trainiere(daten, ziele, {
    "epochen": 400,        # wie oft alle Beispiele gezeigt werden
    "rate": 0.05,          # wie große Lernschritte das Netz macht
    "batch": 4,            # wie viele Beispiele pro Schritt
    "optimierer": "adam",  # oder "sgd", "adamw"
    "ausgabe": 0           # 0 = still trainieren
})
zeige "Fehler am Ende: " + text(verlauf[länge(verlauf) - 1])
zeige "Genauigkeit: " + text(netz.genauigkeit(daten, ziele))

netz.speichern("mein_netz.mook")
setze kopie auf ki_laden("mein_netz.mook")
zeige kopie.vorhersage(daten).zu_liste()
```

`trainiere` gibt dir den Fehler-Verlauf als Liste zurück — perfekt, um
mit der 2D-API eine eigene Lernkurve zu malen. `.mook`-Dateien sind
übrigens das safetensors-Format, das auch die großen KI-Werkzeuge lesen.

Für längere Trainings gibt es Profi-Techniken als weitere Optionen:
`"clip": 1.0` kappt zu große Lernschritte (verhindert Explosionen),
`"lr_plan": "cosine"` (oder `"step"`, `"warmup"`) verändert die Lernrate
über die Zeit, `"geduld": 5` stoppt automatisch, wenn sich 5 Epochen
lang nichts verbessert (`"min_besserung"` stellt ein, was als
Verbesserung zählt), und `"checkpoint": "bestes.mook"` speichert
laufend das beste Modell. Low-Level gibt es dafür
`gradienten_kappen(parameter(schichten), 1.0)` — und `opt["rate"]`
darfst du zwischen Schritten einfach selbst ändern.

Endet dein Netz auf `"softmax"` (Klassen-Raten, z.B. Ziffern 0–9), nimmt
`trainiere` automatisch den richtigen Fehler (Kreuzentropie) und du darfst
als Ziele einfach die Klassen-Nummern übergeben: `tensor_aus_liste([0, 1, 2])`.

Auf Englisch heißt alles genauso, nur übersetzt: `ai_net`, `train`,
`predict`, `accuracy`, `save`, `ai_load`.

### Stufe 3: Unter die Haube schauen

Die Kinderleicht-Schicht ist nur Zucker. Darunter liegen Tensoren mit
Autograd — dieselben Bausteine, aus denen auch die großen Frameworks
bestehen. Du kannst jederzeit selbst schalten und walten:

```moo
setze schichten auf [schicht_dicht(2, 8, "tanh"), schicht_dicht(8, 1, "sigmoid")]
setze opt auf optimierer_adam(parameter(schichten), 0.05)
setze i auf 0
solange i < 400:
    setze fehler auf mse(vorwaerts(schichten, daten), ziele)
    fehler.rueckwaerts()     # Ableitungen ausrechnen (Autograd)
    opt.schritt()            # Gewichte anpassen
    setze i auf i + 1
```

Genau das macht `trainiere` intern — plus Mischen und Batches. Es gibt
auch `schicht_dropout`, `schicht_layernorm`, `schicht_embedding`,
`kreuzentropie` und `optimierer_adamw`, wenn du tiefer einsteigen willst.
Schau dir `beispiele/ki_xor.moos` (14 Zeilen) und `beispiele/neuralnet.moos`
(dasselbe komplett von Hand, 321 Zeilen) an — der Unterschied ist der
ganze Punkt.

### Stufe 4: Ein kleines Sprachmodell — der Transformer-Baukasten

Dieselben Bausteine reichen für die Architektur hinter ChatGPT & Co. —
nur viel, viel kleiner. Drei neue Teile kommen dazu:

**1. Der Zeichen-Tokenizer** macht aus Text Zahlen (und zurück). Er
arbeitet auf echten Zeichen, nicht auf Bytes — `"ä"` ist EIN Zeichen:

```moo
setze tok auf text_tokenizer("Es war einmal …")
zeige tok["vokab"]              # wie viele verschiedene Zeichen
zeige tok["ids"]                # der Text als Tensor von Zeichen-Nummern
zeige tok["id_zu_zeichen"][0]   # und zurück: Nummer -> Zeichen
```

**2. Zwei neue Schichten:** `schicht_attention(dim, koepfe)` lässt jedes
Zeichen auf alle Zeichen davor „schauen“ (kausal — in die Zukunft schauen
ist verboten), und `schicht_position(max, dim, "gelernt")` sagt dem Modell,
WO in der Folge ein Zeichen steht.

**3. Residual-Verbindungen** schreibst du in moo einfach als Rechnung —
der `+`-Operator auf Tensoren läuft über den Autograd-Tape, die
Ableitungen fließen also durch beide Wege:

```moo
setze x auf x + vorwaerts([norm, attention], x)   # Residual!
```

Das komplette Beispiel ist `beispiele/ki_sprachmodell.moos`: ein
Mini-Transformer (2 Blöcke, ~120.000 Parameter) lernt auf echten
Grimm-Märchen (`./skripte/maerchen_download.sh` holt den Text), das
nächste Zeichen vorherzusagen. Nach ~90 Sekunden CPU-Training wird aus
Zeichensalat erkennbar deutscher Text mit Wörtern und Satzzeichen.

Und damit du die richtigen Erwartungen hast: Das ist ein
**Lern-Demonstrator, kein Chatbot**. Es zeigt ehrlich und beweisbar,
DASS ein Transformer lernt (der Verlust sinkt messbar, der Text bekommt
Struktur) — für mehr braucht es Millionen Mal mehr Daten und Rechenzeit.
Genau deshalb ist es so lehrreich: Es ist dieselbe Mathematik.

## Kamera und Mikrofon als KI-Eingabe

Moo kann unter Linux Bilder und Audio synchron per Pull lesen. Die Handles
sind refcountet und sollten trotzdem explizit geschlossen werden:

```moo
setze kamera auf kamera_oeffnen()                # camera_open
setze frame auf kamera_frame(kamera, 1000)       # camera_frame
setze bild_tensor auf tensor_aus_frame(frame, "rgb")
kamera_schliessen(kamera)                        # camera_close
```

`kamera_liste` (`camera_list`) listet Capture-Geräte. Bei
`kamera_oeffnen(pfad?, breite?, hoehe?, fps?)` müssen vollständig angegebene
Formatwerte exakt unterstützt werden. Ausgelassene Werte werden
deterministisch nahe 640×480 bei 30 FPS gewählt. `kamera_frame` liefert immer
einen eigenen kompakten RGBA8-Frame. Die Kamera arbeitet mit
Latest-Frame-Semantik: ältere fertige Treiberbuffer dürfen verworfen werden.

```moo
setze mikro auf mikro_oeffnen(48000, 1, "default")  # microphone_open
setze block auf mikro_lesen(mikro, 48000, 2000)     # microphone_read
zeige block["rate"]                                  # tatsächliche Rate
setze spec auf spektrogramm(block["daten"], 1024, 512)
mikro_schliessen(mikro)                              # microphone_close
```

Timeouts gelten für den vollständigen Aufruf. `0` blockiert nicht; erlaubt
sind 0 bis 60000 ms. Fehler unterscheiden Timeout, Belegt/Berechtigung,
nicht unterstützte Formate, Disconnect/BROKEN, nicht reparierbaren
XRUN/Suspend und bereits geschlossene Handles. Ein Audio-Timeout liefert
niemals einen still verkürzten Tensor. Vollständige Beispiele:
`beispiele/ki_kamera_live.moos` und `beispiele/ki_mikro_spektrum.moos`.
Das zusätzliche privilegierte Integrationsgate
`compiler/runtime/tests/run_capture_loopback.sh` verwendet ein echtes
`v4l2loopback`-Gerät plus `snd-aloop`; ohne diese Runner-Voraussetzungen
meldet es transparent Skip 77 statt einen Scheinerfolg.

## Tipps für Anfänger

1. **Einrückung ist wichtig!** Verwende 4 Leerzeichen für jeden Block.
2. **Kommentare** beginnen mit `#` — sie werden vom Programm ignoriert.
3. **Strings** werden in Anführungszeichen geschrieben: `"Text"` oder `'Text'`.
4. **Listen zählen ab 0** — das erste Element ist `liste[0]`.
5. **Deutsch oder Englisch** — du kannst sogar mischen!
6. **Fehler sind normal** — lies die Fehlermeldung, sie zeigt dir die Zeile.
