# ============================================================
# moo KOMPLETTE TESTSUITE
# ============================================================

setze ok auf 0
setze fail auf 0

# === 1. VARIABLEN & KONSTANTEN ===
setze x auf 42
wenn x == 42:
    ok += 1
sonst:
    fail += 1

konstante PI auf 3.14
wenn PI == 3.14:
    ok += 1
sonst:
    fail += 1

x += 8
wenn x == 50:
    ok += 1
sonst:
    fail += 1

# === 2. ARITHMETIK ===
wenn 3 + 4 == 7:
    ok += 1
sonst:
    fail += 1

wenn 10 - 3 == 7:
    ok += 1
sonst:
    fail += 1

wenn 6 * 7 == 42:
    ok += 1
sonst:
    fail += 1

wenn 17 % 5 == 2:
    ok += 1
sonst:
    fail += 1

wenn 2 ** 10 == 1024:
    ok += 1
sonst:
    fail += 1

wenn (2 + 3) * 4 == 20:
    ok += 1
sonst:
    fail += 1

# === 3. STRINGS ===
wenn "Hallo" + " Welt" == "Hallo Welt":
    ok += 1
sonst:
    fail += 1

wenn "ha" * 3 == "hahaha":
    ok += 1
sonst:
    fail += 1

wenn "Hallo"[0] == "H":
    ok += 1
sonst:
    fail += 1

wenn "Hallo"[-1] == "o":
    ok += 1
sonst:
    fail += 1

wenn "Hallo Welt"[0..5] == "Hallo":
    ok += 1
sonst:
    fail += 1

wenn "hallo".upper() == "HALLO":
    ok += 1
sonst:
    fail += 1

wenn "WELT".lower() == "welt":
    ok += 1
sonst:
    fail += 1

# === 4. F-STRINGS ===
setze name auf "moo"
wenn f"name={name}" == "name=moo":
    ok += 1
sonst:
    fail += 1

wenn f"2+3={2+3}" == "2+3=5":
    ok += 1
sonst:
    fail += 1

# === 5. BOOLEANS ===
wenn 5 == 5:
    ok += 1
sonst:
    fail += 1

wenn 5 != 3:
    ok += 1
sonst:
    fail += 1

wenn wahr und wahr:
    ok += 1
sonst:
    fail += 1

wenn nicht falsch:
    ok += 1
sonst:
    fail += 1

# === 6. BEDINGUNGEN ===
setze r auf ""
wenn 5 > 3:
    setze r auf "ja"
wenn r == "ja":
    ok += 1
sonst:
    fail += 1

setze n auf 15
setze r auf ""
wenn n > 20:
    setze r auf "gross"
sonst wenn n > 10:
    setze r auf "mittel"
sonst:
    setze r auf "klein"
wenn r == "mittel":
    ok += 1
sonst:
    fail += 1

# === 7. SCHLEIFEN ===
setze summe auf 0
setze i auf 0
solange i < 5:
    summe += i
    i += 1
wenn summe == 10:
    ok += 1
sonst:
    fail += 1

setze summe auf 0
für z in [10, 20, 30]:
    summe += z
wenn summe == 60:
    ok += 1
sonst:
    fail += 1

setze summe auf 0
für z in 0..5:
    summe += z
wenn summe == 10:
    ok += 1
sonst:
    fail += 1

# Break
setze r auf 0
setze i auf 0
solange i < 100:
    wenn i == 5:
        stopp
    r += 1
    i += 1
wenn r == 5:
    ok += 1
sonst:
    fail += 1

# Continue
setze r auf 0
für i in 0..10:
    wenn i % 2 == 0:
        weiter
    r += 1
wenn r == 5:
    ok += 1
sonst:
    fail += 1

# === 8. LISTEN ===
setze l auf [5, 3, 8, 1]
wenn l[0] == 5:
    ok += 1
sonst:
    fail += 1

wenn l[-1] == 1:
    ok += 1
sonst:
    fail += 1

l.append(10)
wenn l.length() == 5:
    ok += 1
sonst:
    fail += 1

setze s auf [3, 1, 2]
s.sort()
wenn s[0] == 1:
    ok += 1
sonst:
    fail += 1

s.reverse()
wenn s[0] == 3:
    ok += 1
sonst:
    fail += 1

# === 9. DICTIONARIES ===
setze d auf {"a": 1, "b": 2}
wenn d["a"] == 1:
    ok += 1
sonst:
    fail += 1

d["c"] = 3
wenn d["c"] == 3:
    ok += 1
sonst:
    fail += 1

# === 10. FUNKTIONEN ===
funktion quadrat(n):
    gib_zurück n * n

wenn quadrat(7) == 49:
    ok += 1
sonst:
    fail += 1

funktion gruss(wer, prefix = "Hallo"):
    gib_zurück prefix + " " + wer

wenn gruss("Welt") == "Hallo Welt":
    ok += 1
sonst:
    fail += 1

wenn gruss("Welt", "Hi") == "Hi Welt":
    ok += 1
sonst:
    fail += 1

# === 11. LAMBDAS & CLOSURES ===
setze doppelt auf (x) => x * 2
wenn doppelt(21) == 42:
    ok += 1
sonst:
    fail += 1

setze faktor auf 10
setze mult auf (x) => x * faktor
wenn mult(5) == 50:
    ok += 1
sonst:
    fail += 1

# === 12. KLASSEN / OOP ===
klasse Tier:
    funktion erstelle(art, laut):
        selbst.art = art
        selbst.laut = laut
    funktion sprechen():
        gib_zurück selbst.art + " macht " + selbst.laut

setze hund auf neu Tier("Hund", "Wuff")
wenn hund.sprechen() == "Hund macht Wuff":
    ok += 1
