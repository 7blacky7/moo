# ============================================================
# stdlib/ui_moo_surface.moo — toolkitfreies RGBA-Offscreen-Backend
#
# Nutzt ausschließlich die finalen Surface-Builtins. Text und Textmetrik
# bleiben bewusst in Moo und verwenden den deterministischen _UIMF-3x5-Font.
# Kein Fenster, kein importiere ui, kein nativer Font und kein BMP-Builtin.
# BMP-Export: uim_surface_snapshot(k) -> test_frame_save_bmp(frame, pfad).
# ============================================================

importiere ui_moo_kern

funktion _uims_anfordern(kontext, args):
    setze zustand auf kontext["backend_zustand"]
    zustand["ungueltig"] = wahr
    gib_zurück wahr

funktion _uims_farbe(kontext, args):
    # _uim_farbe speichert RGBA bereits in kontext["_farbe"].
    gib_zurück wahr

funktion _uims_rect_fuell(kontext, z, x, y, b, h):
    setze f auf kontext["_farbe"]
    gib_zurück surface_rect(z, x, y, b, h, f[0], f[1], f[2], f[3])

funktion _uims_rect_rand(kontext, z, x, y, b, h):
    wenn b <= 0 oder h <= 0:
        gib_zurück falsch
    setze ok auf _uims_rect_fuell(kontext, z, x, y, b, 1)
    wenn h > 1:
        setze ok auf _uims_rect_fuell(kontext, z, x, y + h - 1, b, 1) und ok
    wenn h > 2:
        setze ok auf _uims_rect_fuell(kontext, z, x, y + 1, 1, h - 2) und ok
        wenn b > 1:
            setze ok auf _uims_rect_fuell(kontext, z, x + b - 1, y + 1, 1, h - 2) und ok
    gib_zurück ok

funktion _uims_rechteck(kontext, args):
    wenn args[5]:
        gib_zurück _uims_rect_fuell(kontext, args[0], args[1], args[2], args[3], args[4])
    gib_zurück _uims_rect_rand(kontext, args[0], args[1], args[2], args[3], args[4])

funktion _uims_rechteck_rund(kontext, args):
    wenn args[6]:
        setze f auf kontext["_farbe"]
        gib_zurück surface_roundrect(args[0], args[1], args[2], args[3], args[4], args[5], f[0], f[1], f[2], f[3])
    # Der native Vertrag besitzt nur gefüllte Roundrects. Der einpixelige
    # Rand degradiert explizit auf einen überlappungsfreien Rechteckrand.
    gib_zurück _uims_rect_rand(kontext, args[0], args[1], args[2], args[3], args[4])

funktion _uims_kreis_rand(kontext, z, cx, cy, radius):
    wenn radius < 0:
        gib_zurück falsch
    setze f auf kontext["_farbe"]
    setze r2 auf radius * radius
    setze innen_radius auf radius - 1
    setze innen2 auf innen_radius * innen_radius
    wenn innen_radius < 0:
        setze innen2 auf 0 - 1
    setze dy auf 0 - radius
    setze ok auf wahr
    solange dy <= radius:
        setze dx auf 0 - radius
        solange dx <= radius:
            setze d2 auf dx * dx + dy * dy
            wenn d2 <= r2 und d2 > innen2:
                setze ok auf surface_rect(z, cx + dx, cy + dy, 1, 1, f[0], f[1], f[2], f[3]) und ok
            setze dx auf dx + 1
        setze dy auf dy + 1
    gib_zurück ok

funktion _uims_kreis(kontext, args):
    wenn args[4]:
        setze f auf kontext["_farbe"]
        gib_zurück surface_circle(args[0], args[1], args[2], args[3], f[0], f[1], f[2], f[3])
    gib_zurück _uims_kreis_rand(kontext, args[0], args[1], args[2], args[3])

funktion _uims_linie(kontext, args):
    setze breite auf args[5]
    wenn breite <= 0:
        gib_zurück falsch
    setze f auf kontext["_farbe"]
    setze dx auf args[3] - args[1]
    wenn dx < 0:
        setze dx auf 0 - dx
    setze dy auf args[4] - args[2]
    wenn dy < 0:
        setze dy auf 0 - dy
    setze start auf 0
    setze rest auf breite - 1
    solange rest >= 2:
        setze start auf start - 1
        setze rest auf rest - 2
    setze i auf 0
    setze ok auf wahr
    solange i < breite:
        setze versatz auf start + i
        wenn dx >= dy:
            setze ok auf surface_line(args[0], args[1], args[2] + versatz, args[3], args[4] + versatz, f[0], f[1], f[2], f[3]) und ok
        sonst:
            setze ok auf surface_line(args[0], args[1] + versatz, args[2], args[3] + versatz, args[4], f[0], f[1], f[2], f[3]) und ok
        setze i auf i + 1
    gib_zurück ok

