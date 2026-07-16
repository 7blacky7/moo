# moo Playground Server — geschrieben in moo!
# Nutzt den moo-Webserver um das Playground-HTML auszuliefern
# und moo-Code zu transpilieren + auszufuehren.
#
# Starten: moo-compiler run beispiele/playground/playground_server.moo

# HTML-Datei laden
setze html auf datei_lesen("beispiele/playground/index.html")

# Server starten
setze server auf web_server(8080)
zeige "moo Playground: http://localhost:8080"

# Request-Loop
solange wahr:
    setze req auf server.web_annehmen()

    wenn req == nichts:
        weiter

    setze pfad auf req["pfad"]
    setze methode auf req["methode"]

    # Playground-HTML ausliefern
    wenn pfad == "/" und methode == "GET":
        server.antworten(html, 200)
        weiter

    wenn pfad == "/index.html" und methode == "GET":
        server.antworten(html, 200)
        weiter

    # 404 fuer alles andere
    server.antworten("Nicht gefunden", 404)
