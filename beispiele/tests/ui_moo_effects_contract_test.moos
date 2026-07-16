# P016-O5 UI1: toolkitfreier Vertrag fuer die oeffentliche Moo-Effekt-API.
# Keine Desktop-UI, kein Hostbackend und keine Uhr. Dieses Harness ist absichtlich
# zuerst RED und prueft die protokolltreuen Descriptor-/Fallback-Semantiken.

importiere ui_moo_effects

setze qa auf {}
qa["fehler"] = 0

funktion pruef(name, ok):
    wenn ok:
        zeige "PASS " + name
    sonst:
        zeige "FAIL " + name
        qa["fehler"] = qa["fehler"] + 1
    gib_zurück ok

funktion farbe_ist(f, r, g, b, a):
    gib_zurück f["r"] == r und f["g"] == g und f["b"] == b und f["a"] == a

setze bits auf uim_effekt_faehigkeiten()
pruef("bits-corner", bits["ecken"] == 1)
pruef("bits-shadow", bits["schatten"] == 2)
pruef("bits-backdrop", bits["hintergrund_unschaerfe"] == 4)
pruef("bits-saturation", bits["saettigung"] == 8)
pruef("bits-tint", bits["toenung"] == 16)
pruef("bits-noise", bits["rauschen"] == 32)
pruef("bits-affine", bits["transform"] == 64)
pruef("bits-animation", bits["animation"] == 128)
pruef("bits-all", bits["alle"] == 255)

setze e auf uim_effekt_neu()
pruef("neutral-mask", e["aktiv"] == 0 und e["erforderlich"] == 0)
pruef("neutral-policy", e["fallback"] == 0)
pruef("neutral-saturation", e["hintergrund"]["saettigung"] == 256)
pruef("neutral-affine", e["transform"]["m11"] == 65536 und e["transform"]["m22"] == 65536)
pruef("neutral-alpha", farbe_ist(e["schatten"]["farbe"], 0, 0, 0, 0))

pruef("set-corners", uim_effekt_ecken_setze(e, 18, 16, 14, 12))
pruef("set-shadow", uim_effekt_schatten_setze(e, 0, 12, 24, 2, 0, 0, 0, 120))
pruef("set-backdrop", uim_effekt_hintergrund_setze(e, 20, 300, 72, 118, 255, 96, 96, 24, 12345))
pruef("set-transform", uim_effekt_transform_setze(e, 65536, 2048, 0 - 2048, 65536, 131072, 65536, 0, 0))
pruef("set-animation-bit", uim_effekt_animation_aktiv_setze(e, wahr))
pruef("combined-mask", e["aktiv"] == 255)
pruef("corner-order", e["ecken"]["oben_links"] == 18 und e["ecken"]["unten_links"] == 12)
pruef("shadow-values", e["schatten"]["y"] == 12 und e["schatten"]["unschaerfe"] == 24)
pruef("backdrop-values", e["hintergrund"]["saettigung"] == 300 und e["hintergrund"]["rauschen_seed"] == 12345)
pruef("affine-values", e["transform"]["m12"] == 2048 und e["transform"]["tx"] == 131072)

# Vollstaendige portable Faehigkeiten duerfen requested/effective nicht degradieren.
setze voll auf uim_effekt_aufloesen(e, 255)
pruef("full-ok", voll["ok"])
pruef("full-requested", voll["angefordert"]["aktiv"] == 255)
pruef("full-effective", voll["effektiv"]["aktiv"] == 255)
pruef("full-degraded-zero", voll["degradiert"] == 0)

# ALLOW_DISABLE deaktiviert fehlende Effekte und setzt deren Parameter neutral.
e["fallback"] = 1
setze nur_ecken_schatten auf uim_effekt_aufloesen(e, 3)
pruef("disable-ok", nur_ecken_schatten["ok"])
pruef("disable-mask", nur_ecken_schatten["effektiv"]["aktiv"] == 3)
pruef("disable-degraded", nur_ecken_schatten["degradiert"] == 252)
pruef("disable-blur-neutral", nur_ecken_schatten["effektiv"]["hintergrund"]["unschaerfe"] == 0)
pruef("disable-saturation-neutral", nur_ecken_schatten["effektiv"]["hintergrund"]["saettigung"] == 256)
pruef("disable-tint-neutral", farbe_ist(nur_ecken_schatten["effektiv"]["hintergrund"]["toenung"], 0, 0, 0, 0))
pruef("disable-affine-neutral", nur_ecken_schatten["effektiv"]["transform"]["m11"] == 65536 und nur_ecken_schatten["effektiv"]["transform"]["tx"] == 0)