funktion _uims_text(kontext, args):
    setze z auf args[0]
    setze x auf args[1]
    setze y auf args[2]
    setze s auf args[3]
    setze px auf _uim_zf_px(args[4])
    setze f auf kontext["_farbe"]
    setze i auf 0
    setze cx auf x
    setze ok auf wahr
    solange i < länge(s):
        setze ch auf s[i]
        wenn _UIMF.enthält(ch):
            setze bits auf _UIMF[ch]
            setze zy auf 0
            solange zy < 5:
                setze zx auf 0
                solange zx < 3:
                    wenn bits[zy * 3 + zx] == 1:
                        setze ok auf surface_rect(z, cx + zx * px, y + zy * px, px, px, f[0], f[1], f[2], f[3]) und ok
                    setze zx auf zx + 1
                setze zy auf zy + 1
        setze cx auf cx + 4 * px
        setze i auf i + 1
    gib_zurück ok

funktion _uims_text_breite(kontext, args):
    gib_zurück länge(args[1]) * 4 * _uim_zf_px(args[2])

funktion _uims_clip_setze(kontext, args):
    gib_zurück surface_clip_push(args[0], args[1], args[2], args[3], args[4])

funktion _uims_clip_loesche(kontext, args):
    gib_zurück surface_clip_pop(args[0])

funktion uim_backend_surface():
    setze ops auf {}
    ops["anfordern"] = _uims_anfordern
    ops["farbe"] = _uims_farbe
    ops["rechteck"] = _uims_rechteck
    ops["rechteck_rund"] = _uims_rechteck_rund
    ops["kreis"] = _uims_kreis
    ops["linie"] = _uims_linie
    ops["text"] = _uims_text
    ops["text_breite"] = _uims_text_breite
    ops["clip_setze"] = _uims_clip_setze
    ops["clip_loesche"] = _uims_clip_loesche
    gib_zurück uim_backend_neu("surface-rgba8", _uim_faehigkeiten(wahr, wahr, falsch, wahr, "pixelfont-3x5"), ops)

funktion uim_surface_wurzel(b, h):
    setze bildflaeche auf surface_new(b, h)
    wenn bildflaeche == nichts:
        gib_zurück nichts
    setze kontext auf uim_backend_wurzel(uim_backend_surface(), b, h)
    wenn kontext == nichts:
        gib_zurück nichts
    setze zustand auf kontext["backend_zustand"]
    zustand["surface"] = bildflaeche
    zustand["ungueltig"] = wahr
    gib_zurück kontext

funktion uim_surface_handle(kontext):
    gib_zurück kontext["backend_zustand"]["surface"]

funktion uim_surface_leeren(kontext, r, g, b, a):
    gib_zurück surface_clear(uim_surface_handle(kontext), r, g, b, a)

funktion uim_surface_zeichne(kontext):
    setze bildflaeche auf uim_surface_handle(kontext)
    setze ok auf uim_backend_zeichne(kontext, bildflaeche)
    wenn ok:
        kontext["backend_zustand"]["ungueltig"] = falsch
    sonst:
        kontext["backend_zustand"]["ungueltig"] = wahr
    gib_zurück ok

funktion uim_surface_hash(kontext):
    gib_zurück surface_hash(uim_surface_handle(kontext))

funktion uim_surface_pixel(kontext, x, y):
    gib_zurück surface_read_pixel(uim_surface_handle(kontext), x, y)

funktion uim_surface_snapshot(kontext):
    gib_zurück surface_snapshot_to_frame(uim_surface_handle(kontext))

funktion uim_surface_maus(kontext, x, y, gedrueckt):
    gib_zurück uim_backend_maus(kontext, x, y, gedrueckt)

funktion uim_surface_bewegung(kontext, x, y):
    gib_zurück uim_backend_bewegung(kontext, x, y)

funktion uim_surface_groesse_setze(kontext, b, h):
    # Die native Surface ist unveränderlich dimensioniert. Nur identische
    # Größen sind zulässig; für andere Größen eine neue Wurzel erzeugen.
    setze wurzel auf kontext["wurzel"]
    wenn wurzel["b"] != b oder wurzel["h"] != h:
        gib_zurück falsch
    gib_zurück wahr

funktion uim_surface_taste(kontext, taste, gedrueckt, mod):
    gib_zurück uim_backend_taste(kontext, taste, gedrueckt, mod)

funktion uim_surface_rad(kontext, x, y, delta):
    gib_zurück uim_backend_rad(kontext, x, y, delta)

funktion uim_surface_fokus(kontext, hat_fokus):
    gib_zurück uim_backend_fokus(kontext, hat_fokus)
