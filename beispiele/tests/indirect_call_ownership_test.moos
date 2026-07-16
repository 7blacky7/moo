# P016-M1 B4b: Compiler ownership regression for dynamic function calls.
# moo_func_call_N borrows the function value. Generated indirect-call code must
# release its owning fn_val temporary after the helper returns.

setze fehler auf 0

funktion callback_0():
    gib_zurück 42

funktion callback_2(kontext, args):
    wenn kontext.enthält("operationen") und länge(args) == 1:
        gib_zurück args[0] + 1
    gib_zurück 0 - 1

funktion backend_neu():
    setze operationen auf {}
    operationen["null"] = callback_0
    operationen["zwei"] = callback_2
    setze backend auf {}
    backend["operationen"] = operationen
    gib_zurück backend

setze aktuell auf nichts
setze i auf 0
solange i < 1000:
    setze aktuell auf backend_neu()
    setze operationen auf aktuell["operationen"]
    setze fn0 auf operationen["null"]
    setze fn2 auf operationen["zwei"]
    wenn fn0() != 42:
        setze fehler auf fehler + 1
    wenn fn2(aktuell, [i]) != i + 1:
        setze fehler auf fehler + 1
    setze i auf i + 1

wenn fehler == 0 und i == 1000:
    zeige "P016-M1-INDIRECT-CALL-OWNERSHIP-OK"
sonst:
    zeige "P016-M1-INDIRECT-CALL-OWNERSHIP-RED fehler=" + text(fehler)
    wirf "indirect call ownership regression"
