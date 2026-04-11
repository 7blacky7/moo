# moo Taschenrechner — interaktiver CLI-Rechner
# Starten: moo-compiler run beispiele/taschenrechner.moo

zeige "=== moo Taschenrechner ==="
zeige "Gib einen Ausdruck ein (oder 'ende' zum Beenden)"
zeige "Beispiele: 2 + 3, 10 * 5, 100 / 7"
zeige ""

solange wahr:
    setze eingabe_text auf eingabe("rechne> ")

    wenn eingabe_text == "ende":
        zeige "Auf Wiedersehen!"
        stopp

    wenn eingabe_text == "hilfe":
        zeige "  Operationen: + - * / % **"
        zeige "  Funktionen:  wurzel(x), abs(x), runden(x)"
        zeige "  'ende' zum Beenden"
        weiter

    # Ausdruck auswerten mit ausfuehren()
    setze code auf "zeige " + eingabe_text
    setze ergebnis auf ausfuehren(code)

    wenn ergebnis == "":
        zeige "  Fehler: Ungueltiger Ausdruck"
    sonst:
        zeige "  = " + ergebnis
