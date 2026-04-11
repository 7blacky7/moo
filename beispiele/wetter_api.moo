# moo Wetter — HTTP API abfragen + JSON parsen
# Nutzt wttr.in (keine API-Key noetig)
# Starten: moo-compiler run beispiele/wetter_api.moo

zeige "=== moo Wetter ==="
zeige ""

setze stadt auf eingabe("Stadt (z.B. Berlin): ")
wenn stadt == "":
    setze stadt auf "Berlin"

zeige "Lade Wetter fuer " + stadt + "..."

setze url auf "https://wttr.in/" + stadt + "?format=j1"
setze antwort auf http_get(url)

versuche:
    setze daten auf json_parse(antwort)
    setze aktuell auf daten["current_condition"]
    setze wetter auf aktuell[0]
    setze temp auf wetter["temp_C"]
    setze gefuehlt auf wetter["FeelsLikeC"]
    setze feucht auf wetter["humidity"]
    setze wind auf wetter["windspeedKmph"]

    zeige ""
    zeige "Wetter in " + stadt + ":"
    zeige "  Temperatur:   " + temp + " C"
    zeige "  Gefuehlt wie: " + gefuehlt + " C"
    zeige "  Feuchtigkeit: " + feucht + "%"
    zeige "  Wind:         " + wind + " km/h"
fange fehler:
    zeige "Fehler beim Laden der Wetterdaten."

