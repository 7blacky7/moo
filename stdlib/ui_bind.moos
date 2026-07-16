# ============================================================
# stdlib/ui_bind.moo — State-Binding-Registry (P4 Phase 1)
#
# Schlanke stdlib-Bruecke zwischen UI-Widgets und einem
# state-Dict. Keine native Binding-Handles, keine Magie.
#
# Zwei Wege funktionieren parallel:
#   1) Manuelle on_change-Callbacks (bestehend, weiter erlaubt).
#   2) Deklarative ui_bind-Aufrufe + ui_state_setze.
#
# state ist ein einfaches Dict. Die Registry lebt IM state unter
# zwei reservierten Schluesseln:
#   state["__bindings"]  -> Liste von [widget, key, typ]
#   state["__watchers"]  -> Dict key -> Liste von Callbacks
#
# Unterstuetzte Typen (Phase 1):
#   "text"        — ui_eingabe (bidirektional via on_change)
#   "checked"     — ui_checkbox (State→Widget Push)
#   "wert"        — ui_slider (State→Widget Push)
#   "auswahl"     — ui_dropdown (State→Widget Push)
#   "label"       — ui_label (one-way State→Widget)
#   "textbereich" — ui_textbereich (Setter/Getter stabil)
#
# Runtime-Limit-Beachtung:
#   - Bracket-Syntax g["k"] statt Dot (MooValue pointer-tagged).
#   - Keine Keyword-Args am Call-Site.
#   - Keine Multi-Line-Lambdas (daher top-level Funktionen).
#   - on_change-Callbacks sind 0-arg (moo_func_call_0) — der
#     Auto-Sync fuer Eingabe-Felder nutzt deshalb einen globalen
#     Sync-Dispatcher, der alle registrierten States abklappert.
# ============================================================


# ---------- Modul-Globale Sync-Queue -----------------------
# Alle States, die Text-Bindings mit Auto-Sync registriert haben.
setze __ui_bind_states auf []


# Wird als 0-arg-Callback an ui_eingabe_on_change gehaengt.
# Geht ueber alle registrierten States, liest den aktuellen
# Widget-Text und schreibt ihn (falls geaendert) per
# ui_state_setze zurueck. Damit werden Watchers + Cross-Widget-
# Updates konsistent ausgeloest.
funktion __ui_bind_sync_alle():
    für state in __ui_bind_states:
        setze bindings auf state["__bindings"]
        für b in bindings:
            wenn b[2] == "text":
                setze widget auf b[0]
                setze key auf b[1]
                setze frisch auf ui_eingabe_text(widget)
                setze alt auf state[key]
                wenn alt != frisch:
                    ui_state_setze(state, key, frisch)
    gib_zurück wahr


# ---------- Interne Helfer --------------------------------

# Initialisiert Registry-Felder auf dem state, falls noch nicht
# gesetzt, und registriert state in der modul-globalen Liste
# __ui_bind_states (einmalig).
funktion __ui_bind_init(state):
    wenn nicht state.enthält("__bindings"):
        state["__bindings"] = []
        state["__watchers"] = {}
        __ui_bind_states.hinzufügen(state)
    gib_zurück wahr


funktion __ui_bind_push_zu_widget(widget, typ, wert):
    wenn typ == "text":
        ui_eingabe_setze(widget, wert)
    sonst wenn typ == "checked":
        ui_checkbox_setze(widget, wert)
    sonst wenn typ == "wert":
        ui_slider_setze(widget, wert)
    sonst wenn typ == "auswahl":
        ui_dropdown_auswahl_setze(widget, wert)
    sonst wenn typ == "label":
        ui_label_setze(widget, wert)
    sonst wenn typ == "textbereich":
        ui_textbereich_setze(widget, wert)
    gib_zurück wahr


funktion __ui_bind_pull_von_widget(widget, typ):
    wenn typ == "text":
        gib_zurück ui_eingabe_text(widget)
    sonst wenn typ == "checked":
        gib_zurück ui_checkbox_wert(widget)
    sonst wenn typ == "wert":
        gib_zurück ui_slider_wert(widget)
    sonst wenn typ == "auswahl":
        gib_zurück ui_dropdown_auswahl(widget)
    sonst wenn typ == "textbereich":
        gib_zurück ui_textbereich_text(widget)
    gib_zurück nichts


funktion __ui_bind_eintrag_hinzu(state, widget, key, typ):
    __ui_bind_init(state)
    state["__bindings"].hinzufügen([widget, key, typ])
    gib_zurück wahr


# ---------- Oeffentliche Binder ---------------------------

# Text-Binding (bidirektional). Initialwert State→Widget, danach
# feuert jede Widget-Aenderung __ui_bind_sync_alle, das state
# aktualisiert und Watchers/weitere bound Widgets triggert.
funktion ui_bind_text(widget, state, key):
    __ui_bind_eintrag_hinzu(state, widget, key, "text")
    wenn state.enthält(key):
        ui_eingabe_setze(widget, state[key])
    sonst:
        state[key] = ui_eingabe_text(widget)
    ui_eingabe_on_change(widget, __ui_bind_sync_alle)
    gib_zurück wahr


