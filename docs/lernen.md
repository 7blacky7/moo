# moo lernen 🐄

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
11. [String-Methoden](#string-methoden)
12. [Dictionaries](#dictionaries)
13. [Funktionen](#funktionen)
14. [Lambdas](#lambdas)
15. [Standardbibliothek (Stdlib)](#standardbibliothek-stdlib)
16. [Klassen & Objekte](#klassen--objekte)
17. [Vererbung](#vererbung)
18. [Fehlerbehandlung](#fehlerbehandlung)
19. [Match / Switch](#match--switch)
20. [Module & Imports](#module--imports)
21. [Schlüsselwort-Tabelle](#schlüsselwort-tabelle)
22. [CLI-Befehle](#cli-befehle)
23. [Nativer Compiler](#nativer-compiler)

---

## Installation

```bash
cd ~/dev/moo
uv sync
```

## Erstes Programm

Erstelle eine Datei `hallo.moo`:

```
zeige "Hallo Welt!"
```

Ausführen:

```bash
moo run hallo.moo
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

---

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

---

## CLI-Befehle

```bash
# Programm ausführen (Transpiler — über Python)
moo run datei.moo

# Nach Python übersetzen
moo build datei.moo -t python

# Nach JavaScript übersetzen
moo build datei.moo -t javascript

# In Datei speichern
moo build datei.moo -t python -o ausgabe.py
moo build datei.moo -t javascript -o ausgabe.js

# Nativer Compiler — direkt ausführen
moo-compiler run datei.moo

# Nativer Compiler — native Binary erzeugen
moo-compiler compile datei.moo
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
moo-compiler run hallo.moo
```

Kompiliert und führt das Programm in einem Schritt aus. Praktisch für die Entwicklung.

### Binary erzeugen

```bash
moo-compiler compile hallo.moo
```

Erzeugt eine eigenständige Binary-Datei, die ohne moo ausgeführt werden kann:

```bash
./hallo
```

### Alle nativen Features

Der native Compiler unterstützt alle in diesem Dokument beschriebenen Features:
Variablen, Bedingungen, Schleifen, Funktionen, Lambdas, Klassen, Vererbung,
String-Methoden, Eingabe, Stdlib-Funktionen, Fehlerbehandlung und Match/Switch.

---

## Tipps für Anfänger

1. **Einrückung ist wichtig!** Verwende 4 Leerzeichen für jeden Block.
2. **Kommentare** beginnen mit `#` — sie werden vom Programm ignoriert.
3. **Strings** werden in Anführungszeichen geschrieben: `"Text"` oder `'Text'`.
4. **Listen zählen ab 0** — das erste Element ist `liste[0]`.
5. **Deutsch oder Englisch** — du kannst sogar mischen!
6. **Fehler sind normal** — lies die Fehlermeldung, sie zeigt dir die Zeile.
