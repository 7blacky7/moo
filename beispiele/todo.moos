# Todo-Liste — ein einfaches Programm in moo
# Zeigt: Klassen, Listen, Schleifen, Bedingungen, String-Ops

klasse Todo:
    funktion erstelle(titel):
        selbst.titel = titel
        selbst.erledigt = falsch

    funktion abschliessen():
        selbst.erledigt = wahr
        gib_zurück selbst.titel + " erledigt!"

    funktion anzeigen():
        wenn selbst.erledigt:
            gib_zurück "[x] " + selbst.titel
        sonst:
            gib_zurück "[ ] " + selbst.titel

# Todos erstellen
setze aufgaben auf []
aufgaben.append(neu Todo("moo lernen"))
aufgaben.append(neu Todo("Compiler bauen"))
aufgaben.append(neu Todo("Tests schreiben"))
aufgaben.append(neu Todo("Dokumentation"))

# Erste zwei abschliessen
zeige aufgaben[0].abschliessen()
zeige aufgaben[1].abschliessen()

# Alle anzeigen
zeige ""
zeige "=== Meine Todo-Liste ==="
zeige "-" * 25
für aufgabe in aufgaben:
    zeige aufgabe.anzeigen()

# Statistik
setze erledigt auf 0
für aufgabe in aufgaben:
    wenn aufgabe.erledigt:
        erledigt += 1

zeige ""
zeige text(erledigt) + " von " + text(länge(aufgaben)) + " erledigt"