# Required gewinnt immer gegen den Fallback.
e["erforderlich"] = bits["hintergrund_unschaerfe"]
setze required_fehlt auf uim_effekt_aufloesen(e, 251)
pruef("required-nogo", required_fehlt["ok"] == falsch)
pruef("required-atomic", required_fehlt["effektiv"] == nichts und required_fehlt["degradiert"] == 0)
e["erforderlich"] = 0

# ALLOW_APPROXIMATE ist nur fuer Blur/Saettigung/Rauschen zulaessig.
e["fallback"] = 2
setze approx_blur auf uim_effekt_aufloesen(e, 251)
pruef("approx-blur-ok", approx_blur["ok"])
pruef("approx-blur-degraded", approx_blur["degradiert"] == 4)
setze approx_tint auf uim_effekt_aufloesen(e, 239)
pruef("approx-tint-nogo", approx_tint["ok"] == falsch)
pruef("approx-tint-atomic", approx_tint["effektiv"] == nichts)

# REQUIRE verweigert jede Degradation.
e["fallback"] = 0
setze strict_fehlt auf uim_effekt_aufloesen(e, 127)
pruef("strict-animation-nogo", strict_fehlt["ok"] == falsch)

# Eingaben ausserhalb des V2-Vertrags duerfen den Descriptor nicht mutieren.
setze vorher auf e["aktiv"]
pruef("invalid-policy", uim_effekt_fallback_setze(e, 3) == falsch)
pruef("invalid-required", uim_effekt_erforderlich_setze(e, 256) == falsch)
pruef("invalid-corner", uim_effekt_ecken_setze(e, 5000, 0, 0, 0) == falsch)
pruef("invalid-shadow", uim_effekt_schatten_setze(e, 0, 0, 65, 0, 0, 0, 0, 0) == falsch)
pruef("invalid-backdrop", uim_effekt_hintergrund_setze(e, 33, 256, 0, 0, 0, 0, 0, 0) == falsch)
pruef("invalid-no-mutation", e["aktiv"] == vorher und e["ecken"]["oben_links"] == 18)

# Deterministische Animation und Reduced-Motion-Endpunkt.
setze a auf uim_effekt_animation("deckkraft", 0, 65536, 1000, "linear", 1)
pruef("animation-valid", a != nichts)
pruef("animation-start", uim_effekt_animation_wert(a, 0, falsch) == 0)
pruef("animation-half", uim_effekt_animation_wert(a, 500, falsch) == 32768)
pruef("animation-end", uim_effekt_animation_wert(a, 1000, falsch) == 65536)
pruef("animation-reduced", uim_effekt_animation_wert(a, 1, wahr) == 65536)
setze a2 auf uim_effekt_animation("deckkraft", 0, 65536, 1000, "linear", 2)
pruef("animation-repeat-boundary", uim_effekt_animation_wert(a2, 1000, falsch) == 0)
pruef("animation-repeat-terminal", uim_effekt_animation_wert(a2, 2000, falsch) == 65536)
pruef("animation-invalid-duration", uim_effekt_animation("deckkraft", 0, 1, 0, "linear", 1) == nichts)

# Status ist maschinenlesbar fuer Gallery/Diagnose.
setze diag auf uim_effekt_status("surface-rgba8", nur_ecken_schatten)
pruef("status-backend", diag["backend"] == "surface-rgba8")
pruef("status-capabilities", diag["faehigkeiten"] == 3)
pruef("status-requested", diag["angefordert"] == 255)
pruef("status-effective", diag["effektiv"] == 3)
pruef("status-degraded", diag["degradiert"] == 252)

wenn qa["fehler"] == 0:
    zeige "P016-UI1-EFFECTS-CONTRACT-OK"
sonst:
    zeige "P016-UI1-EFFECTS-CONTRACT-FEHLER " + text(qa["fehler"])
    wirf "P016 UI1 Effektvertrag verletzt"