# Checkbox-Binding. State→Widget automatisch; Widget→State passiert
# entweder via ui_state_sync_von_widgets(state) oder indem der User
# im Checkbox-Callback ui_state_setze(state, key, ui_checkbox_wert(w))
# ruft.
funktion ui_bind_checked(widget, state, key):
    __ui_bind_eintrag_hinzu(state, widget, key, "checked")
    wenn state.enthält(key):
        ui_checkbox_setze(widget, state[key])
    sonst:
        state[key] = ui_checkbox_wert(widget)
    gib_zurück wahr


# Slider-Binding. Analog ui_bind_checked.
funktion ui_bind_wert(widget, state, key):
    __ui_bind_eintrag_hinzu(state, widget, key, "wert")
    wenn state.enthält(key):
        ui_slider_setze(widget, state[key])
    sonst:
        state[key] = ui_slider_wert(widget)
    gib_zurück wahr


# Dropdown-Auswahl-Binding (Index).
funktion ui_bind_auswahl(widget, state, key):
    __ui_bind_eintrag_hinzu(state, widget, key, "auswahl")
    wenn state.enthält(key):
        ui_dropdown_auswahl_setze(widget, state[key])
    sonst:
        state[key] = ui_dropdown_auswahl(widget)
    gib_zurück wahr


# Label-Binding: nur one-way (State→Widget).
funktion ui_bind_label(widget, state, key):
    __ui_bind_eintrag_hinzu(state, widget, key, "label")
    wenn state.enthält(key):
        ui_label_setze(widget, state[key])
    gib_zurück wahr


# Textbereich-Binding. Setter/Getter stabil; Widget→State via
# manuelles ui_state_setze (Textbereich hat kein on_change).
funktion ui_bind_textbereich(widget, state, key):
    __ui_bind_eintrag_hinzu(state, widget, key, "textbereich")
    wenn state.enthält(key):
        ui_textbereich_setze(widget, state[key])
    sonst:
        state[key] = ui_textbereich_text(widget)
    gib_zurück wahr


# Generisches ui_bind: Der User gibt den Typ explizit an, weil
# Widget-Handles keine introspizierbare Typ-Info tragen.
# typ ∈ {"text","checked","wert","auswahl","label","textbereich"}
funktion ui_bind(widget, state, key, typ):
    wenn typ == "text":
        gib_zurück ui_bind_text(widget, state, key)
    sonst wenn typ == "checked":
        gib_zurück ui_bind_checked(widget, state, key)
    sonst wenn typ == "wert":
        gib_zurück ui_bind_wert(widget, state, key)
    sonst wenn typ == "auswahl":
        gib_zurück ui_bind_auswahl(widget, state, key)
    sonst wenn typ == "label":
        gib_zurück ui_bind_label(widget, state, key)
    sonst wenn typ == "textbereich":
        gib_zurück ui_bind_textbereich(widget, state, key)
    gib_zurück falsch


# ---------- State-API ------------------------------------

funktion ui_state_hole(state, key):
    wenn state.enthält(key):
        gib_zurück state[key]
    gib_zurück nichts


# Setzt state[key] = wert und triggert:
#   1) alle gebundenen Widgets fuer dieses key (State→Widget)
#   2) alle Watcher-Callbacks fuer dieses key, Signatur (state, wert)
funktion ui_state_setze(state, key, wert):
    __ui_bind_init(state)
    state[key] = wert
    für b in state["__bindings"]:
        wenn b[1] == key:
            __ui_bind_push_zu_widget(b[0], b[2], wert)
    wenn state["__watchers"].enthält(key):
        für cb in state["__watchers"][key]:
            cb(state, wert)
    gib_zurück wahr


# Registriert einen Watcher. callback-Signatur: (state, wert).
funktion ui_state_on_change(state, key, callback):
    __ui_bind_init(state)
    wenn nicht state["__watchers"].enthält(key):
        state["__watchers"][key] = []
    state["__watchers"][key].hinzufügen(callback)
    gib_zurück wahr


# Zieht aktuelle Werte aus ALLEN gebundenen Widgets in den State
# nach (z.B. vor Speichern). Praktisch fuer Widgets ohne
# on_change-Auto-Sync (Checkbox/Slider/Dropdown/Textbereich).
funktion ui_state_sync_von_widgets(state):
    __ui_bind_init(state)
    für b in state["__bindings"]:
        setze wert auf __ui_bind_pull_von_widget(b[0], b[2])
        wenn wert != nichts:
            state[b[1]] = wert
    gib_zurück wahr


# Druckt den aktuellen State (ohne __bindings/__watchers) zu stdout.
funktion ui_state_zeige(state):
    zeige "[ui_state]"
    für k in state.schlüssel():
        wenn k != "__bindings":
            wenn k != "__watchers":
                zeige "  " + k + " = " + state[k]
    gib_zurück wahr
