# moo REST API — Todo-Server
# Kompilieren: moo-compiler compile http_api.moo -o api_server
# Testen:      curl http://localhost:3000/api/todos

setze todos auf []
setze naechste_id auf 1

funktion fibonacci(n):
    wenn n <= 0:
        gib_zurück 0
    wenn n == 1:
        gib_zurück 1
    setze a auf 0
    setze b auf 1
    setze ende auf n + 1
    für i in 2..ende:
        setze temp auf b
        setze b auf a + b
        setze a auf temp
    gib_zurück b

setze server auf web_server(3000)
zeige "=== moo REST API auf Port 3000 ==="
zeige "Endpoints: /api/todos (GET/POST), /api/status, /api/fib"

solange wahr:
    setze req auf server.web_annehmen()
    wenn req == nichts:
        weiter

    setze pfad auf req["pfad"]
    setze methode auf req["methode"]
    setze body auf req["body"]

    wenn pfad == "/api/todos" und methode == "GET":
        web_json(req, {"todos": todos, "anzahl": länge(todos)})
        weiter

    wenn pfad == "/api/todos" und methode == "POST":
        web_json(req, {"ok": wahr, "empfangen": body})
        weiter

    wenn pfad == "/api/status":
        web_json(req, {"sprache": "moo", "todos": länge(todos)})
        weiter

    wenn pfad == "/api/fib":
        setze ergebnis auf fibonacci(10)
        web_json(req, {"fibonacci": ergebnis, "n": 10})
        weiter

    wenn pfad == "/":
        web_antworten(req, "<h1>moo REST API</h1><p>Endpoints: /api/todos /api/status /api/fib</p>", 200)
        weiter

    web_antworten(req, "Nicht gefunden", 404)
