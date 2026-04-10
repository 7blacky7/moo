# moo Phase 2 — Vollständige Sprache

> **Execution:** Subagent-Driven Development — parallele Agenten pro Arbeitspaket

**Goal:** moo zu einer vollständigen, produktionsreifen Sprache ausbauen mit dem Besten aus Python, JS, Rust, Go, Kotlin und Elixir.

**Architecture:** Erweiterung der bestehenden Tagged-Value Runtime + neue Syntax im Lexer/Parser + Codegen-Erweiterungen. Kein Umbau nötig.

---

## Arbeitspakete (parallel ausführbar)

### AP1: String-Features (f-Strings, Slicing)
**Neue Syntax:**
```
# f-Strings (Template Literals)
zeige f"Hallo {name}, du bist {alter} Jahre alt"
zeige f"Ergebnis: {2 + 3}"

# String-Slicing
setze text auf "Hallo Welt"
zeige text[0..5]          # "Hallo"
zeige text[6..]           # "Welt"
```

**Änderungen:**
- Lexer: f"..." Token mit Expression-Interpolation
- Parser: FStringExpr AST-Knoten
- Runtime: moo_string_slice(str, start, end)
- Codegen: f-String → concat-Kette

---

### AP2: List Comprehensions & Erweiterte Iteratoren
**Neue Syntax:**
```
# List Comprehension
setze quadrate auf [x * x für x in 0..10]
setze gerade auf [x für x in 0..20 wenn x % 2 == 0]

# Map/Filter als Methoden
setze verdoppelt auf zahlen.map((x) => x * 2)
setze gefiltert auf zahlen.filter((x) => x > 5)
setze summe auf zahlen.reduce((a, b) => a + b, 0)

# Tuple Unpacking
setze a, b, c auf [1, 2, 3]
```

**Änderungen:**
- Parser: ListComprehension AST-Knoten
- Runtime: moo_list_map, moo_list_filter, moo_list_reduce (Funktionspointer)
- Codegen: Comprehension → Loop + Append

---

### AP3: Pipe Operator & Funktionale Features
**Neue Syntax:**
```
# Pipe Operator (wie Elixir)
setze ergebnis auf daten
    |> filtern((x) => x > 0)
    |> sortieren()
    |> erste(5)

# Spread Operator
setze alles auf [...liste1, ...liste2]
setze merged auf {...dict1, ...dict2}

# Destructuring
setze {name, alter} auf person
setze [erste, zweite, ...rest] auf liste
```

**Änderungen:**
- Lexer: |> Token, ... Token
- Parser: PipeExpr, SpreadExpr, DestructuringAssign
- Runtime: keine neuen Funktionen nötig (syntaktischer Zucker)

---

### AP4: Null-Safety & Optional Chaining
**Neue Syntax:**
```
# Optional Chaining (wie Kotlin/JS)
zeige person?.adresse?.strasse    # None statt Crash

# Nullish Coalescing
setze name auf eingabe ?? "Unbekannt"

# Guard/Garantiere (wie Swift)
garantiere alter >= 18, sonst:
    zeige "Zu jung!"
    gib_zurück
```

**Änderungen:**
- Lexer: ?. Token, ?? Token, garantiere/guard Keyword
- Parser: OptionalChain, NullishCoalesce, GuardStatement
- Codegen: ?. → null-Check + Branch, ?? → is_none Check

---

### AP5: Async/Await & Multithreading
**Neue Syntax:**
```
# Async/Await
async funktion lade_daten(url):
    setze antwort auf warte http_get(url)
    gib_zurück antwort

# Parallel ausführen
setze ergebnisse auf parallel:
    lade_daten("url1")
    lade_daten("url2")
    lade_daten("url3")

# Threads (einfach)
setze t auf starte (x) => schwere_berechnung(x)
setze ergebnis auf t.warten()

# Channel-Kommunikation (wie Go)
setze kanal auf kanal()
starte () => kanal.senden(42)
zeige kanal.empfangen()
```

**Änderungen:**
- Runtime: moo_async.c mit pthread-basiertem Thread-Pool
- Runtime: moo_channel.c für Go-style Channels
- Neue Tags: MOO_FUTURE, MOO_THREAD, MOO_CHANNEL
- Lexer/Parser: async, warte/await, parallel, starte/spawn, kanal/channel
- Codegen: async → Future-Wrapper, parallel → Thread-Pool dispatch

---

### AP6: Netzwerk & HTTP
**Neue Syntax:**
```
# HTTP Requests
setze antwort auf http_get("https://api.example.com/daten")
zeige antwort.status
zeige antwort.body

setze ergebnis auf http_post("https://api.example.com/senden", {
    "name": "moo",
    "version": 1
})

# JSON
setze daten auf json_parse('{"name": "Anna"}')
setze text auf json_string(daten)

# TCP/UDP (low-level)
setze server auf tcp_server(8080)
setze client auf tcp_connect("localhost", 8080)
```

