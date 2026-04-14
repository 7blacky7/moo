daten klasse Token(typ, wert)

zeige "=== mini_interpreter Tests ==="
setze umgebung auf {}
zeige "umgebung erstellt"

funktion eval_zeile(mein_text, umgebung):
    versuche:
        zeige "  in try: " + mein_text
        gib_zurück 42
    fange fehler:
        zeige "  Fehler: " + fehler
        gib_zurück nichts

zeige "1+1 = " + text(eval_zeile("1+1", umgebung))
