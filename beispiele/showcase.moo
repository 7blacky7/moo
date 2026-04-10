# ============================================================
# moo Showcase — Tour de moo
# Demonstriert alle Features der Sprache (Deutsch & Englisch)
# ============================================================

# --- 1. Variablen & Konstanten ---
setze name auf "Welt"
konstante version auf 3.0
zeige f"Willkommen bei moo v{version}, {name}!"

# --- 2. Zahlen & Mathematik ---
setze x auf 42
setze y auf 3.14
zeige f"x = {x}, y = {y}, x + y = {x + y}"
zeige f"x ** 2 = {x ** 2}, wurzel(x) = {wurzel(x)}"
zeige f"min = {min(x, y)}, max = {max(x, y)}"

# --- 3. Strings & f-Strings ---
setze gruss auf "Hallo moo!"
zeige f"Grossbuchstaben: {gruss.gross()}"
zeige f"Kleinbuchstaben: {gruss.klein()}"
zeige f"Laenge: {länge(gruss)}"

# --- 4. Listen ---
setze zahlen auf [1, 2, 3, 4, 5]
zeige f"Liste: {zahlen}"
zahlen.hinzufügen(6)
zeige f"Nach append: {zahlen}"
zeige f"Laenge: {zahlen.länge()}"
zeige f"Enthaelt 3: {zahlen.enthält(3)}"

# --- 5. Spread-Operator ---
setze a auf [1, 2, 3]
setze b auf [4, 5, 6]
setze zusammen auf [...a, ...b]
zeige f"Spread Listen: {zusammen}"

setze d1 auf {"name": "moo", "typ": "sprache"}
setze d2 auf {"version": "3.0", "status": "aktiv"}
setze merged auf {...d1, ...d2}
zeige f"Spread Dict: {merged}"

# --- 6. List Comprehensions ---
setze quadrate auf [x * x für x in 1..6]
zeige f"Quadrate: {quadrate}"
setze gerade auf [x für x in 1..11 wenn x % 2 == 0]
zeige f"Gerade Zahlen: {gerade}"

# --- 7. Pipe-Operator ---
funktion verdopple(n):
    gib_zurück n * 2

funktion plus_eins(n):
    gib_zurück n + 1

setze ergebnis auf 5 |> verdopple() |> plus_eins()
zeige f"5 |> verdopple |> plus_eins = {ergebnis}"

# --- 8. Funktionen & Default-Parameter ---
funktion begruessung(wer, prefix="Hallo"):
    gib_zurück f"{prefix}, {wer}!"

zeige begruessung("moo")
zeige begruessung("moo", "Servus")

# --- 9. Closures / Lambdas ---
setze mal_drei auf (x) => x * 3
zeige f"Lambda 4 * 3 = {mal_drei(4)}"

# --- 10. Map & Filter ---
setze nums auf [1, 2, 3, 4, 5]
setze doppelt auf nums.map((x) => x * 2)
zeige f"Map *2: {doppelt}"
setze gross auf nums.filter((x) => x > 3)
zeige f"Filter >3: {gross}"

# --- 11. Dicts ---
setze person auf {"name": "Max", "alter": 30, "stadt": "Berlin"}
zeige f"Person: {person}"
zeige f"Name: {person["name"]}"
zeige f"Hat 'alter': {person.hat("alter")}"
zeige f"Keys: {person.schlüssel()}"

# --- 12. Klassen & Vererbung ---
klasse Tier:
    funktion erstelle(name, laut):
        selbst.name = name
        selbst.laut = laut

    funktion sprich():
        gib_zurück f"{selbst.name} sagt {selbst.laut}!"

klasse Hund(Tier):
    funktion erstelle(name):
        selbst.name = name
        selbst.laut = "Wuff"

    funktion apportiere():
        gib_zurück f"{selbst.name} apportiert den Ball!"

setze rex auf neu Hund("Rex")
zeige rex.sprich()
zeige rex.apportiere()

# --- 13. Try/Catch ---
versuche:
    wirf "Testfehler!"
fange fehler:
    zeige f"Fehler gefangen: {fehler}"

# --- 14. Pattern Matching mit Guards ---
funktion beschreibe(wert):
    prüfe wert:
        fall 0:
            zeige "Null"
        fall n wenn n > 100:
            zeige f"{n} ist sehr gross"
        fall n wenn n > 0:
            zeige f"{n} ist positiv"
        fall _:
            zeige f"{wert} ist negativ oder unbekannt"

beschreibe(0)
beschreibe(42)
beschreibe(999)
beschreibe(-5)

# --- 15. Optional Chaining & Nullish Coalescing ---
setze config auf {"db": {"host": "localhost"}}
setze host auf config["db"]
zeige f"DB Host: {host}"

setze missing auf nichts ?? "Standardwert"
zeige f"Nullish Coalescing: {missing}"

# --- 16. Schleifen ---
zeige "For-Schleife:"
für i in 1..4:
    zeige f"  Iteration {i}"

zeige "While-Schleife:"
setze counter auf 3
solange counter > 0:
    zeige f"  Countdown: {counter}"
    counter -= 1

# --- 17. Range & Iteration ---
setze bereich auf 1..6
zeige f"Range: {bereich}"

# --- 18. String-Methoden ---
setze satz auf "  Hallo, moo Welt!  "
zeige f"Trim: '{satz.trimmen()}'"
setze teile auf "a,b,c".teilen(",")
zeige f"Split: {teile}"
zeige f"Join: {teile.verbinden(" - ")}"
zeige f"Replace: {"Hallo Welt".ersetzen("Welt", "moo")}"

# --- 19. Typ-Pruefung ---
zeige f"Typ von 42: {typ_von(42)}"
zeige f"Typ von 'hi': {typ_von("hi")}"
zeige f"Typ von [1]: {typ_von([1])}"
zeige f"Typ von wahr: {typ_von(wahr)}"

# --- 20. JSON ---
setze json_str auf json_text({"sprache": "moo", "version": 3})
zeige f"JSON: {json_str}"
setze parsed auf json_lesen(json_str)
zeige f"Parsed: {parsed}"

# --- 21. Kryptografie ---
setze hash auf sha256("moo ist toll")
zeige f"SHA256: {hash}"
setze encoded auf base64_kodieren("Hallo moo!")
zeige f"Base64: {encoded}"
zeige f"Decoded: {base64_dekodieren(encoded)}"

# --- Fertig! ---
zeige ""
zeige "============================================"
zeige " moo — die universelle Programmiersprache"
zeige " Zweisprachig, nativ kompiliert, vielseitig"
zeige "============================================"
