# ============================================================
# beispiele/tests/ui_moo_visuell_test.moo — UIMOO-4 visuelle Tests
#
# Snapshot-Sequenz über den Plan-004-Apparat (ui_test_sequenz):
#   frame_001  initial, Theme dunkel (Knopf, Checkbox aus,
#              Slider 50%, Fortschritt 30%)
#   klick Checkbox -> frame_002 (Haken + Fokusring)
#   klick Slider 75% -> frame_003 (Knob rechts, Fokusring)
#   Theme hell -> frame_004
#
# Verifikation: alle 4 Frames als JSON+PNG vorhanden, JSON parsebar,
# Leinwand im Widget-Tree, UND die ui_moo-Zustände stimmen (Checkbox
# an, Slider ~75). Harte Pixel-Diffs (test_frame_diff) folgen mit dem
# Frame-Backend in UIMOO-6 — die PNGs hier sind das Review-Material.
# Marker: === UIMOO-VISUELL OK ===
# ============================================================

importiere ui
importiere ui_moo

setze g auf {}

funktion nix_klick(w):
    gib_zurück wahr

funktion nix_wechsel(w, wert):
    gib_zurück wahr

g["hFenster"] = ui_fenster("UIMOO-Visuell", 600, 460, 0, nichts)
g["k"] = uim_wurzel(g["hFenster"], 10, 10, 580, 400)

uim_hinzu(g["k"], uim_label("ui_moo Widgets — visueller Snapshot", 20, 16, 300, 20))
g["kn"]  = uim_hinzu(g["k"], uim_knopf("Speichern", 20, 50, 130, 32, nix_klick))
g["chk"] = uim_hinzu(g["k"], uim_checkbox("Benachrichtigungen", 20, 100, 220, 24, falsch, nix_wechsel))
g["sl"]  = uim_hinzu(g["k"], uim_slider(20, 145, 220, 24, 0, 100, 50, nix_wechsel))
g["fs"]  = uim_hinzu(g["k"], uim_fortschritt(20, 190, 220, 16))
uim_fortschritt_setze(g["k"], g["fs"], 0.3)
g["sc"] = uim_hinzu(g["k"], uim_scroll(300, 50, 220, 140, 360))
uim_hinzu(g["sc"], uim_label("Scroll-Anfang", 12, 12, 150, 20))
uim_hinzu(g["sc"], uim_label("Scroll-Ende", 12, 300, 150, 20))

ui_widget_id_setze(g["hFenster"], "root")

wenn datei_existiert("beispiele/snapshots/ui_moo") != wahr:
    datei_mkdir("beispiele/snapshots/ui_moo")

setze bericht auf {}
setze bericht["reports"] auf []
setze bericht["fertig"] auf falsch
g["sequenz_laeuft"] = falsch
g["timer_id"] = nichts

funktion sha256_lowerhex_ok(wert):
    wenn typ_von(wert) != "Text" oder länge(wert) != 64:
        gib_zurück falsch
    setze erlaubt auf "0123456789abcdef"
    setze j auf 0
    solange j < 64:
        wenn erlaubt.str_contains(wert[j]) == falsch:
            gib_zurück falsch
        setze j auf j + 1
    gib_zurück wahr

funktion visu_frame(name, state):
    setze aktion auf {}
    aktion["action"] = "frame"
    aktion["pfad"] = "beispiele/snapshots/ui_moo/" + name
    aktion["visual_state"] = state
    setze frames_erg auf ui_test_sequenz(g["hFenster"], "beispiele/snapshots/ui_moo", [aktion])
    für frame_erg in frames_erg:
        bericht["reports"].hinzufügen(frame_erg)

funktion lauf_sequenz():
    # Der native Timer ist periodisch. Vor dem ersten Pump als Ein-Schuss
    # entfernen und verschachtelten Wiedereintritt fail-closed blockieren.
    wenn g["sequenz_laeuft"] == wahr:
        gib_zurück falsch
    g["sequenz_laeuft"] = wahr
    wenn g["timer_id"] != nichts:
        ui_timer_entfernen(g["timer_id"])
        g["timer_id"] = nichts

    # 1 Initial
    visu_frame("frame_001_initial", "initial")

    # 2 Hover und 3 Pressed: direkte ui_moo-Eingabe, kein OS-Maus-Hook.
    uim_backend_bewegung(g["k"], 40, 66)
    visu_frame("frame_002_hover", "hover")
    uim_backend_maus_taste(g["k"], 40, 66, 1, wahr)
    visu_frame("frame_003_pressed", "pressed")
    uim_backend_maus_taste(g["k"], 40, 66, 1, falsch)

    # 4 Checked
    uim_backend_maus_taste(g["k"], 40, 112, 1, wahr)
    uim_backend_maus_taste(g["k"], 40, 112, 1, falsch)
    visu_frame("frame_004_checked", "checked")

    # 5 Slider
    uim_backend_maus_taste(g["k"], 185, 157, 1, wahr)
    uim_backend_maus_taste(g["k"], 185, 157, 1, falsch)
    visu_frame("frame_005_slider", "slider")

    # 6 Scroll
    uim_backend_bewegung(g["k"], 320, 80)
    uim_backend_rad(g["k"], 320, 80, 2)
    visu_frame("frame_006_scroll", "scroll")

    # 7 Theme
    uim_theme_setze(g["k"], uim_theme_hell())
    visu_frame("frame_007_theme", "theme")

    # 8 Resize: native Fenster und logische ui_moo-Wurzel gemeinsam.
    ui_fenster_groesse_setze(g["hFenster"], 720, 520)
    uim_groesse_setze(g["k"], 700, 460)
    ui_test_warte(150)
    visu_frame("frame_008_resize", "resize")

    setze bericht["fertig"] auf wahr
    ui_beenden()

