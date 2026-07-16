# moo Playground — KOMPLETT in moo geschrieben!
# Self-Hosting: moo-Webserver + moo-Eval = Web-IDE fuer moo
#
# Kompilieren: moo-compiler compile playground.moo -o playground
# Starten:     ./playground
# Oeffnen:     http://localhost:8080

# HTML-Datei laden
setze html auf datei_lesen("beispiele/playground/index.html")

# Server starten
setze server auf web_server(8080)
zeige "=== moo Playground ==="
zeige "http://localhost:8080"
zeige "Ctrl+C zum Beenden"
zeige ""

# Request-Loop
solange wahr:
    setze req auf server.web_annehmen()

    wenn req == nichts:
        weiter

    setze pfad auf req["pfad"]
    setze methode auf req["methode"]

    # Playground-HTML ausliefern
    wenn pfad == "/" und methode == "GET":
        web_antworten(req, html, 200)
        weiter

    wenn pfad == "/index.html" und methode == "GET":
        web_antworten(req, html, 200)
        weiter

    # Code ausfuehren — das Herzstueck!
    wenn pfad == "/run" und methode == "POST":
        setze body auf json_parse(req["body"])
        setze code auf body["code"]
        setze output auf ausfuehren(code)
        web_json(req, {"ok": wahr, "output": output})
        weiter

    # 404 fuer alles andere
    web_antworten(req, "Nicht gefunden", 404)
