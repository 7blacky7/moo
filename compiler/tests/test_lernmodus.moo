# Lern-Modus Test — ausfuehrliche Keywords fuer Anfaenger

setze_variable name auf "Anna"
zeige_auf_bildschirm name

setze_variable alter auf 25
wenn_bedingung alter >= 18:
    zeige_auf_bildschirm "Erwachsen!"
sonst_alternative:
    zeige_auf_bildschirm "Noch jung"

funktion_definiere begruessung(wer):
    gib_wert_zurück "Hallo " + wer

zeige_auf_bildschirm begruessung("Welt")

setze_variable zahlen auf [1, 2, 3]
fuer_jedes z in zahlen:
    zeige_auf_bildschirm z
