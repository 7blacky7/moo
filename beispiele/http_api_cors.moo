# moo REST API mit CORS + Cookie-Sessions (neue Header-API).
# Kompilieren: moo-compiler compile http_api_cors.moo -o api_cors_server
# Testen:
#   curl -i http://localhost:3001/api/status
#   curl -i -X OPTIONS http://localhost:3001/api/status -H "Origin: https://example.com"
#   curl -i http://localhost:3001/api/login
#   curl -i -H "Cookie: session=abc" http://localhost:3001/api/me
#
# Zeigt: req["headers"]-Lese-API, req.antworten_mit_headers / req.json_antworten_mit_headers,
# CORS-Header, Set-Cookie, Token-Echo aus Cookie.

setze CORS auf {"Access-Control-Allow-Origin": "*", "Access-Control-Allow-Methods": "GET, POST, OPTIONS", "Access-Control-Allow-Headers": "Content-Type, Authorization", "Cache-Control": "no-store"}

funktion extrahiere_session(cookie_header):
    wenn cookie_header == nichts:
        gib_zurück ""
    setze teile auf cookie_header.teilen(";")
    für t in teile:
        setze t2 auf t.trimmen()
        wenn t2.teilstring(0, 8) == "session=":
            gib_zurück t2.teilstring(8, länge(t2))
    gib_zurück ""

setze server auf web_server(3001)
zeige "=== moo REST API (CORS + Cookies) auf Port 3001 ==="

solange wahr:
    setze req auf server.web_annehmen()
    wenn req == nichts:
        weiter
    setze pfad auf req["pfad"]
    setze methode auf req["methode"]
    setze headers auf req["headers"]

    wenn methode == "OPTIONS":
        req.antworten_mit_headers("", 204, CORS)
        weiter

    wenn pfad == "/api/status":
        req.json_antworten_mit_headers({"sprache": "moo", "cors": wahr, "user_agent": headers["user-agent"]}, 200, CORS)
        weiter

    wenn pfad == "/api/login":
        setze lh auf {"Access-Control-Allow-Origin": "*", "Set-Cookie": "session=abc123; Path=/; HttpOnly", "Cache-Control": "no-store"}
        req.antworten_mit_headers("eingeloggt\n", 200, lh)
        weiter

    wenn pfad == "/api/me":
        setze session auf extrahiere_session(headers["cookie"])
        wenn session == "":
            req.json_antworten_mit_headers({"fehler": "nicht eingeloggt"}, 401, CORS)
            weiter
        req.json_antworten_mit_headers({"session": session, "name": "moo-user"}, 200, CORS)
        weiter

    wenn pfad == "/":
        req.antworten_mit_headers("<h1>moo REST API mit CORS + Cookies</h1>", 200, CORS)
        weiter

    req.antworten_mit_headers("Nicht gefunden", 404, CORS)
