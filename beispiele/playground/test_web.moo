zeige "Starte..."
setze server auf web_server(8080)
zeige "Server laeuft auf 8080"
solange wahr:
    setze req auf server.web_annehmen()
    zeige "Request!"
    server.antworten(req, "Hallo von moo!")