**Änderungen:**
- Runtime: moo_http.c (libcurl für HTTP)
- Runtime: moo_json.c (eigener JSON-Parser oder cJSON)
- Runtime: moo_net.c (TCP/UDP Sockets)
- Neue Builtins: http_get, http_post, json_parse, json_string
- Build: libcurl als Dependency

---

### AP7: Datei-I/O
**Neue Syntax:**
```
# Dateien lesen/schreiben
setze inhalt auf datei_lesen("config.txt")
datei_schreiben("output.txt", "Hallo Welt!")
datei_anhängen("log.txt", "Neue Zeile\n")

# Zeilenweise lesen
setze zeilen auf datei_zeilen("daten.csv")
für zeile in zeilen:
    zeige zeile

# Dateisystem
zeige datei_existiert("config.txt")
zeige verzeichnis_liste(".")
```

**Änderungen:**
- Runtime: moo_file.c (fopen/fread/fwrite + readdir)
- Builtins: datei_lesen/file_read, datei_schreiben/file_write, etc.

---

### AP8: Sicherheit
**Features:**
```
# Sichere Strings (kein Buffer Overflow)
# → Bereits implementiert (MooString mit Length-Tracking)

# Input-Sanitierung
setze sicher auf bereinige(user_input)    # HTML/SQL escapen

# Sichere Zufallszahlen (nicht rand())
setze token auf sichere_zufall(32)        # 32 Bytes kryptographisch sicher
setze hash auf sha256("passwort")

# Sandbox-Modus
# moo-compiler run --sandbox datei.moo    # Kein Netzwerk, kein Dateizugriff

# Timeout
mit_timeout 5000:                         # Max 5 Sekunden
    schwere_berechnung()
```

**Änderungen:**
- Runtime: moo_crypto.c (SHA256, sichere Zufallszahlen via /dev/urandom)
- Runtime: moo_security.c (Input-Sanitierung, Sandbox-Flags)
- CLI: --sandbox Flag das Netzwerk/Datei-Zugriff blockiert

---

### AP9: Error-Handling++ & Pattern Matching
**Neue Syntax:**
```
# Ergebnis-Typ (wie Rust Result)
funktion teile(a, b):
    wenn b == 0:
        gib_zurück fehler("Division durch Null")
    gib_zurück ok(a / b)

setze r auf teile(10, 0)
prüfe r:
    fall ok(wert):
        zeige wert
    fall fehler(msg):
        zeige "Fehler: " + msg

# Erweitertes Pattern Matching
prüfe person:
    fall {"name": "Anna", "alter": n} wenn n > 18:
        zeige "Erwachsene Anna"
    fall {"name": name}:
        zeige "Jemand namens " + name
    standard:
        zeige "Unbekannt"
```

**Änderungen:**
- Runtime: ok/fehler Wrapper (spezielles Tag-Paar)
- Parser: erweiterte Match-Patterns mit Bindings und Guards

---

### AP10: Paketmanager & Ecosystem
**Konzept:**
```bash
# Pakete installieren
moo paket installiere http        # Installiert moo-http Paket
moo paket installiere json
moo paket liste                   # Zeigt installierte Pakete

# In Code nutzen
importiere http
importiere json
```

**Änderungen:**
- CLI: `moo paket` Subcommand
- Registry: GitHub-basiert (moo-packages Repo)
- Resolver: Einfacher Dependency-Resolver

---

## Prioritätsmatrix

| AP | Feature | Impact | Aufwand | Parallelisierbar |
|----|---------|:------:|:-------:|:----------------:|
| AP1 | f-Strings, Slicing | Hoch | Mittel | ✓ |
| AP2 | Comprehensions, Map/Filter | Hoch | Mittel | ✓ |
| AP3 | Pipe, Spread, Destructuring | Mittel | Mittel | ✓ |
| AP4 | Null-Safety, Optional Chaining | Hoch | Leicht | ✓ |
| AP5 | Async, Threads, Channels | Hoch | Schwer | ✓ |
| AP6 | HTTP, JSON, Netzwerk | Hoch | Mittel | ✓ |
| AP7 | Datei-I/O | Hoch | Leicht | ✓ |
| AP8 | Sicherheit, Crypto | Mittel | Mittel | ✓ |
| AP9 | Pattern Matching++ | Mittel | Schwer | ✓ |
| AP10 | Paketmanager | Mittel | Schwer | Nein (braucht AP6+AP7) |

## Ausführungsreihenfolge

**Welle 1 (parallel):** AP1 + AP4 + AP7 (String-Features, Null-Safety, Datei-I/O)
**Welle 2 (parallel):** AP2 + AP6 + AP8 (Comprehensions, HTTP, Sicherheit)
**Welle 3 (parallel):** AP3 + AP5 (Pipe/Spread, Async/Threads)
**Welle 4 (sequentiell):** AP9 + AP10 (Pattern Matching, Paketmanager)
