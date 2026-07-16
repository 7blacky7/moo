# P016-M1 B4b: Compiler ownership regression for typ_von/type_of.
# moo_type_of borrows its argument; generated code must release the owning
# expression temporary after the call. LeakSanitizer is the authoritative gate.

setze fehler auf 0

funktion baue_text_container():
    setze d auf {}
    # The literal is a fresh MooString on every function invocation.
    d["text_metrik"] = "deterministisch-8px"
    setze text_metrik auf d["text_metrik"]
    wenn typ_von(text_metrik) != "Text":
        setze fehler auf fehler + 1
    wenn type_of(text_metrik) != "Text":
        setze fehler auf fehler + 1
    gib_zurück d

setze aktuell auf nichts
setze i auf 0
solange i < 1000:
    setze aktuell auf baue_text_container()
    setze i auf i + 1

wenn fehler == 0 und i == 1000:
    zeige "P016-M1-TYPE-OF-OWNERSHIP-OK"
sonst:
    zeige "P016-M1-TYPE-OF-OWNERSHIP-RED fehler=" + text(fehler)
    wirf "typ_von/type_of ownership regression"
