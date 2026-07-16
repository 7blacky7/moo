# Dict als Parser-State mit pos-Update
funktion parser_advance(p):
    setze tok auf p["tokens"][p["pos"]]
    p["pos"] = p["pos"] + 1
    gib_zurück tok

setze p auf {"tokens": ["a", "b", "c"], "pos": 0}
zeige parser_advance(p)
zeige parser_advance(p)
zeige p["pos"]