ui_zeige_nebenbei(g["hFenster"])
g["timer_id"] = ui_timer_hinzu(500, lauf_sequenz)
ui_laufen()

wenn bericht["fertig"] != wahr:
    zeige "=== UIMOO-VISUELL FEHLER (Sequenz nicht gelaufen) ==="
    beende(1)

# --- Aktions-Reports auswerten ---
setze reports auf bericht["reports"]
setze total auf länge(reports)
setze okc auf 0
setze frames auf []
setze i auf 0
solange i < total:
    setze r auf reports[i]
    wenn r["erfolg"] == wahr:
        setze okc auf okc + 1
    wenn r["action"] == "frame":
        frames.hinzufügen(r["target"])
    setze i auf i + 1
zeige "UIMOO-Visuell: " + text(okc) + "/" + text(total) + " Aktionen OK"

# --- Frames: JSON+PNG vorhanden, JSON parsebar ---
setze dateien_ok auf wahr
setze evidenz_ok auf wahr
setze dimensionen_ok auf wahr
setze hash_folge_ok auf wahr
setze hash_format_ok auf wahr
setze resize_geaendert auf falsch
setze erwartete_zustaende auf ["initial", "hover", "pressed", "checked", "slider", "scroll", "theme", "resize"]
setze hash_gesehen auf {}
setze erste_b auf 0
setze erste_h auf 0
setze i auf 0
setze nf auf länge(frames)
solange i < nf:
    setze basis auf frames[i]
    setze jp auf basis + ".json"
    setze pp auf basis + ".png"
    wenn datei_existiert(jp) != wahr oder datei_existiert(pp) != wahr:
        zeige "FEHLT: " + basis + ".{json,png}"
        setze dateien_ok auf falsch
    sonst:
        setze parsed auf json_parse(datei_lesen(jp))
        wenn parsed == nichts:
            zeige "JSON KAPUTT: " + jp
            setze dateien_ok auf falsch
        sonst:
            setze state_schema_ok auf parsed.enthält("visual_state") und parsed.enthält("state_hash") und parsed.enthält("stale_visual") und parsed.enthält("runaway_geometry")
            wenn state_schema_ok == falsch:
                zeige "V1 RED: Zustands-Evidenz fehlt in " + jp
                setze evidenz_ok auf falsch
            sonst:
                wenn parsed["visual_state"] != erwartete_zustaende[i] oder parsed["stale_visual"] != falsch oder parsed["runaway_geometry"] != falsch:
                    setze evidenz_ok auf falsch
                setze aktueller_hash auf parsed["state_hash"]
                wenn sha256_lowerhex_ok(aktueller_hash) == falsch:
                    zeige "V1 RED: state_hash ist nicht exakt 64 lowercase-hex Zeichen in " + jp
                    setze hash_format_ok auf falsch
                wenn aktueller_hash == "" oder hash_gesehen.enthält(aktueller_hash):
                    setze hash_folge_ok auf falsch
                sonst:
                    hash_gesehen[aktueller_hash] = wahr
            wenn parsed.enthält("window_size") == falsch:
                setze dimensionen_ok auf falsch
            sonst:
                setze wb auf parsed["window_size"]["b"]
                setze wh auf parsed["window_size"]["h"]
                wenn wb <= 0 oder wh <= 0 oder wb > 4096 oder wh > 4096:
                    setze dimensionen_ok auf falsch
                wenn i == 0:
                    setze erste_b auf wb
                    setze erste_h auf wh
                wenn i == nf - 1:
                    setze resize_geaendert auf wb != erste_b oder wh != erste_h
            zeige "OK: " + jp
    setze i auf i + 1

# --- ui_moo-Zustand nach der Sequenz ---
setze chk_ok auf g["chk"]["wert"] == wahr
setze sw auf g["sl"]["wert"]
setze sl_ok auf sw > 72 und sw < 78
setze scroll_ok auf g["sc"]["scroll_y"] > 0
setze theme_ok auf g["k"]["theme"]["name"] == "hell"
setze groesse_ok auf g["k"]["wurzel"]["b"] == 700 und g["k"]["wurzel"]["h"] == 460

wenn okc == total und dateien_ok und nf == 8 und chk_ok und sl_ok und scroll_ok und theme_ok und groesse_ok und evidenz_ok und dimensionen_ok und resize_geaendert und hash_folge_ok und hash_format_ok:
    zeige "=== UIMOO-VISUELL OK states=8 stale_visual=0 runaway_geometry=0 ==="
sonst:
    zeige "=== P016-V1-VISUAL-CONTRACT-RED states=" + text(nf) + " evidence=" + text(evidenz_ok) + " dims=" + text(dimensionen_ok) + " resize=" + text(resize_geaendert) + " hashes=" + text(hash_folge_ok) + " hex=" + text(hash_format_ok) + " chk_ok=" + text(chk_ok) + " slider_wert=" + text(sw) + " sl_ok=" + text(sl_ok) + " scroll_y=" + text(g["sc"]["scroll_y"]) + " scroll_ok=" + text(scroll_ok) + " theme_name=" + text(g["k"]["theme"]["name"]) + " theme_ok=" + text(theme_ok) + " root_b=" + text(g["k"]["wurzel"]["b"]) + " root_h=" + text(g["k"]["wurzel"]["h"]) + " groesse_ok=" + text(groesse_ok) + " ==="
    beende(1)
