# Test: Match/Prüfe — Wert-Vergleich + Guards + Expression
setze farbe auf "rot"
prüfe farbe:
    fall "rot":
        zeige "Rot!"
    fall "blau":
        zeige "Blau!"
    standard:
        zeige "Andere"

setze x auf 42
prüfe x:
    fall 42:
        zeige "Die Antwort"
    standard:
        zeige "Falsch"

# Match als Expression (When)
setze alter auf 25
setze kat auf prüfe alter:
    fall n wenn n >= 65: "senior"
    fall n wenn n >= 18: "erwachsen"
    fall n wenn n >= 13: "teenager"
    standard: "kind"
zeige kat

setze alter2 auf 8
setze kat2 auf prüfe alter2:
    fall n wenn n >= 18: "erwachsen"
    standard: "kind"
zeige kat2

# String-Match (der kritische Test!)
setze sprache auf "deutsch"
prüfe sprache:
    fall "deutsch":
        zeige "Hallo Welt"
    fall "english":
        zeige "Hello World"
    standard:
        zeige "???"
