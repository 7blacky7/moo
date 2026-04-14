# Regressions-Test fuer P2b — Server-Seite:
#   req["headers"]  (Lese-Dict)
#   req.antworten_mit_headers(body, status, headers_dict)
#   req.json_antworten_mit_headers(daten, status, headers_dict)
#
# Ziel: Signatur/Codegen-Drift verhindern. Der Test startet KEINE echte
# HTTP-Verbindung (waere CI-fragil), sondern exerciert alle neuen
# Dispatch-Pfade in einem nie-laufenden Branch, sodass der Compiler sie
# tatsaechlich emittieren MUSS. Fehlt ein Alias in codegen.rs, bricht
# die Kompilierung sofort.

funktion exercise_new_api(req):
    setze cookie auf req["headers"]["cookie"]
    setze auth auf req["headers"]["authorization"]
    zeige cookie
    zeige auth

    setze headers auf {
        "Set-Cookie": "session=abc; HttpOnly",
        "Cache-Control": "no-store"
    }
    req.antworten_mit_headers("<h1>hi</h1>", 200, headers)
    req.respond_with_headers("<h1>en</h1>", 200, headers)
    req.json_antworten_mit_headers({"ok": wahr}, 200, headers)
    req.json_respond_with_headers({"ok": wahr}, 200, headers)

# Run-Guard: stellt sicher dass der Body nie ausgefuehrt wird, aber alle
# Dispatch-Pfade compiliert werden. `falsch` ist Compile-Zeit-konstant;
# die Funktion wird emittiert und linkt gegen die Runtime-Symbole.
wenn falsch:
    setze server auf web_server(0)
    setze req auf server.web_annehmen()
    wenn req != nichts:
        exercise_new_api(req)
    server.schliessen()

zeige "HTTP headers OK"
