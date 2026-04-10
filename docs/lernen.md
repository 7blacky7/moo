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
7. [Bedingungen](#bedingungen)
8. [Schleifen](#schleifen)
9. [Listen](#listen)
10. [Dictionaries](#dictionaries)
11. [Funktionen](#funktionen)
12. [Lambdas](#lambdas)
13. [Klassen & Objekte](#klassen--objekte)
14. [Vererbung](#vererbung)
15. [Fehlerbehandlung](#fehlerbehandlung)
16. [Match / Switch](#match--switch)
17. [Module & Imports](#module--imports)
18. [Schlüsselwort-Tabelle](#schlüsselwort-tabelle)
19. [CLI-Befehle](#cli-befehle)

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

Kurze Einmal-Funktionen:

```
setze verdopple auf (x) => x * 2
zeige verdopple(21)      # 42

setze addiere auf (a, b) => a + b
zeige addiere(3, 4)      # 7
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

---

## CLI-Befehle

```bash
# Programm ausführen
moo run datei.moo

# Nach Python übersetzen
moo build datei.moo -t python

# Nach JavaScript übersetzen
moo build datei.moo -t javascript

# In Datei speichern
moo build datei.moo -t python -o ausgabe.py
moo build datei.moo -t javascript -o ausgabe.js
```

---

## Tipps für Anfänger

1. **Einrückung ist wichtig!** Verwende 4 Leerzeichen für jeden Block.
2. **Kommentare** beginnen mit `#` — sie werden vom Programm ignoriert.
3. **Strings** werden in Anführungszeichen geschrieben: `"Text"` oder `'Text'`.
4. **Listen zählen ab 0** — das erste Element ist `liste[0]`.
5. **Deutsch oder Englisch** — du kannst sogar mischen!
6. **Fehler sind normal** — lies die Fehlermeldung, sie zeigt dir die Zeile.
