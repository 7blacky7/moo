# P016-M1 B1: Headless fail-first lifecycle contract for pure ui_moo.
# Opens no window and does not touch GTK, Win32, Cocoa, desktop input, or capture.

importiere ui
importiere ui_moo

setze s auf {}
s["fehler"] = 0
s["remove_calls"] = 0
s["switch_calls"] = 0
s["destroy_calls"] = 0

funktion pruef(name, ok):
    wenn ok:
        zeige "PASS " + name
    sonst:
        zeige "FAIL " + name
        s["fehler"] = s["fehler"] + 1
    gib_zurück ok

funktion entferne_selbst(w):
    s["remove_calls"] = s["remove_calls"] + 1
    setze k auf s["remove_k"]
    k["wurzel"]["kinder"] = []
    gib_zurück wahr

funktion wechsle_backend(w):
    s["switch_calls"] = s["switch_calls"] + 1
    setze k auf s["switch_k"]
    setze ersatz auf s["switch_ersatz"]
    k["backend_vertrag"] = ersatz["backend_vertrag"]
    k["backend_zustand"] = ersatz["backend_zustand"]
    gib_zurück wahr

funktion zerstoere_im_callback(w):
    s["destroy_calls"] = s["destroy_calls"] + 1
    s["destroy_k"]["wurzel"] = nichts
    gib_zurück wahr

# Removal during dispatch: the callback removes its own last owning tree edge.
setze remove_k auf uim_mock_wurzel(160, 90)
s["remove_k"] = remove_k
setze alias auf uim_hinzu(remove_k, uim_knopf("REMOVE", 10, 10, 90, 28, entferne_selbst))
alias["id"] = "remove-self"
remove_k["fokus"] = alias
uim_mock_maus(remove_k, 20, 20, wahr)
uim_mock_maus(remove_k, 20, 20, falsch)
pruef("remove-callback-once", s["remove_calls"] == 1)
pruef("remove-find-none", uim_finde(remove_k, "remove-self") == nichts)
pruef("remove-alias-readable", alias["id"] == "remove-self")
pruef("remove-alias-not-interactive", _uim_ist_interaktiv(remove_k, alias) == falsch)
uim_mock_taste(remove_k, "Enter", wahr, 0)
pruef("remove-stale-focus-cleared", remove_k["fokus"] == nichts)
pruef("remove-draw-1", uim_mock_zeichne(remove_k))
pruef("remove-draw-2", uim_mock_zeichne(remove_k))

# A coherent backend switch from inside dispatch must affect only following draws.
setze switch_k auf uim_mock_wurzel(160, 90)
setze switch_ersatz auf uim_mock_wurzel(160, 90)
s["switch_k"] = switch_k
s["switch_ersatz"] = switch_ersatz
uim_hinzu(switch_k, uim_knopf("SWITCH", 10, 10, 90, 28, wechsle_backend))
uim_hinzu(switch_k, uim_label("after", 10, 50, 80, 20))
uim_mock_maus(switch_k, 20, 20, wahr)
uim_mock_maus(switch_k, 20, 20, falsch)
setze switch_draw_1 auf uim_mock_zeichne(switch_k)
setze switch_draw_2 auf uim_mock_zeichne(switch_k)
setze switch_cmds auf uim_mock_befehle(switch_k)
pruef("backend-switch-callback-once", s["switch_calls"] == 1)
pruef("backend-switch-follow-draw", switch_draw_1 und switch_draw_2)
pruef("backend-switch-commandbuffer", länge(switch_cmds) > 0)

# Destroy during dispatch must make every following operation fail closed.
setze destroy_k auf uim_mock_wurzel(160, 90)
s["destroy_k"] = destroy_k
uim_hinzu(destroy_k, uim_knopf("DESTROY", 10, 10, 90, 28, zerstoere_im_callback))
uim_mock_maus(destroy_k, 20, 20, wahr)
uim_mock_maus(destroy_k, 20, 20, falsch)
pruef("destroy-callback-once", s["destroy_calls"] == 1)
pruef("destroy-root-cleared", destroy_k["wurzel"] == nichts)
pruef("destroy-follow-draw-fail-closed", uim_mock_zeichne(destroy_k) == falsch)

# Losing the backend contract must not be reported as a successful draw.
setze drop_k auf uim_mock_wurzel(160, 90)
uim_hinzu(drop_k, uim_label("DROP", 10, 10, 80, 20))
drop_k["backend_vertrag"] = nichts
pruef("backend-drop-draw-fail-closed", uim_mock_zeichne(drop_k) == falsch)

# Repeated fresh contexts exercise create/add/draw/release without platform UI.
setze loop_ok auf wahr
setze i auf 0
solange i < 1000:
    setze lk auf uim_mock_wurzel(96, 64)
    uim_hinzu(lk, uim_label("L", 2, 2, 20, 12))
    wenn uim_mock_zeichne(lk) == falsch:
        setze loop_ok auf falsch
    wenn uim_mock_zeichne(lk) == falsch:
        setze loop_ok auf falsch
    wenn länge(uim_mock_befehle(lk)) == 0:
        setze loop_ok auf falsch
    setze i auf i + 1
pruef("lifecycle-1000", loop_ok und i == 1000)

wenn s["fehler"] == 0:
    zeige "P016-M1-LIFECYCLE-OK"
sonst:
    zeige "P016-M1-LIFECYCLE-RED fehler=" + text(s["fehler"])
    wirf "P016-M1 Lifecycle-Vertrag verletzt"
