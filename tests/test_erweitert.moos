# moo Erweiterte Testsuite — neue Features
# Separate Datei wegen Compiler-Limit bei >80 Tests in einer main()

setze ok auf 0
setze fail auf 0

# === 1. CONST-FOLDING ===
setze cf auf 2 * 3 + 4
wenn cf == 10:
    ok += 1
sonst:
    fail += 1

setze cf2 auf "Hallo" + " " + "Welt"
wenn cf2 == "Hallo Welt":
    ok += 1
sonst:
    fail += 1

# === 2. REGEX ===
setze rx auf regex("[0-9]+")
wenn passt("abc123", rx):
    ok += 1
sonst:
    fail += 1

setze treffer auf finde("test42end", rx)
wenn treffer == "42":
    ok += 1
sonst:
    fail += 1

setze alle auf finde_alle("a1b22c333", rx)
wenn länge(alle) == 3:
    ok += 1
sonst:
    fail += 1

# === 3. INTERFACE ===
schnittstelle Zaehlbar:
    funktion anzahl()

klasse Sammlung implementiert Zaehlbar:
    funktion erstelle(n):
        selbst.n = n
    funktion anzahl():
        gib_zurück selbst.n

setze s auf neu Sammlung(5)
wenn s.anzahl() == 5:
    ok += 1
sonst:
    fail += 1

# === 4. UNSAFE-BLOCK ===
setze unsafe_ok auf falsch
unsicher:
    setze unsafe_ok auf wahr

wenn unsafe_ok:
    ok += 1
sonst:
    fail += 1

# === 5. KERN-BUILTINS ===
setze num_test auf zahl("123")
wenn num_test == 123:
    ok += 1
sonst:
    fail += 1

setze home auf umgebung("HOME")
wenn länge(home) > 0:
    ok += 1
sonst:
    fail += 1

# === 6. TYP-ANNOTATIONEN ===
setze typed_name: Text auf "Anna"
wenn typed_name == "Anna":
    ok += 1
sonst:
    fail += 1

funktion typed_add(a: Zahl, b: Zahl) -> Zahl:
    gib_zurück a + b

wenn typed_add(10, 20) == 30:
    ok += 1
sonst:
    fail += 1

# === 7. LERN-MODUS ===
setze_variable lern auf "test"
wenn lern == "test":
    ok += 1
sonst:
    fail += 1

# === 8. MULTIPLE RETURNS ===
funktion absolut(x):
    wenn x < 0:
        gib_zurück -x
    gib_zurück x

wenn absolut(-7) == 7:
    ok += 1
sonst:
    fail += 1

wenn absolut(3) == 3:
    ok += 1
sonst:
    fail += 1

# === 9. TERNARY ===
setze note auf 85
setze bewertung auf note >= 90 ? "sehr gut" : "anderes"
wenn bewertung == "anderes":
    ok += 1
sonst:
    fail += 1

# === 10. VEKTOR-OPS DIVISION ===
setze v auf [100, 200, 300] / 10
wenn v[0] == 10:
    ok += 1
sonst:
    fail += 1

wenn v[2] == 30:
    ok += 1
sonst:
    fail += 1

# === 11. STRING ZU ZAHL EDGE CASES ===
setze n1 auf zahl("3.14")
wenn n1 > 3:
    ok += 1
sonst:
    fail += 1

# === ERGEBNIS ===
zeige ""
zeige "=" * 50
setze total auf ok + fail
zeige f"Erweitert: {total} Tests: {ok} OK, {fail} FAIL"
wenn fail == 0:
    zeige "ALLE ERWEITERTEN TESTS BESTANDEN!"
sonst:
    zeige f"{fail} ERWEITERTE TESTS FEHLGESCHLAGEN!"
