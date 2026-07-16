setze server auf web_server(8080)
zeige "Body-Test Server auf 8080"
solange wahr:
    setze req auf server.web_annehmen()
    wenn req == nichts:
        weiter
    setze body auf req["body"]
    zeige "Body erhalten: " + body
    # Versuche JSON zu parsen
    setze daten auf json_parse(body)
    setze code auf daten["code"]
    zeige "Code: " + code
    setze output auf ausfuehren(code)
    web_json(req, {"ok": wahr, "output": output})
