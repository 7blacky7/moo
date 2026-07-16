# moo Wetter — HTTP API abfragen
# Nutzt wttr.in (einfaches Text-Format)
# Starten: moo-compiler run beispiele/wetter_api.moo

zeige "=== moo Wetter ==="

setze stadt auf eingabe("Stadt (z.B. Berlin): ")
wenn stadt == "":
    setze stadt auf "Berlin"

zeige "Lade Wetter fuer " + stadt + "..."

# wttr.in im Kurzformat (kein JSON, direkt lesbarer Text)
setze url auf "https://wttr.in/" + stadt + "?format=3"
setze antwort auf http_get(url)
zeige antwort

# Detaillierter
setze url2 auf "https://wttr.in/" + stadt + "?format=%C+%t+%h+%w"
setze detail auf http_get(url2)
zeige "Detail: " + detail