sonst:
    fail += 1

wenn hund.art == "Hund":
    ok += 1
sonst:
    fail += 1

hund.art = "Dogge"
wenn hund.art == "Dogge":
    ok += 1
sonst:
    fail += 1

# === 13. TRY/CATCH ===
setze gefangen auf ""
versuche:
    wirf "Testfehler"
fange err:
    setze gefangen auf err
wenn gefangen == "Testfehler":
    ok += 1
sonst:
    fail += 1

# === 14. MATCH ===
setze tag auf "Montag"
setze r auf ""
prüfe tag:
    fall "Montag":
        setze r auf "Start"
    standard:
        setze r auf "Andere"
wenn r == "Start":
    ok += 1
sonst:
    fail += 1

# === 15. OPTIONAL CHAINING ===
setze tier2 auf neu Tier("Katze", "Miau")
wenn tier2?.art == "Katze":
    ok += 1
sonst:
    fail += 1

# === 16. NULLISH COALESCING ===
wenn (nichts ?? "fallback") == "fallback":
    ok += 1
sonst:
    fail += 1

wenn ("da" ?? "fallback") == "da":
    ok += 1
sonst:
    fail += 1

# === 17. LIST COMPREHENSIONS ===
setze q auf [x * x für x in 0..6]
wenn q[3] == 9:
    ok += 1
sonst:
    fail += 1

setze g auf [x für x in 0..10 wenn x % 2 == 0]
wenn g[2] == 4:
    ok += 1
sonst:
    fail += 1

# === 18. MAP/FILTER ===
setze nums auf [1, 2, 3, 4, 5]
setze doubled auf nums.map((x) => x * 2)
wenn doubled[0] == 2:
    ok += 1
sonst:
    fail += 1

setze big auf nums.filter((x) => x > 3)
wenn big[0] == 4:
    ok += 1
sonst:
    fail += 1

# === 19. DATEI I/O ===
datei_schreiben("/tmp/moo_suite.txt", "test123")
wenn datei_lesen("/tmp/moo_suite.txt") == "test123":
    ok += 1
sonst:
    fail += 1

wenn datei_existiert("/tmp/moo_suite.txt"):
    ok += 1
sonst:
    fail += 1

datei_löschen("/tmp/moo_suite.txt")
wenn nicht datei_existiert("/tmp/moo_suite.txt"):
    ok += 1
sonst:
    fail += 1

# === 20. JSON ===
setze j auf json_parse('{"x": 42}')
wenn j["x"] == 42:
    ok += 1
sonst:
    fail += 1

# === 21. CRYPTO ===
setze h auf sha256("test")
wenn länge(h) == 64:
    ok += 1
sonst:
    fail += 1

setze b auf base64_encode("Hallo")
wenn base64_decode(b) == "Hallo":
    ok += 1
sonst:
    fail += 1

# === 22. DATENBANK ===
setze db auf db_verbinde("sqlite:///tmp/moo_suite.db")
db_ausführen(db, "CREATE TABLE t (id INTEGER, val TEXT)")
db_ausführen(db, "INSERT INTO t VALUES (1, 'eins')")
db_ausführen(db, "INSERT INTO t VALUES (2, 'zwei')")
setze rows auf db_abfrage(db, "SELECT * FROM t ORDER BY id")
wenn länge(rows) == 2:
    ok += 1
sonst:
    fail += 1
db_schliessen(db)
datei_löschen("/tmp/moo_suite.db")

# === 23. STDLIB ===
wenn abs(-5) == 5:
    ok += 1
sonst:
    fail += 1

wenn min(3, 7) == 3:
    ok += 1
sonst:
    fail += 1

wenn max(3, 7) == 7:
    ok += 1
sonst:
    fail += 1

# === 24. SPREAD ===
setze a auf [1, 2, 3]
setze b auf [4, 5, 6]
setze alles auf [...a, ...b]
wenn alles[0] == 1:
    ok += 1
sonst:
    fail += 1

wenn alles[5] == 6:
    ok += 1
sonst:
    fail += 1

wenn länge(alles) == 6:
    ok += 1
sonst:
    fail += 1

# Dict Spread
setze basis auf {"x": 1}
setze merged auf {...basis, "y": 2}
wenn merged["x"] == 1:
    ok += 1
sonst:
    fail += 1

wenn merged["y"] == 2:
    ok += 1
sonst:
    fail += 1

# === 25. ZWEISPRACHIG ===
set greeting to "Hello"
set result to greeting + " " + name
wenn result == "Hello moo":
    ok += 1
sonst:
    fail += 1

# === 26. RESULT-TYP (Rust) ===
setze r auf ok(42)
wenn ist_ok(r):
    ok += 1
sonst:
    fail += 1

setze r2 auf fehler("kaputt")
wenn ist_fehler(r2):
    ok += 1
sonst:
    fail += 1

setze wert auf entpacke(ok(99))
wenn wert == 99:
    ok += 1
sonst:
    fail += 1

# === 27. TERNARY-OPERATOR (Swift) ===
setze s auf 5 > 3 ? "ja" : "nein"
wenn s == "ja":
    ok += 1
sonst:
    fail += 1

setze t auf 1 > 10 ? "gross" : "klein"
wenn t == "klein":
    ok += 1
sonst:
    fail += 1

# === ERGEBNIS ===
zeige ""
zeige "=" * 50
setze total auf ok + fail
zeige f"{total} Tests: {ok} OK, {fail} FAIL"
wenn fail == 0:
    zeige "ALLE TESTS BESTANDEN!"
sonst:
    zeige f"{fail} TESTS FEHLGESCHLAGEN!"
