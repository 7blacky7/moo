# P016-O2 B1: kopfloser Unicode-/Graphem-Fallbackvertrag fuer den Moo-Surface-Textpfad.
# Kein importiere ui, kein Fenster und kein nativer Plattform-Text.

importiere ui_moo_surface

setze ergebnis auf {}
ergebnis["fehler"] = 0
ergebnis["checks"] = 0

funktion pruef(name, ok):
    ergebnis["checks"] = ergebnis["checks"] + 1
    wenn ok:
        zeige "PASS " + name
    sonst:
        zeige "FAIL " + name
        ergebnis["fehler"] = ergebnis["fehler"] + 1
    gib_zurück ok

funktion metrik_digest(kontext, zeichner):
    setze teile auf []
    teile.hinzufügen(text(_uim_zf_text_breite(kontext, zeichner, "A", 11)))
    teile.hinzufügen(text(_uim_zf_text_breite(kontext, zeichner, "A", 12)))
    teile.hinzufügen(text(_uim_zf_text_breite(kontext, zeichner, "é", 11)))
    teile.hinzufügen(text(_uim_zf_text_breite(kontext, zeichner, "é", 12)))
    teile.hinzufügen(text(_uim_zf_text_breite(kontext, zeichner, "👨‍👩‍👧‍👦", 11)))
    teile.hinzufügen(text(_uim_zf_text_breite(kontext, zeichner, "👨‍👩‍👧‍👦", 12)))
    teile.hinzufügen(text(_uim_zf_text_breite(kontext, zeichner, "🇩🇪", 11)))
    teile.hinzufügen(text(_uim_zf_text_breite(kontext, zeichner, "🇩🇪", 12)))
    gib_zurück teile[0] + "|" + teile[1] + "|" + teile[2] + "|" + teile[3] + "|" + teile[4] + "|" + teile[5] + "|" + teile[6] + "|" + teile[7]

funktion raster_digest(groesse):
    setze kontext auf uim_surface_wurzel(96, 24)
    setze zeichner auf uim_surface_handle(kontext)
    uim_surface_leeren(kontext, 0, 0, 0, 0)
    _uim_farbe(kontext, zeichner, [240, 230, 220, 255])
    _uim_zf_text(kontext, zeichner, 0, 0, "é", groesse)
    _uim_zf_text(kontext, zeichner, 24, 0, "👨‍👩‍👧‍👦", groesse)
    _uim_zf_text(kontext, zeichner, 48, 0, "🇩🇪", groesse)
    gib_zurück uim_surface_hash(kontext)

funktion raster_text_hash(s):
    setze kontext auf uim_surface_wurzel(1040, 8)
    setze zeichner auf uim_surface_handle(kontext)
    uim_surface_leeren(kontext, 0, 0, 0, 0)
    _uim_farbe(kontext, zeichner, [240, 230, 220, 255])
    _uim_zf_text(kontext, zeichner, 0, 0, s, 11)
    gib_zurück uim_surface_hash(kontext)

setze text_k auf uim_surface_wurzel(96, 24)
setze text_z auf uim_surface_handle(text_k)

pruef("ascii-scale1", _uim_zf_text_breite(text_k, text_z, "A", 11) == 4)
pruef("ascii-scale2", _uim_zf_text_breite(text_k, text_z, "A", 12) == 8)
pruef("decomposed-grapheme-scale1", _uim_zf_text_breite(text_k, text_z, "é", 11) == 4)
pruef("decomposed-grapheme-scale2", _uim_zf_text_breite(text_k, text_z, "é", 12) == 8)
pruef("zwj-family-grapheme-scale1", _uim_zf_text_breite(text_k, text_z, "👨‍👩‍👧‍👦", 11) == 4)
pruef("zwj-family-grapheme-scale2", _uim_zf_text_breite(text_k, text_z, "👨‍👩‍👧‍👦", 12) == 8)
pruef("ri-flag-grapheme-scale1", _uim_zf_text_breite(text_k, text_z, "🇩🇪", 11) == 4)
pruef("ri-flag-grapheme-scale2", _uim_zf_text_breite(text_k, text_z, "🇩🇪", 12) == 8)

setze metrik_a auf metrik_digest(text_k, text_z)
setze metrik_b auf metrik_digest(text_k, text_z)
pruef("metrik-digest-repeat", metrik_a == metrik_b)
pruef("metrik-digest-kanonisch", metrik_a == "4|8|4|8|4|8|4|8")

setze raster_1a auf raster_digest(11)
setze raster_1b auf raster_digest(11)
setze raster_2a auf raster_digest(12)
setze raster_2b auf raster_digest(12)
pruef("raster-scale1-repeat", raster_1a == raster_1b)
pruef("raster-scale2-repeat", raster_2a == raster_2b)
pruef("raster-scale-unterscheidbar", raster_1a != raster_2a)
pruef("raster-digest-repeat", raster_1a + "|" + raster_2a == raster_1b + "|" + raster_2b)

# B3: Jede fehlerhafte UTF-8-Byteposition erzeugt genau ein Fallback-Glyph.
# Der Decoder muss nach einem Fehler um genau ein Byte resynchronisieren.
setze kaputt_overlong auf bytes_neu([192, 175])
setze kaputt_truncated auf bytes_neu([226, 130])
setze kaputt_stray auf bytes_neu([128])
setze kaputt_surrogate auf bytes_neu([237, 160, 128])
setze kaputt_too_high auf bytes_neu([244, 144, 128, 128])

pruef("malformed-overlong-width", _uim_zf_text_breite(text_k, text_z, kaputt_overlong, 11) == 8)
pruef("malformed-overlong-raster", raster_text_hash(kaputt_overlong) == raster_text_hash("??"))
pruef("malformed-truncated-width", _uim_zf_text_breite(text_k, text_z, kaputt_truncated, 11) == 8)
pruef("malformed-truncated-raster", raster_text_hash(kaputt_truncated) == raster_text_hash("??"))
pruef("malformed-stray-width", _uim_zf_text_breite(text_k, text_z, kaputt_stray, 11) == 4)
pruef("malformed-stray-raster", raster_text_hash(kaputt_stray) == raster_text_hash("?"))
pruef("malformed-surrogate-width", _uim_zf_text_breite(text_k, text_z, kaputt_surrogate, 11) == 12)
pruef("malformed-surrogate-raster", raster_text_hash(kaputt_surrogate) == raster_text_hash("???"))
pruef("malformed-too-high-width", _uim_zf_text_breite(text_k, text_z, kaputt_too_high, 11) == 16)
pruef("malformed-too-high-raster", raster_text_hash(kaputt_too_high) == raster_text_hash("????"))

setze kaputt_mixed auf bytes_neu([65, 192, 175, 66])
pruef("malformed-resync-width", _uim_zf_text_breite(text_k, text_z, kaputt_mixed, 11) == 16)
pruef("malformed-resync-raster", raster_text_hash(kaputt_mixed) == raster_text_hash("A??B"))

setze viele_bytes auf []
setze viele_fragezeichen auf ""
setze viele_i auf 0
solange viele_i < 256:
    viele_bytes.hinzufügen(128)
    setze viele_fragezeichen auf viele_fragezeichen + "?"
    setze viele_i auf viele_i + 1
setze viele_kaputt auf bytes_neu(viele_bytes)
pruef("malformed-256-width", _uim_zf_text_breite(text_k, text_z, viele_kaputt, 11) == 1024)
pruef("malformed-256-raster", raster_text_hash(viele_kaputt) == raster_text_hash(viele_fragezeichen))

# B4: Versionierter logischer Textplan. Das ist bewusst KEIN voller
# Unicode-BiDi-Algorithmus und KEIN echter Shaper. v1 klassifiziert Runs in
# deterministischer UTF-8-Quellreihenfolge und bewahrt den Pixelfont-Fallback.
funktion text_plan_optionen(version, richtung, skript):
    setze o auf {}
    o["version"] = version
    o["direction"] = richtung
    o["script"] = skript
    gib_zurück o

setze plan_ltr auf text_plan_optionen(1, "ltr", "latin")
setze ascii_plan auf uim_text_plan("AB", plan_ltr)
pruef("plan-ascii-dict", typ_von(ascii_plan) == "Woerterbuch")
pruef("plan-ascii-schema", ascii_plan["version"] == 1 und ascii_plan["order"] == "logical" und ascii_plan["source_unit"] == "utf8-bytes" und ascii_plan["cluster_model"] == "bounded-v1" und ascii_plan["full_bidi"] == falsch und ascii_plan["full_shaping"] == falsch und ascii_plan["direction"] == "ltr" und ascii_plan["script"] == "latin" und ascii_plan["supported"] == wahr und ascii_plan["fallback"] == "none")
pruef("plan-ascii-glyph-advance", länge(ascii_plan["glyphs"]) == 2 und ascii_plan["glyphs"][0] == "A" und ascii_plan["glyphs"][1] == "B" und ascii_plan["advance"] == _uim_zf_text_breite(text_k, text_z, "AB", 11))
setze ascii_run auf ascii_plan["runs"][0]
pruef("plan-ascii-run", länge(ascii_plan["runs"]) == 1 und ascii_run["source_start"] == 0 und ascii_run["source_end"] == 2 und ascii_run["cluster_start"] == 0 und ascii_run["cluster_count"] == 2 und ascii_run["direction"] == "ltr" und ascii_run["script"] == "latin" und ascii_run["supported"] == wahr und ascii_run["fallback"] == "none")

setze plan_auto auf text_plan_optionen(1, "auto", "auto")
setze mixed_text auf "Aאب"
setze mixed_plan auf uim_text_plan(mixed_text, plan_auto)
pruef("plan-mixed-schema", typ_von(mixed_plan) == "Woerterbuch" und mixed_plan["version"] == 1 und mixed_plan["order"] == "logical" und mixed_plan["source_unit"] == "utf8-bytes" und mixed_plan["cluster_model"] == "bounded-v1" und mixed_plan["full_bidi"] == falsch und mixed_plan["full_shaping"] == falsch und mixed_plan["direction"] == "ltr" und mixed_plan["script"] == "mixed" und mixed_plan["supported"] == falsch und mixed_plan["fallback"] == "logical-glyph")
pruef("plan-mixed-glyph-advance-raster", länge(mixed_plan["glyphs"]) == 3 und mixed_plan["glyphs"][0] == "A" und mixed_plan["glyphs"][1] == "?" und mixed_plan["glyphs"][2] == "?" und mixed_plan["advance"] == 12 und mixed_plan["advance"] == _uim_zf_text_breite(text_k, text_z, mixed_text, 11) und raster_text_hash(mixed_text) == raster_text_hash("A??"))
pruef("plan-mixed-runs-source-order", länge(mixed_plan["runs"]) == 3)
setze mixed_latin auf mixed_plan["runs"][0]
setze mixed_hebrew auf mixed_plan["runs"][1]
setze mixed_arabic auf mixed_plan["runs"][2]
pruef("plan-mixed-latin-run", mixed_latin["source_start"] == 0 und mixed_latin["source_end"] == 1 und mixed_latin["cluster_start"] == 0 und mixed_latin["cluster_count"] == 1 und mixed_latin["direction"] == "ltr" und mixed_latin["script"] == "latin" und mixed_latin["supported"] == wahr und mixed_latin["fallback"] == "none")
pruef("plan-mixed-hebrew-run", mixed_hebrew["source_start"] == 1 und mixed_hebrew["source_end"] == 3 und mixed_hebrew["cluster_start"] == 1 und mixed_hebrew["cluster_count"] == 1 und mixed_hebrew["direction"] == "rtl" und mixed_hebrew["script"] == "hebrew" und mixed_hebrew["supported"] == falsch und mixed_hebrew["fallback"] == "logical-glyph")
pruef("plan-mixed-arabic-run", mixed_arabic["source_start"] == 3 und mixed_arabic["source_end"] == 5 und mixed_arabic["cluster_start"] == 2 und mixed_arabic["cluster_count"] == 1 und mixed_arabic["direction"] == "rtl" und mixed_arabic["script"] == "arabic" und mixed_arabic["supported"] == falsch und mixed_arabic["fallback"] == "logical-glyph")

# Arabisch bleibt explizit ungestuetzt: keine Ligatur und keine visuelle
# Umordnung, sondern ein logisches Fallback-Glyph pro Graphem.
setze arabic_text auf "لا"
setze arabic_plan auf uim_text_plan(arabic_text, text_plan_optionen(1, "rtl", "arabic"))
pruef("plan-arabic-schema-unsupported", arabic_plan["version"] == 1 und arabic_plan["order"] == "logical" und arabic_plan["source_unit"] == "utf8-bytes" und arabic_plan["cluster_model"] == "bounded-v1" und arabic_plan["full_bidi"] == falsch und arabic_plan["full_shaping"] == falsch und arabic_plan["direction"] == "rtl" und arabic_plan["script"] == "arabic" und arabic_plan["supported"] == falsch und arabic_plan["fallback"] == "logical-glyph")
pruef("plan-arabic-logical-fallback", länge(arabic_plan["glyphs"]) == 2 und arabic_plan["glyphs"][0] == "?" und arabic_plan["glyphs"][1] == "?" und arabic_plan["advance"] == 8 und arabic_plan["advance"] == _uim_zf_text_breite(text_k, text_z, arabic_text, 11) und raster_text_hash(arabic_text) == raster_text_hash("??"))
setze arabic_run auf arabic_plan["runs"][0]
pruef("plan-arabic-run", länge(arabic_plan["runs"]) == 1 und arabic_run["source_start"] == 0 und arabic_run["source_end"] == 4 und arabic_run["cluster_start"] == 0 und arabic_run["cluster_count"] == 2 und arabic_run["direction"] == "rtl" und arabic_run["script"] == "arabic" und arabic_run["supported"] == falsch und arabic_run["fallback"] == "logical-glyph")

setze family_text auf "👨‍👩‍👧‍👦"
setze family_plan auf uim_text_plan(family_text, plan_auto)
setze family_run auf family_plan["runs"][0]
pruef("plan-family-grapheme-preserved", family_plan["order"] == "logical" und family_plan["source_unit"] == "utf8-bytes" und family_plan["cluster_model"] == "bounded-v1" und family_plan["full_bidi"] == falsch und family_plan["full_shaping"] == falsch und family_plan["direction"] == "ltr" und family_plan["script"] == "common" und family_plan["supported"] == falsch und family_plan["fallback"] == "logical-glyph" und länge(family_plan["glyphs"]) == 1 und family_plan["glyphs"][0] == "?" und family_plan["advance"] == 4 und länge(family_plan["runs"]) == 1 und family_run["source_start"] == 0 und family_run["source_end"] == 25 und family_run["cluster_start"] == 0 und family_run["cluster_count"] == 1)

pruef("plan-v2-fail-closed", uim_text_plan("A", text_plan_optionen(2, "ltr", "latin")) == nichts)
pruef("plan-direction-invalid-fail-closed", uim_text_plan("A", text_plan_optionen(1, "sideways", "latin")) == nichts)
pruef("plan-script-invalid-fail-closed", uim_text_plan("A", text_plan_optionen(1, "ltr", "klingon")) == nichts)
pruef("plan-input-fail-closed", uim_text_plan("A", {}) == nichts und uim_text_plan("A", nichts) == nichts und uim_text_plan("A", "ungueltig") == nichts und uim_text_plan(nichts, plan_ltr) == nichts)

# B5: Versioniertes, messbares Capability-Manifest des eingebetteten
# Textpfads. Es beschreibt nur reale Eigenschaften und behauptet weder
# Font-Fallback noch BiDi/Shaping/Kerning/Subpixel-Unterstuetzung.
funktion capability_raster_hash(s, groesse):
    setze k auf uim_surface_wurzel(64, 16)
    setze z auf uim_surface_handle(k)
    uim_surface_leeren(k, 0, 0, 0, 0)
    _uim_farbe(k, z, [240, 230, 220, 255])
    _uim_zf_text(k, z, 0, 0, s, groesse)
    gib_zurück uim_surface_hash(k)

setze capability_a auf uim_text_faehigkeiten(1)
setze capability_b auf uim_text_faehigkeiten(1)
pruef("capability-schema-v1", typ_von(capability_a) == "Woerterbuch" und länge(capability_a) == 22)
pruef("capability-identity-source", capability_a["version"] == 1 und capability_a["source_unit"] == "utf8-bytes" und capability_a["engine"] == "embedded-pixel-v1" und capability_a["adapter_kind"] == "embedded" und capability_a["plan_version"] == 1)
pruef("capability-layout-honest", capability_a["cluster_model"] == "bounded-v1" und capability_a["order"] == "logical" und capability_a["full_bidi"] == falsch und capability_a["full_shaping"] == falsch und capability_a["kerning"] == falsch)
pruef("capability-font-replacement-honest", capability_a["font_source"] == "embedded-3x5" und capability_a["font_fallback"] == falsch und capability_a["replacement_glyph"] == "?" und capability_a["replacement_unit"] == "bounded-cluster")
pruef("capability-raster-honest", capability_a["rasterization"] == "monochrome" und capability_a["grayscale"] == falsch und capability_a["subpixel"] == falsch)
pruef("capability-metrics-scale-exact", capability_a["metrics"] == "deterministic-integer" und capability_a["scale_model"] == "font-size-threshold" und capability_a["scale_threshold"] == 12 und capability_a["scale_below"] == 1 und capability_a["scale_at_or_above"] == 2)
pruef("capability-version-fail-closed", uim_text_faehigkeiten(0) == nichts und uim_text_faehigkeiten(2) == nichts und uim_text_faehigkeiten(nichts) == nichts und uim_text_faehigkeiten("1") == nichts und uim_text_faehigkeiten(wahr) == nichts)

capability_a["engine"] = "mutiert"
setze capability_c auf uim_text_faehigkeiten(1)
pruef("capability-fresh-copy-isolation", capability_a["engine"] == "mutiert" und capability_b["engine"] == "embedded-pixel-v1" und capability_c["engine"] == "embedded-pixel-v1")

setze capability_ersatz_text auf "👨‍👩‍👧‍👦🇩🇪"
setze capability_ersatz_plan auf uim_text_plan(capability_ersatz_text, plan_auto)
pruef("capability-actual-plan-consistency", capability_ersatz_plan["version"] == capability_b["plan_version"] und capability_ersatz_plan["source_unit"] == capability_b["source_unit"] und capability_ersatz_plan["cluster_model"] == capability_b["cluster_model"] und capability_ersatz_plan["order"] == capability_b["order"] und capability_ersatz_plan["full_bidi"] == capability_b["full_bidi"] und capability_ersatz_plan["full_shaping"] == capability_b["full_shaping"])
pruef("capability-one-replacement-per-bounded-cluster", länge(capability_ersatz_plan["glyphs"]) == 2 und capability_ersatz_plan["glyphs"][0] == capability_b["replacement_glyph"] und capability_ersatz_plan["glyphs"][1] == capability_b["replacement_glyph"] und capability_ersatz_plan["advance"] == 8)
pruef("capability-width-raster-scale11-12", _uim_zf_text_breite(text_k, text_z, capability_ersatz_text, 11) == capability_ersatz_plan["advance"] * capability_b["scale_below"] und _uim_zf_text_breite(text_k, text_z, capability_ersatz_text, 12) == capability_ersatz_plan["advance"] * capability_b["scale_at_or_above"] und capability_raster_hash(capability_ersatz_text, 11) == capability_raster_hash("??", 11) und capability_raster_hash(capability_ersatz_text, 12) == capability_raster_hash("??", 12))

# B6: Logische Caret-/Selection-Geometrie auf exakt denselben bounded-v1-
# Clustergrenzen. Offsets bleiben UTF-8-Bytes; Advances sind unskaliert.
funktion auswahl_plan(s, optionen, start, ende):
    gib_zurück uim_text_auswahl_plan(s, optionen, start, ende)

setze selection_empty_a auf auswahl_plan("", plan_auto, 0, 0)
setze selection_empty_b auf auswahl_plan("", plan_auto, 0, 0)
pruef("selection-empty-schema", typ_von(selection_empty_a) == "Woerterbuch" und länge(selection_empty_a) == 12 und selection_empty_a.enthält("version") und selection_empty_a.enthält("source_unit") und selection_empty_a.enthält("cluster_model") und selection_empty_a.enthält("order") und selection_empty_a.enthält("full_bidi") und selection_empty_a.enthält("selection_start") und selection_empty_a.enthält("selection_end") und selection_empty_a.enthält("cluster_start") und selection_empty_a.enthält("cluster_end") und selection_empty_a.enthält("advance_start") und selection_empty_a.enthält("advance_end") und selection_empty_a.enthält("collapsed") und selection_empty_a["version"] == 1 und selection_empty_a["source_unit"] == "utf8-bytes" und selection_empty_a["cluster_model"] == "bounded-v1" und selection_empty_a["order"] == "logical" und selection_empty_a["full_bidi"] == falsch und selection_empty_a["selection_start"] == 0 und selection_empty_a["selection_end"] == 0 und selection_empty_a["cluster_start"] == 0 und selection_empty_a["cluster_end"] == 0 und selection_empty_a["advance_start"] == 0 und selection_empty_a["advance_end"] == 0 und selection_empty_a["collapsed"] == wahr)
selection_empty_a["order"] = "mutiert"
pruef("selection-fresh-copy", selection_empty_a["order"] == "mutiert" und selection_empty_b["order"] == "logical")

setze selection_ascii auf auswahl_plan("ABC", plan_auto, 1, 3)
pruef("selection-ascii-range", selection_ascii["selection_start"] == 1 und selection_ascii["selection_end"] == 3 und selection_ascii["cluster_start"] == 1 und selection_ascii["cluster_end"] == 3 und selection_ascii["advance_start"] == 4 und selection_ascii["advance_end"] == 12 und selection_ascii["collapsed"] == falsch)
setze caret_ascii auf auswahl_plan("ABC", plan_auto, 2, 2)
pruef("caret-ascii-collapsed", caret_ascii["cluster_start"] == 2 und caret_ascii["cluster_end"] == 2 und caret_ascii["advance_start"] == 8 und caret_ascii["advance_end"] == 8 und caret_ascii["collapsed"] == wahr)

setze selection_decomposed_text auf "éX"
setze caret_decomposed auf auswahl_plan(selection_decomposed_text, plan_auto, 3, 3)
pruef("caret-decomposed-valid", caret_decomposed["cluster_start"] == 1 und caret_decomposed["cluster_end"] == 1 und caret_decomposed["advance_start"] == 4 und caret_decomposed["advance_end"] == 4 und caret_decomposed["collapsed"] == wahr)
pruef("selection-decomposed-internal-rejected", auswahl_plan(selection_decomposed_text, plan_auto, 1, 3) == nichts und auswahl_plan(selection_decomposed_text, plan_auto, 2, 3) == nichts)

setze selection_family_plus auf family_text + "A"
setze selection_family auf auswahl_plan(selection_family_plus, plan_auto, 25, 26)
pruef("selection-zwj-valid", selection_family["cluster_start"] == 1 und selection_family["cluster_end"] == 2 und selection_family["advance_start"] == 4 und selection_family["advance_end"] == 8)
pruef("selection-zwj-internal-rejected", auswahl_plan(selection_family_plus, plan_auto, 4, 25) == nichts)

setze selection_ri_text auf "🇩🇪A"
setze selection_ri auf auswahl_plan(selection_ri_text, plan_auto, 8, 9)
pruef("selection-ri-valid", selection_ri["cluster_start"] == 1 und selection_ri["cluster_end"] == 2 und selection_ri["advance_start"] == 4 und selection_ri["advance_end"] == 8)
pruef("selection-ri-internal-rejected", auswahl_plan(selection_ri_text, plan_auto, 4, 8) == nichts)

setze selection_mixed auf auswahl_plan(mixed_text, plan_auto, 1, 5)
pruef("selection-mixed-rtl-remains-logical", selection_mixed["order"] == "logical" und selection_mixed["full_bidi"] == falsch und selection_mixed["cluster_start"] == 1 und selection_mixed["cluster_end"] == 3 und selection_mixed["advance_start"] == 4 und selection_mixed["advance_end"] == 12)
setze selection_malformed auf auswahl_plan(kaputt_mixed, plan_auto, 1, 3)
pruef("selection-malformed-resync", selection_malformed["source_unit"] == "utf8-bytes" und selection_malformed["cluster_start"] == 1 und selection_malformed["cluster_end"] == 3 und selection_malformed["advance_start"] == 4 und selection_malformed["advance_end"] == 12)

pruef("selection-invalid-text-domain-fail-closed", auswahl_plan(nichts, plan_auto, 0, 0) == nichts und auswahl_plan({}, plan_auto, 0, 0) == nichts)
pruef("selection-invalid-offset-type-fraction", auswahl_plan("ABC", plan_auto, "0", 1) == nichts und auswahl_plan("ABC", plan_auto, 0.5, 1) == nichts und auswahl_plan("ABC", plan_auto, 0, wahr) == nichts)
pruef("selection-invalid-range-reverse", auswahl_plan("ABC", plan_auto, -1, 1) == nichts und auswahl_plan("ABC", plan_auto, 0, 4) == nichts und auswahl_plan("ABC", plan_auto, 2, 1) == nichts)
pruef("selection-invalid-options", auswahl_plan("ABC", {}, 0, 1) == nichts und auswahl_plan("ABC", "ungueltig", 0, 1) == nichts und auswahl_plan("ABC", text_plan_optionen(2, "ltr", "latin"), 0, 1) == nichts)

setze selection_scale auf auswahl_plan(selection_decomposed_text, plan_auto, 0, 4)
pruef("selection-scale-consistency", selection_scale["advance_start"] == 0 und selection_scale["advance_end"] == 8 und selection_scale["advance_end"] * capability_b["scale_below"] == _uim_zf_text_breite(text_k, text_z, selection_decomposed_text, 11) und selection_scale["advance_end"] * capability_b["scale_at_or_above"] == _uim_zf_text_breite(text_k, text_z, selection_decomposed_text, 12))

# B7: Inverses logisches Hit-Testing. Ganzzahlige unskalierte Advances
# werden auf die naechste bounded-v1-Caretgrenze abgebildet; Gleichstand
# am 2-Pixel-Mittelpunkt geht deterministisch zur folgenden Grenze.
funktion caret_treffer(s, optionen, advance):
    gib_zurück uim_text_caret_treffer(s, optionen, advance)

setze hit_empty_neg auf caret_treffer("", plan_auto, -9)
setze hit_empty_zero auf caret_treffer("", plan_auto, 0)
setze hit_empty_rechts auf caret_treffer("", plan_auto, 9)
pruef("caret-hit-empty-clamps", hit_empty_neg["selection_start"] == 0 und hit_empty_neg["cluster_start"] == 0 und hit_empty_zero["selection_start"] == 0 und hit_empty_zero["advance_start"] == 0 und hit_empty_rechts["selection_start"] == 0 und hit_empty_rechts["cluster_start"] == 0)

setze hit_ascii_1 auf caret_treffer("ABC", plan_auto, 1)
pruef("caret-hit-ascii-1-left", hit_ascii_1["selection_start"] == 0 und hit_ascii_1["selection_end"] == 0 und hit_ascii_1["cluster_start"] == 0 und hit_ascii_1["advance_start"] == 0 und hit_ascii_1["collapsed"] == wahr)
setze hit_ascii_2 auf caret_treffer("ABC", plan_auto, 2)
pruef("caret-hit-ascii-2-midpoint-forward", typ_von(hit_ascii_2) == "Woerterbuch" und länge(hit_ascii_2) == 12 und hit_ascii_2["version"] == 1 und hit_ascii_2["source_unit"] == "utf8-bytes" und hit_ascii_2["cluster_model"] == "bounded-v1" und hit_ascii_2["order"] == "logical" und hit_ascii_2["full_bidi"] == falsch und hit_ascii_2["selection_start"] == 1 und hit_ascii_2["selection_end"] == 1 und hit_ascii_2["cluster_start"] == 1 und hit_ascii_2["cluster_end"] == 1 und hit_ascii_2["advance_start"] == 4 und hit_ascii_2["advance_end"] == 4 und hit_ascii_2["collapsed"] == wahr)
setze hit_ascii_3 auf caret_treffer("ABC", plan_auto, 3)
pruef("caret-hit-ascii-3-right", hit_ascii_3["selection_start"] == 1 und hit_ascii_3["cluster_start"] == 1 und hit_ascii_3["advance_start"] == 4)
setze hit_ascii_6 auf caret_treffer("ABC", plan_auto, 6)
pruef("caret-hit-ascii-next-midpoint-forward", hit_ascii_6["selection_start"] == 2 und hit_ascii_6["cluster_start"] == 2 und hit_ascii_6["advance_start"] == 8)
setze hit_ascii_end auf caret_treffer("ABC", plan_auto, 12)
setze hit_ascii_after auf caret_treffer("ABC", plan_auto, 99)
pruef("caret-hit-ascii-end-clamp", hit_ascii_end["selection_start"] == 3 und hit_ascii_end["cluster_start"] == 3 und hit_ascii_end["advance_start"] == 12 und hit_ascii_after["selection_start"] == 3 und hit_ascii_after["cluster_start"] == 3 und hit_ascii_after["advance_start"] == 12)

setze hit_round0 auf caret_treffer("ABC", plan_auto, 0)
setze hit_round1 auf caret_treffer("ABC", plan_auto, 4)
setze hit_round2 auf caret_treffer("ABC", plan_auto, 8)
setze hit_round3 auf caret_treffer("ABC", plan_auto, 12)
setze selection_round0 auf auswahl_plan("ABC", plan_auto, 0, 0)
setze selection_round1 auf auswahl_plan("ABC", plan_auto, 1, 1)
setze selection_round2 auf auswahl_plan("ABC", plan_auto, 2, 2)
setze selection_round3 auf auswahl_plan("ABC", plan_auto, 3, 3)
pruef("caret-hit-b6-roundtrip", hit_round0["selection_start"] == selection_round0["selection_start"] und hit_round1["selection_start"] == selection_round1["selection_start"] und hit_round2["selection_start"] == selection_round2["selection_start"] und hit_round3["selection_start"] == selection_round3["selection_start"])

setze hit_decomposed auf caret_treffer(selection_decomposed_text, plan_auto, 2)
pruef("caret-hit-decomposed-cluster", hit_decomposed["selection_start"] == 3 und hit_decomposed["cluster_start"] == 1 und hit_decomposed["advance_start"] == 4)
setze hit_family auf caret_treffer(selection_family_plus, plan_auto, 2)
pruef("caret-hit-zwj-cluster", hit_family["selection_start"] == 25 und hit_family["cluster_start"] == 1 und hit_family["advance_start"] == 4)
setze hit_ri auf caret_treffer(selection_ri_text, plan_auto, 2)
pruef("caret-hit-ri-cluster", hit_ri["selection_start"] == 8 und hit_ri["cluster_start"] == 1 und hit_ri["advance_start"] == 4)
setze hit_mixed auf caret_treffer(mixed_text, plan_auto, 2)
pruef("caret-hit-mixed-remains-logical", hit_mixed["selection_start"] == 1 und hit_mixed["cluster_start"] == 1 und hit_mixed["order"] == "logical" und hit_mixed["full_bidi"] == falsch)
setze hit_malformed_1 auf caret_treffer(kaputt_mixed, plan_auto, 2)
setze hit_malformed_2 auf caret_treffer(kaputt_mixed, plan_auto, 6)
pruef("caret-hit-malformed-bytes-resync", hit_malformed_1["selection_start"] == 1 und hit_malformed_1["cluster_start"] == 1 und hit_malformed_2["selection_start"] == 2 und hit_malformed_2["cluster_start"] == 2)

setze hit_fresh_a auf caret_treffer("ABC", plan_auto, 2)
setze hit_fresh_b auf caret_treffer("ABC", plan_auto, 2)
hit_fresh_a["selection_start"] = 99
pruef("caret-hit-fresh-copy", hit_fresh_a["selection_start"] == 99 und hit_fresh_b["selection_start"] == 1 und hit_fresh_b["order"] == "logical")
pruef("caret-hit-invalid-domain-options", caret_treffer(nichts, plan_auto, 0) == nichts und caret_treffer({}, plan_auto, 0) == nichts und caret_treffer("ABC", {}, 0) == nichts und caret_treffer("ABC", "ungueltig", 0) == nichts und caret_treffer("ABC", text_plan_optionen(2, "ltr", "latin"), 0) == nichts)
pruef("caret-hit-invalid-advance", caret_treffer("ABC", plan_auto, "1") == nichts und caret_treffer("ABC", plan_auto, 0.5) == nichts und caret_treffer("ABC", plan_auto, wahr) == nichts)
pruef("caret-hit-scale-consistency", hit_decomposed["advance_start"] * capability_b["scale_below"] == _uim_zf_text_breite(text_k, text_z, "é", 11) und hit_decomposed["advance_start"] * capability_b["scale_at_or_above"] == _uim_zf_text_breite(text_k, text_z, "é", 12))

# B8: Begrenztes logisches Caret-Schreiten auf denselben bounded-v1-
# Clustergrenzen. Das ist bewusst keine visuelle BiDi-Navigation.
funktion caret_schritt(s, optionen, offset, delta):
    gib_zurück uim_text_caret_schritt(s, optionen, offset, delta)

setze step_empty_neg auf caret_schritt("", plan_auto, 0, -9)
setze step_empty_zero auf caret_schritt("", plan_auto, 0, 0)
setze step_empty_pos auf caret_schritt("", plan_auto, 0, 9)
pruef("caret-step-empty-clamps", step_empty_neg["selection_start"] == 0 und step_empty_zero["selection_start"] == 0 und step_empty_pos["selection_start"] == 0 und step_empty_pos["cluster_start"] == 0 und step_empty_pos["collapsed"] == wahr)

setze step_ascii_schema auf caret_schritt("ABC", plan_auto, 0, 1)
pruef("caret-step-schema-exact12", typ_von(step_ascii_schema) == "Woerterbuch" und länge(step_ascii_schema) == 12 und step_ascii_schema["version"] == 1 und step_ascii_schema["source_unit"] == "utf8-bytes" und step_ascii_schema["cluster_model"] == "bounded-v1" und step_ascii_schema["order"] == "logical" und step_ascii_schema["full_bidi"] == falsch und step_ascii_schema["selection_start"] == 1 und step_ascii_schema["selection_end"] == 1 und step_ascii_schema["cluster_start"] == 1 und step_ascii_schema["cluster_end"] == 1 und step_ascii_schema["advance_start"] == 4 und step_ascii_schema["advance_end"] == 4 und step_ascii_schema["collapsed"] == wahr)
setze step_ascii_zero auf caret_schritt("ABC", plan_auto, 2, 0)
pruef("caret-step-zero-noop", step_ascii_zero["selection_start"] == 2 und step_ascii_zero["cluster_start"] == 2 und step_ascii_zero["advance_start"] == 8)
setze step_ascii_pos auf caret_schritt("ABC", plan_auto, 1, 1)
setze step_ascii_pos_multi auf caret_schritt("ABC", plan_auto, 1, 2)
setze step_ascii_pos_clamp auf caret_schritt("ABC", plan_auto, 1, 99)
pruef("caret-step-ascii-positive", step_ascii_pos["selection_start"] == 2 und step_ascii_pos_multi["selection_start"] == 3 und step_ascii_pos_clamp["selection_start"] == 3)
setze step_ascii_neg auf caret_schritt("ABC", plan_auto, 2, -1)
setze step_ascii_neg_multi auf caret_schritt("ABC", plan_auto, 2, -2)
setze step_ascii_neg_clamp auf caret_schritt("ABC", plan_auto, 2, -99)
pruef("caret-step-ascii-negative", step_ascii_neg["selection_start"] == 1 und step_ascii_neg_multi["selection_start"] == 0 und step_ascii_neg_clamp["selection_start"] == 0)

setze step_decomp_vor auf caret_schritt(selection_decomposed_text, plan_auto, 0, 1)
setze step_decomp_zurueck auf caret_schritt(selection_decomposed_text, plan_auto, 4, -1)
pruef("caret-step-decomposed-both-ways", step_decomp_vor["selection_start"] == 3 und step_decomp_vor["cluster_start"] == 1 und step_decomp_zurueck["selection_start"] == 3 und step_decomp_zurueck["cluster_start"] == 1)
setze step_decomp_links auf caret_schritt(selection_decomposed_text, plan_auto, 3, -99)
setze step_decomp_rechts auf caret_schritt(selection_decomposed_text, plan_auto, 3, 99)
pruef("caret-step-decomposed-clamps", step_decomp_links["selection_start"] == 0 und step_decomp_rechts["selection_start"] == 4)
setze step_family_vor auf caret_schritt(selection_family_plus, plan_auto, 0, 1)
setze step_family_zurueck auf caret_schritt(selection_family_plus, plan_auto, 26, -1)
pruef("caret-step-zwj-boundary", step_family_vor["selection_start"] == 25 und step_family_vor["cluster_start"] == 1 und step_family_zurueck["selection_start"] == 25)
setze step_ri_vor auf caret_schritt(selection_ri_text, plan_auto, 0, 1)
setze step_ri_zurueck auf caret_schritt(selection_ri_text, plan_auto, 9, -1)
pruef("caret-step-ri-boundary", step_ri_vor["selection_start"] == 8 und step_ri_vor["cluster_start"] == 1 und step_ri_zurueck["selection_start"] == 8)
setze step_mixed_vor auf caret_schritt(mixed_text, plan_auto, 1, 1)
setze step_mixed_zurueck auf caret_schritt(mixed_text, plan_auto, 3, -1)
pruef("caret-step-mixed-remains-logical", step_mixed_vor["selection_start"] == 3 und step_mixed_vor["cluster_start"] == 2 und step_mixed_vor["order"] == "logical" und step_mixed_vor["full_bidi"] == falsch und step_mixed_zurueck["selection_start"] == 1)
setze step_malformed_vor auf caret_schritt(kaputt_mixed, plan_auto, 1, 2)
setze step_malformed_zurueck auf caret_schritt(kaputt_mixed, plan_auto, 3, -2)
pruef("caret-step-malformed-resync", step_malformed_vor["selection_start"] == 3 und step_malformed_vor["cluster_start"] == 3 und step_malformed_zurueck["selection_start"] == 1 und step_malformed_zurueck["cluster_start"] == 1)

setze step_round_b6 auf caret_schritt("ABC", plan_auto, selection_round2["selection_start"], 0)
setze step_round_b7 auf caret_schritt("ABC", plan_auto, hit_round1["selection_start"], 1)
pruef("caret-step-b6-b7-roundtrip", step_round_b6["selection_start"] == selection_round2["selection_start"] und step_round_b6["advance_start"] == selection_round2["advance_start"] und step_round_b7["selection_start"] == hit_round2["selection_start"] und step_round_b7["advance_start"] == hit_round2["advance_start"])
setze step_fresh_a auf caret_schritt("ABC", plan_auto, 1, 1)
setze step_fresh_b auf caret_schritt("ABC", plan_auto, 1, 1)
step_fresh_a["selection_start"] = 99
pruef("caret-step-fresh-copy", step_fresh_a["selection_start"] == 99 und step_fresh_b["selection_start"] == 2 und step_fresh_b["order"] == "logical")
pruef("caret-step-invalid-domain-options", caret_schritt(nichts, plan_auto, 0, 1) == nichts und caret_schritt({}, plan_auto, 0, 1) == nichts und caret_schritt("ABC", {}, 0, 1) == nichts und caret_schritt("ABC", "ungueltig", 0, 1) == nichts und caret_schritt("ABC", text_plan_optionen(2, "ltr", "latin"), 0, 1) == nichts)
pruef("caret-step-invalid-offset", caret_schritt("ABC", plan_auto, -1, 1) == nichts und caret_schritt("ABC", plan_auto, 4, 1) == nichts und caret_schritt(selection_decomposed_text, plan_auto, 1, 1) == nichts und caret_schritt(selection_decomposed_text, plan_auto, 2, 1) == nichts und caret_schritt("ABC", plan_auto, "1", 1) == nichts und caret_schritt("ABC", plan_auto, 0.5, 1) == nichts und caret_schritt("ABC", plan_auto, wahr, 1) == nichts)
pruef("caret-step-invalid-delta", caret_schritt("ABC", plan_auto, 0, "1") == nichts und caret_schritt("ABC", plan_auto, 0, 0.5) == nichts und caret_schritt("ABC", plan_auto, 0, wahr) == nichts)

# B9: Pixel-skaliertes Caret-Hit-Testing auf der tatsaechlichen
# eingebetteten 3x5-Skalierung. Kein DPI-/Subpixel- oder visuelles BiDi.
funktion caret_treffer_px(s, optionen, groesse, advance_px):
    gib_zurück uim_text_caret_treffer_px(s, optionen, groesse, advance_px)

setze px_empty_1 auf caret_treffer_px("", plan_auto, 11, -9)
setze px_empty_2 auf caret_treffer_px("", plan_auto, 12, 9)
pruef("caret-px-empty-clamps", px_empty_1["selection_start"] == 0 und px_empty_1["scale"] == 1 und px_empty_1["pixel_advance_start"] == 0 und px_empty_2["selection_start"] == 0 und px_empty_2["scale"] == 2 und px_empty_2["pixel_advance_end"] == 0)

setze px_schema auf caret_treffer_px("ABC", plan_auto, 11, 2)
pruef("caret-px-schema-exact15", typ_von(px_schema) == "Woerterbuch" und länge(px_schema) == 15 und px_schema["version"] == 1 und px_schema["source_unit"] == "utf8-bytes" und px_schema["cluster_model"] == "bounded-v1" und px_schema["order"] == "logical" und px_schema["full_bidi"] == falsch und px_schema["selection_start"] == 1 und px_schema["selection_end"] == 1 und px_schema["cluster_start"] == 1 und px_schema["cluster_end"] == 1 und px_schema["advance_start"] == 4 und px_schema["advance_end"] == 4 und px_schema["collapsed"] == wahr und px_schema["scale"] == 1 und px_schema["pixel_advance_start"] == 4 und px_schema["pixel_advance_end"] == 4)

setze px_frac auf caret_treffer_px("ABC", plan_auto, 11.999, 2)
pruef("caret-px-fractional-size-below-threshold", px_frac["scale"] == 1 und px_frac["selection_start"] == 1 und px_frac["pixel_advance_start"] == 4)
setze px_s1_left auf caret_treffer_px("ABC", plan_auto, 11, 1)
setze px_s1_mid auf caret_treffer_px("ABC", plan_auto, 11, 2)
setze px_s1_right auf caret_treffer_px("ABC", plan_auto, 11, 3)
pruef("caret-px-scale1-midpoint-forward", px_s1_left["selection_start"] == 0 und px_s1_mid["selection_start"] == 1 und px_s1_right["selection_start"] == 1)
setze px_s2_left auf caret_treffer_px("ABC", plan_auto, 12, 3)
setze px_s2_mid auf caret_treffer_px("ABC", plan_auto, 12, 4)
setze px_s2_right auf caret_treffer_px("ABC", plan_auto, 12, 5)
pruef("caret-px-scale2-midpoint-forward", px_s2_left["selection_start"] == 0 und px_s2_left["pixel_advance_start"] == 0 und px_s2_mid["selection_start"] == 1 und px_s2_mid["pixel_advance_start"] == 8 und px_s2_right["selection_start"] == 1)
setze px_s2_next auf caret_treffer_px("ABC", plan_auto, 12, 12)
pruef("caret-px-scale2-next-midpoint", px_s2_next["selection_start"] == 2 und px_s2_next["cluster_start"] == 2 und px_s2_next["advance_start"] == 8 und px_s2_next["pixel_advance_start"] == 16)
setze px_large_size auf caret_treffer_px("ABC", plan_auto, 99, 4)
pruef("caret-px-large-size-same-scale2", px_large_size["scale"] == 2 und px_large_size["selection_start"] == 1 und px_large_size["pixel_advance_start"] == 8)
setze px_left_clamp auf caret_treffer_px("ABC", plan_auto, 11, -99)
setze px_right_clamp auf caret_treffer_px("ABC", plan_auto, 12, 99)
pruef("caret-px-clamps", px_left_clamp["selection_start"] == 0 und px_left_clamp["pixel_advance_start"] == 0 und px_right_clamp["selection_start"] == 3 und px_right_clamp["advance_start"] == 12 und px_right_clamp["pixel_advance_start"] == 24)

setze px_decomp auf caret_treffer_px(selection_decomposed_text, plan_auto, 12, 4)
pruef("caret-px-decomposed-cluster", px_decomp["selection_start"] == 3 und px_decomp["cluster_start"] == 1 und px_decomp["advance_start"] == 4 und px_decomp["pixel_advance_start"] == 8)
setze px_zwj auf caret_treffer_px(selection_family_plus, plan_auto, 12, 4)
pruef("caret-px-zwj-cluster", px_zwj["selection_start"] == 25 und px_zwj["cluster_start"] == 1 und px_zwj["pixel_advance_start"] == 8)
setze px_ri auf caret_treffer_px(selection_ri_text, plan_auto, 12, 4)
pruef("caret-px-ri-cluster", px_ri["selection_start"] == 8 und px_ri["cluster_start"] == 1 und px_ri["pixel_advance_start"] == 8)
setze px_mixed auf caret_treffer_px(mixed_text, plan_auto, 12, 4)
pruef("caret-px-mixed-remains-logical", px_mixed["selection_start"] == 1 und px_mixed["order"] == "logical" und px_mixed["full_bidi"] == falsch und px_mixed["scale"] == 2)
setze px_malformed_1 auf caret_treffer_px(kaputt_mixed, plan_auto, 12, 4)
setze px_malformed_2 auf caret_treffer_px(kaputt_mixed, plan_auto, 12, 12)
pruef("caret-px-malformed-resync", px_malformed_1["selection_start"] == 1 und px_malformed_1["cluster_start"] == 1 und px_malformed_1["advance_start"] == 4 und px_malformed_1["pixel_advance_start"] == 8 und px_malformed_2["selection_start"] == 2 und px_malformed_2["cluster_start"] == 2 und px_malformed_2["advance_start"] == 8 und px_malformed_2["pixel_advance_start"] == 16 und px_malformed_2["scale"] == 2)

setze px_round_b7 auf caret_treffer_px("ABC", plan_auto, 11, hit_round2["advance_start"])
setze px_round_b6 auf caret_treffer_px("ABC", plan_auto, 12, selection_round2["advance_start"] * 2)
pruef("caret-px-b6-b7-roundtrip", px_round_b7["selection_start"] == hit_round2["selection_start"] und px_round_b7["advance_start"] == hit_round2["advance_start"] und px_round_b7["pixel_advance_start"] == 8 und px_round_b6["selection_start"] == selection_round2["selection_start"] und px_round_b6["advance_start"] == selection_round2["advance_start"] und px_round_b6["pixel_advance_start"] == 16)
setze px_fresh_a auf caret_treffer_px("ABC", plan_auto, 12, 4)
setze px_fresh_b auf caret_treffer_px("ABC", plan_auto, 12, 4)
px_fresh_a["pixel_advance_start"] = 99
pruef("caret-px-fresh-copy", px_fresh_a["pixel_advance_start"] == 99 und px_fresh_b["pixel_advance_start"] == 8 und px_fresh_b["selection_start"] == 1)
pruef("caret-px-invalid-domain-options", caret_treffer_px(nichts, plan_auto, 11, 0) == nichts und caret_treffer_px({}, plan_auto, 11, 0) == nichts und caret_treffer_px("ABC", {}, 11, 0) == nichts und caret_treffer_px("ABC", "ungueltig", 11, 0) == nichts und caret_treffer_px("ABC", text_plan_optionen(2, "ltr", "latin"), 11, 0) == nichts)
pruef("caret-px-invalid-size-advance", caret_treffer_px("ABC", plan_auto, 0, 0) == nichts und caret_treffer_px("ABC", plan_auto, -1, 0) == nichts und caret_treffer_px("ABC", plan_auto, "12", 0) == nichts und caret_treffer_px("ABC", plan_auto, wahr, 0) == nichts und caret_treffer_px("ABC", plan_auto, 12, "1") == nichts und caret_treffer_px("ABC", plan_auto, 12, 0.5) == nichts und caret_treffer_px("ABC", plan_auto, 12, wahr) == nichts)

# B10: Pixel-skalierte logische Selection-Spannen. Die Byte-/Cluster-
# Validierung bleibt vollstaendig beim bestehenden B6-Vertrag.
funktion auswahl_plan_px(s, optionen, groesse, start, ende):
    gib_zurück uim_text_auswahl_plan_px(s, optionen, groesse, start, ende)

setze selection_px_empty_1 auf auswahl_plan_px("", plan_auto, 11, 0, 0)
setze selection_px_empty_2 auf auswahl_plan_px("", plan_auto, 12, 0, 0)
pruef("selection-px-empty-scales", selection_px_empty_1["selection_start"] == 0 und selection_px_empty_1["selection_end"] == 0 und selection_px_empty_1["scale"] == 1 und selection_px_empty_1["pixel_advance_start"] == 0 und selection_px_empty_2["collapsed"] == wahr und selection_px_empty_2["scale"] == 2 und selection_px_empty_2["pixel_advance_end"] == 0)

setze selection_px_ascii auf auswahl_plan_px("ABC", plan_auto, 11, 1, 3)
pruef("selection-px-ascii-schema-exact15-range", typ_von(selection_px_ascii) == "Woerterbuch" und länge(selection_px_ascii) == 15 und selection_px_ascii["version"] == 1 und selection_px_ascii["source_unit"] == "utf8-bytes" und selection_px_ascii["cluster_model"] == "bounded-v1" und selection_px_ascii["order"] == "logical" und selection_px_ascii["full_bidi"] == falsch und selection_px_ascii["selection_start"] == 1 und selection_px_ascii["selection_end"] == 3 und selection_px_ascii["cluster_start"] == 1 und selection_px_ascii["cluster_end"] == 3 und selection_px_ascii["advance_start"] == 4 und selection_px_ascii["advance_end"] == 12 und selection_px_ascii["collapsed"] == falsch und selection_px_ascii["scale"] == 1 und selection_px_ascii["pixel_advance_start"] == 4 und selection_px_ascii["pixel_advance_end"] == 12)

setze selection_px_frac auf auswahl_plan_px("ABC", plan_auto, 11.999, 0, 3)
setze selection_px_threshold auf auswahl_plan_px("ABC", plan_auto, 12, 0, 3)
pruef("selection-px-threshold-11-999-vs12", selection_px_frac["scale"] == 1 und selection_px_frac["pixel_advance_end"] == 12 und selection_px_threshold["scale"] == 2 und selection_px_threshold["pixel_advance_end"] == 24)
setze selection_px_large auf auswahl_plan_px("ABC", plan_auto, 99, 1, 2)
pruef("selection-px-large-size-scale2", selection_px_large["scale"] == 2 und selection_px_large["advance_start"] == 4 und selection_px_large["advance_end"] == 8 und selection_px_large["pixel_advance_start"] == 8 und selection_px_large["pixel_advance_end"] == 16)

setze selection_px_b9_a auf caret_treffer_px("ABC", plan_auto, 12, 4)
setze selection_px_b9_b auf caret_treffer_px("ABC", plan_auto, 12, 4)
setze selection_px_collapsed auf auswahl_plan_px("ABC", plan_auto, 12, 1, 1)
selection_px_b9_a["pixel_advance_start"] = 99
pruef("selection-px-collapsed-b9-exact15-fresh-equality", länge(selection_px_b9_a) == 15 und länge(selection_px_b9_b) == 15 und selection_px_b9_a["pixel_advance_start"] == 99 und selection_px_b9_b["pixel_advance_start"] == 8 und selection_px_b9_b["collapsed"] == wahr und selection_px_collapsed["collapsed"] == wahr und selection_px_collapsed["selection_start"] == selection_px_b9_b["selection_start"] und selection_px_collapsed["selection_end"] == selection_px_b9_b["selection_end"] und selection_px_collapsed["pixel_advance_start"] == selection_px_b9_b["pixel_advance_start"] und selection_px_collapsed["pixel_advance_end"] == selection_px_b9_b["pixel_advance_end"])

setze selection_px_decomposed auf auswahl_plan_px(selection_decomposed_text, plan_auto, 12, 0, 3)
pruef("selection-px-decomposed", selection_px_decomposed["cluster_start"] == 0 und selection_px_decomposed["cluster_end"] == 1 und selection_px_decomposed["advance_start"] == 0 und selection_px_decomposed["advance_end"] == 4 und selection_px_decomposed["pixel_advance_end"] == 8)
setze selection_px_zwj auf auswahl_plan_px(selection_family_plus, plan_auto, 12, 0, 25)
pruef("selection-px-zwj", selection_px_zwj["selection_end"] == 25 und selection_px_zwj["cluster_start"] == 0 und selection_px_zwj["cluster_end"] == 1 und selection_px_zwj["pixel_advance_start"] == 0 und selection_px_zwj["pixel_advance_end"] == 8)
setze selection_px_ri auf auswahl_plan_px(selection_ri_text, plan_auto, 12, 0, 8)
pruef("selection-px-ri", selection_px_ri["selection_end"] == 8 und selection_px_ri["cluster_start"] == 0 und selection_px_ri["cluster_end"] == 1 und selection_px_ri["pixel_advance_end"] == 8)
setze selection_px_mixed auf auswahl_plan_px(mixed_text, plan_auto, 12, 1, 5)
pruef("selection-px-mixed-logical", selection_px_mixed["order"] == "logical" und selection_px_mixed["full_bidi"] == falsch und selection_px_mixed["cluster_start"] == 1 und selection_px_mixed["cluster_end"] == 3 und selection_px_mixed["pixel_advance_start"] == 8 und selection_px_mixed["pixel_advance_end"] == 24)
setze selection_px_malformed auf auswahl_plan_px(kaputt_mixed, plan_auto, 12, 1, 3)
pruef("selection-px-malformed-resync", selection_px_malformed["source_unit"] == "utf8-bytes" und selection_px_malformed["selection_start"] == 1 und selection_px_malformed["selection_end"] == 3 und selection_px_malformed["cluster_start"] == 1 und selection_px_malformed["cluster_end"] == 3 und selection_px_malformed["advance_start"] == 4 und selection_px_malformed["advance_end"] == 12 und selection_px_malformed["pixel_advance_start"] == 8 und selection_px_malformed["pixel_advance_end"] == 24)

setze selection_px_round auf auswahl_plan_px(selection_decomposed_text, plan_auto, 12, 0, 4)
pruef("selection-px-b6-width-scaled-roundtrip", selection_px_round["selection_start"] == selection_scale["selection_start"] und selection_px_round["selection_end"] == selection_scale["selection_end"] und selection_px_round["advance_start"] == selection_scale["advance_start"] und selection_px_round["advance_end"] == selection_scale["advance_end"] und selection_px_round["pixel_advance_start"] == selection_scale["advance_start"] * 2 und selection_px_round["pixel_advance_end"] == selection_scale["advance_end"] * 2 und selection_px_round["pixel_advance_end"] == _uim_zf_text_breite(text_k, text_z, selection_decomposed_text, 12))
setze selection_px_fresh_a auf auswahl_plan_px("ABC", plan_auto, 12, 1, 3)
setze selection_px_fresh_b auf auswahl_plan_px("ABC", plan_auto, 12, 1, 3)
selection_px_fresh_a["selection_end"] = 99
selection_px_fresh_a["pixel_advance_end"] = 99
pruef("selection-px-fresh-copy", selection_px_fresh_a["selection_end"] == 99 und selection_px_fresh_a["pixel_advance_end"] == 99 und selection_px_fresh_b["selection_end"] == 3 und selection_px_fresh_b["pixel_advance_end"] == 24 und länge(selection_px_fresh_b) == 15)

pruef("selection-px-invalid-domain-options-v2", auswahl_plan_px(nichts, plan_auto, 11, 0, 0) == nichts und auswahl_plan_px({}, plan_auto, 11, 0, 0) == nichts und auswahl_plan_px("ABC", {}, 11, 0, 1) == nichts und auswahl_plan_px("ABC", "ungueltig", 11, 0, 1) == nichts und auswahl_plan_px("ABC", text_plan_optionen(2, "ltr", "latin"), 11, 0, 1) == nichts)
pruef("selection-px-invalid-size", auswahl_plan_px("ABC", plan_auto, 0, 0, 1) == nichts und auswahl_plan_px("ABC", plan_auto, -1, 0, 1) == nichts und auswahl_plan_px("ABC", plan_auto, "12", 0, 1) == nichts und auswahl_plan_px("ABC", plan_auto, wahr, 0, 1) == nichts)
pruef("selection-px-invalid-offsets", auswahl_plan_px("ABC", plan_auto, 12, "0", 1) == nichts und auswahl_plan_px("ABC", plan_auto, 12, 0.5, 1) == nichts und auswahl_plan_px("ABC", plan_auto, 12, 0, wahr) == nichts und auswahl_plan_px("ABC", plan_auto, 12, -1, 1) == nichts und auswahl_plan_px("ABC", plan_auto, 12, 0, 4) == nichts und auswahl_plan_px("ABC", plan_auto, 12, 2, 1) == nichts und auswahl_plan_px(selection_decomposed_text, plan_auto, 12, 1, 3) == nichts und auswahl_plan_px(selection_decomposed_text, plan_auto, 12, 2, 3) == nichts)

# B11: Pixel-skalierter logischer Caret-Schritt. Die eigentliche
# Byte-/Cluster-Navigation bleibt vollstaendig beim bestehenden B8-Vertrag.
funktion caret_schritt_px(s, optionen, groesse, offset, delta):
    gib_zurück uim_text_caret_schritt_px(s, optionen, groesse, offset, delta)

setze step_px_empty_1 auf caret_schritt_px("", plan_auto, 11, 0, -99)
setze step_px_empty_2 auf caret_schritt_px("", plan_auto, 12, 0, 99)
pruef("caret-step-px-empty-scales-clamps", step_px_empty_1["selection_start"] == 0 und step_px_empty_1["scale"] == 1 und step_px_empty_1["pixel_advance_start"] == 0 und step_px_empty_2["selection_end"] == 0 und step_px_empty_2["collapsed"] == wahr und step_px_empty_2["scale"] == 2 und step_px_empty_2["pixel_advance_end"] == 0)

setze step_px_ascii auf caret_schritt_px("ABC", plan_auto, 11, 0, 1)
pruef("caret-step-px-ascii-schema-exact15-forward", typ_von(step_px_ascii) == "Woerterbuch" und länge(step_px_ascii) == 15 und step_px_ascii["version"] == 1 und step_px_ascii["source_unit"] == "utf8-bytes" und step_px_ascii["cluster_model"] == "bounded-v1" und step_px_ascii["order"] == "logical" und step_px_ascii["full_bidi"] == falsch und step_px_ascii["selection_start"] == 1 und step_px_ascii["selection_end"] == 1 und step_px_ascii["cluster_start"] == 1 und step_px_ascii["cluster_end"] == 1 und step_px_ascii["advance_start"] == 4 und step_px_ascii["advance_end"] == 4 und step_px_ascii["collapsed"] == wahr und step_px_ascii["scale"] == 1 und step_px_ascii["pixel_advance_start"] == 4 und step_px_ascii["pixel_advance_end"] == 4)

setze step_px_frac auf caret_schritt_px("ABC", plan_auto, 11.999, 0, 1)
setze step_px_threshold auf caret_schritt_px("ABC", plan_auto, 12, 0, 1)
pruef("caret-step-px-threshold-11-999-vs12", step_px_frac["scale"] == 1 und step_px_frac["pixel_advance_start"] == 4 und step_px_threshold["scale"] == 2 und step_px_threshold["pixel_advance_start"] == 8)
setze step_px_large auf caret_schritt_px("ABC", plan_auto, 99, 0, 1)
pruef("caret-step-px-large-size-scale2", step_px_large["scale"] == 2 und step_px_large["selection_start"] == 1 und step_px_large["advance_start"] == 4 und step_px_large["pixel_advance_start"] == 8)

setze step_px_zero auf caret_schritt_px("ABC", plan_auto, 12, 2, 0)
setze step_px_zero_b10 auf auswahl_plan_px("ABC", plan_auto, 12, 2, 2)
pruef("caret-step-px-zero-b10-collapsed", step_px_zero["selection_start"] == 2 und step_px_zero["advance_start"] == 8 und step_px_zero["pixel_advance_start"] == 16 und step_px_zero["collapsed"] == wahr und step_px_zero["selection_start"] == step_px_zero_b10["selection_start"] und step_px_zero["pixel_advance_start"] == step_px_zero_b10["pixel_advance_start"])
setze step_px_pos auf caret_schritt_px("ABC", plan_auto, 12, 1, 1)
setze step_px_pos_multi auf caret_schritt_px("ABC", plan_auto, 12, 1, 2)
setze step_px_pos_clamp auf caret_schritt_px("ABC", plan_auto, 12, 1, 99)
pruef("caret-step-px-positive-multi-right-clamp", step_px_pos["selection_start"] == 2 und step_px_pos["pixel_advance_start"] == 16 und step_px_pos_multi["selection_start"] == 3 und step_px_pos_multi["pixel_advance_start"] == 24 und step_px_pos_clamp["selection_start"] == 3 und step_px_pos_clamp["pixel_advance_start"] == 24)
setze step_px_neg auf caret_schritt_px("ABC", plan_auto, 12, 2, -1)
setze step_px_neg_multi auf caret_schritt_px("ABC", plan_auto, 12, 2, -2)
setze step_px_neg_clamp auf caret_schritt_px("ABC", plan_auto, 12, 2, -99)
pruef("caret-step-px-negative-multi-left-clamp", step_px_neg["selection_start"] == 1 und step_px_neg["pixel_advance_start"] == 8 und step_px_neg_multi["selection_start"] == 0 und step_px_neg_multi["pixel_advance_start"] == 0 und step_px_neg_clamp["selection_start"] == 0 und step_px_neg_clamp["pixel_advance_start"] == 0)

setze step_px_decomp_vor auf caret_schritt_px(selection_decomposed_text, plan_auto, 12, 0, 1)
setze step_px_decomp_zurueck auf caret_schritt_px(selection_decomposed_text, plan_auto, 12, 4, -1)
pruef("caret-step-px-decomposed", step_px_decomp_vor["selection_start"] == 3 und step_px_decomp_vor["cluster_start"] == 1 und step_px_decomp_vor["pixel_advance_start"] == 8 und step_px_decomp_zurueck["selection_start"] == 3 und step_px_decomp_zurueck["pixel_advance_start"] == 8)
setze step_px_zwj auf caret_schritt_px(selection_family_plus, plan_auto, 12, 0, 1)
pruef("caret-step-px-zwj", step_px_zwj["selection_start"] == 25 und step_px_zwj["cluster_start"] == 1 und step_px_zwj["pixel_advance_start"] == 8)
setze step_px_ri auf caret_schritt_px(selection_ri_text, plan_auto, 12, 0, 1)
pruef("caret-step-px-ri", step_px_ri["selection_start"] == 8 und step_px_ri["cluster_start"] == 1 und step_px_ri["pixel_advance_start"] == 8)
setze step_px_mixed_vor auf caret_schritt_px(mixed_text, plan_auto, 12, 1, 1)
setze step_px_mixed_zurueck auf caret_schritt_px(mixed_text, plan_auto, 12, 3, -1)
pruef("caret-step-px-mixed-logical", step_px_mixed_vor["selection_start"] == 3 und step_px_mixed_vor["cluster_start"] == 2 und step_px_mixed_vor["pixel_advance_start"] == 16 und step_px_mixed_vor["order"] == "logical" und step_px_mixed_vor["full_bidi"] == falsch und step_px_mixed_zurueck["selection_start"] == 1 und step_px_mixed_zurueck["pixel_advance_start"] == 8)
setze step_px_malformed_vor auf caret_schritt_px(kaputt_mixed, plan_auto, 12, 1, 2)
setze step_px_malformed_zurueck auf caret_schritt_px(kaputt_mixed, plan_auto, 12, 3, -2)
pruef("caret-step-px-malformed-resync", step_px_malformed_vor["selection_start"] == 3 und step_px_malformed_vor["cluster_start"] == 3 und step_px_malformed_vor["advance_start"] == 12 und step_px_malformed_vor["pixel_advance_start"] == 24 und step_px_malformed_zurueck["selection_start"] == 1 und step_px_malformed_zurueck["cluster_start"] == 1 und step_px_malformed_zurueck["pixel_advance_start"] == 8)

setze step_px_cross_a auf caret_schritt_px("ABC", plan_auto, 12, 1, 1)
setze step_px_cross_b auf caret_schritt_px("ABC", plan_auto, 12, 1, 1)
setze step_px_cross_b8 auf caret_schritt("ABC", plan_auto, 1, 1)
setze step_px_cross_b9 auf caret_treffer_px("ABC", plan_auto, 12, 12)
setze step_px_cross_b10 auf auswahl_plan_px("ABC", plan_auto, 12, 2, 2)
step_px_cross_a["pixel_advance_start"] = 99
pruef("caret-step-px-b8-b9-b10-roundtrip-fresh", länge(step_px_cross_a) == 15 und länge(step_px_cross_b) == 15 und step_px_cross_a["pixel_advance_start"] == 99 und step_px_cross_b["pixel_advance_start"] == 16 und step_px_cross_b["selection_start"] == step_px_cross_b8["selection_start"] und step_px_cross_b["advance_start"] == step_px_cross_b8["advance_start"] und step_px_cross_b["selection_start"] == step_px_cross_b9["selection_start"] und step_px_cross_b["pixel_advance_start"] == step_px_cross_b9["pixel_advance_start"] und step_px_cross_b["selection_start"] == step_px_cross_b10["selection_start"] und step_px_cross_b["pixel_advance_start"] == step_px_cross_b10["pixel_advance_start"])

pruef("caret-step-px-invalid-domain-options-size", caret_schritt_px(nichts, plan_auto, 11, 0, 1) == nichts und caret_schritt_px({}, plan_auto, 11, 0, 1) == nichts und caret_schritt_px("ABC", {}, 11, 0, 1) == nichts und caret_schritt_px("ABC", "ungueltig", 11, 0, 1) == nichts und caret_schritt_px("ABC", text_plan_optionen(2, "ltr", "latin"), 11, 0, 1) == nichts und caret_schritt_px("ABC", plan_auto, 0, 0, 1) == nichts und caret_schritt_px("ABC", plan_auto, -1, 0, 1) == nichts und caret_schritt_px("ABC", plan_auto, "12", 0, 1) == nichts und caret_schritt_px("ABC", plan_auto, wahr, 0, 1) == nichts)
pruef("caret-step-px-invalid-offset-delta", caret_schritt_px("ABC", plan_auto, 12, "0", 1) == nichts und caret_schritt_px("ABC", plan_auto, 12, 0.5, 1) == nichts und caret_schritt_px("ABC", plan_auto, 12, 0, wahr) == nichts und caret_schritt_px("ABC", plan_auto, 12, -1, 1) == nichts und caret_schritt_px("ABC", plan_auto, 12, 4, 1) == nichts und caret_schritt_px(selection_decomposed_text, plan_auto, 12, 1, 1) == nichts und caret_schritt_px(selection_decomposed_text, plan_auto, 12, 2, 1) == nichts und caret_schritt_px("ABC", plan_auto, 12, 0, "1") == nichts und caret_schritt_px("ABC", plan_auto, 12, 0, 0.5) == nichts und caret_schritt_px("ABC", plan_auto, 12, wahr, 1) == nichts)

# B12: Pixel-skalierter logischer Textplan. Alle Glyph-/Run-/Fallback-
# Aussagen stammen unverändert aus B4; ergänzt wird nur die reale 3x5-Skalierung.
funktion text_plan_px(s, optionen, groesse):
    gib_zurück uim_text_plan_px(s, optionen, groesse)

setze plan_px_empty_1 auf text_plan_px("", plan_auto, 11)
setze plan_px_empty_2 auf text_plan_px("", plan_auto, 12)
pruef("text-plan-px-empty-scales", länge(plan_px_empty_1) == 15 und länge(plan_px_empty_1["glyphs"]) == 0 und länge(plan_px_empty_1["runs"]) == 0 und plan_px_empty_1["advance"] == 0 und plan_px_empty_1["scale"] == 1 und plan_px_empty_1["pixel_advance"] == 0 und länge(plan_px_empty_2) == 15 und plan_px_empty_2["advance"] == 0 und plan_px_empty_2["scale"] == 2 und plan_px_empty_2["pixel_advance"] == 0)

setze plan_px_ascii auf text_plan_px("AB", plan_ltr, 11)
pruef("text-plan-px-ascii-schema-exact15", typ_von(plan_px_ascii) == "Woerterbuch" und länge(plan_px_ascii) == 15 und plan_px_ascii["version"] == 1 und plan_px_ascii["order"] == "logical" und plan_px_ascii["source_unit"] == "utf8-bytes" und plan_px_ascii["cluster_model"] == "bounded-v1" und plan_px_ascii["full_bidi"] == falsch und plan_px_ascii["full_shaping"] == falsch und plan_px_ascii["direction"] == "ltr" und plan_px_ascii["script"] == "latin" und plan_px_ascii["supported"] == wahr und plan_px_ascii["fallback"] == "none" und länge(plan_px_ascii["glyphs"]) == 2 und plan_px_ascii["glyphs"][0] == "A" und plan_px_ascii["glyphs"][1] == "B" und plan_px_ascii["advance"] == 8 und länge(plan_px_ascii["runs"]) == 1 und plan_px_ascii["scale"] == 1 und plan_px_ascii["pixel_advance"] == 8)

setze plan_px_frac auf text_plan_px("ABC", plan_auto, 11.999)
setze plan_px_threshold auf text_plan_px("ABC", plan_auto, 12)
pruef("text-plan-px-threshold-11-999-vs12", plan_px_frac["advance"] == 12 und plan_px_frac["scale"] == 1 und plan_px_frac["pixel_advance"] == 12 und plan_px_threshold["advance"] == 12 und plan_px_threshold["scale"] == 2 und plan_px_threshold["pixel_advance"] == 24)
setze plan_px_large auf text_plan_px("ABC", plan_auto, 99)
pruef("text-plan-px-large-size-scale2", plan_px_large["advance"] == 12 und plan_px_large["scale"] == 2 und plan_px_large["pixel_advance"] == 24)

setze plan_px_decomp_basis auf uim_text_plan(selection_decomposed_text, plan_auto)
setze plan_px_decomp auf text_plan_px(selection_decomposed_text, plan_auto, 12)
pruef("text-plan-px-decomposed-preserved", länge(plan_px_decomp["glyphs"]) == länge(plan_px_decomp_basis["glyphs"]) und länge(plan_px_decomp["runs"]) == länge(plan_px_decomp_basis["runs"]) und plan_px_decomp["advance"] == plan_px_decomp_basis["advance"] und plan_px_decomp["pixel_advance"] == plan_px_decomp_basis["advance"] * 2)
setze plan_px_zwj_basis auf uim_text_plan(selection_family_plus, plan_auto)
setze plan_px_zwj auf text_plan_px(selection_family_plus, plan_auto, 12)
pruef("text-plan-px-zwj-preserved", länge(plan_px_zwj["glyphs"]) == länge(plan_px_zwj_basis["glyphs"]) und plan_px_zwj["glyphs"][0] == plan_px_zwj_basis["glyphs"][0] und länge(plan_px_zwj["runs"]) == länge(plan_px_zwj_basis["runs"]) und plan_px_zwj["advance"] == plan_px_zwj_basis["advance"] und plan_px_zwj["pixel_advance"] == plan_px_zwj_basis["advance"] * 2)
setze plan_px_ri_basis auf uim_text_plan(selection_ri_text, plan_auto)
setze plan_px_ri auf text_plan_px(selection_ri_text, plan_auto, 12)
pruef("text-plan-px-ri-preserved", länge(plan_px_ri["glyphs"]) == länge(plan_px_ri_basis["glyphs"]) und länge(plan_px_ri["runs"]) == länge(plan_px_ri_basis["runs"]) und plan_px_ri["advance"] == plan_px_ri_basis["advance"] und plan_px_ri["pixel_advance"] == plan_px_ri_basis["advance"] * 2)

setze plan_px_mixed auf text_plan_px(mixed_text, plan_auto, 12)
pruef("text-plan-px-mixed-logical", plan_px_mixed["order"] == "logical" und plan_px_mixed["full_bidi"] == falsch und plan_px_mixed["full_shaping"] == falsch und plan_px_mixed["direction"] == mixed_plan["direction"] und plan_px_mixed["script"] == "mixed" und plan_px_mixed["supported"] == falsch und plan_px_mixed["fallback"] == "logical-glyph" und länge(plan_px_mixed["runs"]) == 3 und plan_px_mixed["advance"] == 12 und plan_px_mixed["pixel_advance"] == 24)
setze plan_px_arabic auf text_plan_px(arabic_text, text_plan_optionen(1, "rtl", "arabic"), 12)
pruef("text-plan-px-arabic-unsupported-honest", plan_px_arabic["order"] == "logical" und plan_px_arabic["full_bidi"] == falsch und plan_px_arabic["full_shaping"] == falsch und plan_px_arabic["direction"] == "rtl" und plan_px_arabic["script"] == "arabic" und plan_px_arabic["supported"] == falsch und plan_px_arabic["fallback"] == "logical-glyph" und länge(plan_px_arabic["glyphs"]) == 2 und plan_px_arabic["glyphs"][0] == "?" und plan_px_arabic["glyphs"][1] == "?" und plan_px_arabic["advance"] == 8 und plan_px_arabic["pixel_advance"] == 16)

setze plan_px_malformed_basis auf uim_text_plan(kaputt_mixed, plan_auto)
setze plan_px_malformed auf text_plan_px(kaputt_mixed, plan_auto, 12)
pruef("text-plan-px-malformed-resync", länge(plan_px_malformed["glyphs"]) == 4 und plan_px_malformed["glyphs"][0] == "A" und plan_px_malformed["glyphs"][1] == "?" und plan_px_malformed["glyphs"][2] == "?" und plan_px_malformed["glyphs"][3] == "B" und länge(plan_px_malformed["runs"]) == länge(plan_px_malformed_basis["runs"]) und plan_px_malformed["advance"] == plan_px_malformed_basis["advance"] und plan_px_malformed["advance"] == 16 und plan_px_malformed["scale"] == 2 und plan_px_malformed["pixel_advance"] == 32)

pruef("text-plan-px-actual-width-equality", text_plan_px("AB", plan_ltr, 11)["pixel_advance"] == _uim_zf_text_breite(text_k, text_z, "AB", 11) und text_plan_px("AB", plan_ltr, 12)["pixel_advance"] == _uim_zf_text_breite(text_k, text_z, "AB", 12) und text_plan_px(mixed_text, plan_auto, 12)["pixel_advance"] == _uim_zf_text_breite(text_k, text_z, mixed_text, 12))
setze plan_px_cross auf text_plan_px("ABC", plan_auto, 12)
setze plan_px_cross_selection auf auswahl_plan_px("ABC", plan_auto, 12, 0, 3)
setze plan_px_cross_caret auf caret_treffer_px("ABC", plan_auto, 12, 99)
pruef("text-plan-px-b9-b10-terminal-width-equality", plan_px_cross["pixel_advance"] == 24 und plan_px_cross["pixel_advance"] == plan_px_cross_selection["pixel_advance_end"] und plan_px_cross["pixel_advance"] == plan_px_cross_caret["pixel_advance_end"] und plan_px_cross_selection["selection_end"] == 3 und plan_px_cross_caret["selection_end"] == 3)

setze plan_px_fresh_a auf text_plan_px("AB", plan_ltr, 12)
setze plan_px_fresh_b auf text_plan_px("AB", plan_ltr, 12)
plan_px_fresh_a["pixel_advance"] = 99
plan_px_fresh_a["glyphs"][0] = "?"
plan_px_fresh_a["runs"][0]["source_end"] = 99
setze plan_px_base_nach auf uim_text_plan("AB", plan_ltr)
pruef("text-plan-px-fresh-nested-base-unaffected", plan_px_fresh_a["pixel_advance"] == 99 und plan_px_fresh_a["glyphs"][0] == "?" und plan_px_fresh_a["runs"][0]["source_end"] == 99 und plan_px_fresh_b["pixel_advance"] == 16 und plan_px_fresh_b["glyphs"][0] == "A" und plan_px_fresh_b["runs"][0]["source_end"] == 2 und länge(plan_px_base_nach) == 13 und plan_px_base_nach["glyphs"][0] == "A" und plan_px_base_nach["runs"][0]["source_end"] == 2)

pruef("text-plan-px-invalid-domain-options-v2", text_plan_px(nichts, plan_auto, 11) == nichts und text_plan_px({}, plan_auto, 11) == nichts und text_plan_px("A", {}, 11) == nichts und text_plan_px("A", "ungueltig", 11) == nichts und text_plan_px("A", text_plan_optionen(2, "ltr", "latin"), 11) == nichts)
pruef("text-plan-px-invalid-size", text_plan_px("A", plan_auto, 0) == nichts und text_plan_px("A", plan_auto, -1) == nichts und text_plan_px("A", plan_auto, "12") == nichts und text_plan_px("A", plan_auto, wahr) == nichts)

# B13: Exakte Single-Line-Pixelmetrik des eingebetteten 3x5-Renderers.
# Keine Baseline-Koordinate, kein Zeilenumbruch und keine native Fontmetrik.
funktion zeilenmetrik_px(version, groesse):
    gib_zurück uim_text_zeilenmetrik_px(version, groesse)

setze line_metric_11 auf zeilenmetrik_px(1, 11)
pruef("line-metric-px-schema-exact11", typ_von(line_metric_11) == "Woerterbuch" und länge(line_metric_11) == 11 und line_metric_11["version"] == 1 und line_metric_11["engine"] == "embedded-pixel-v1" und line_metric_11["unit"] == "surface-pixels" und line_metric_11["line_model"] == "single-line-top-origin-v1" und line_metric_11["origin"] == "top-left" und line_metric_11["scale"] == 1 und line_metric_11["bitmap_width"] == 3 und line_metric_11["bitmap_height"] == 5 und line_metric_11["advance_x"] == 4 und line_metric_11["line_height"] == 5 und line_metric_11["baseline_supported"] == falsch und line_metric_11.enthält("baseline") == falsch)

setze line_metric_frac auf zeilenmetrik_px(1, 11.999)
setze line_metric_12 auf zeilenmetrik_px(1, 12)
pruef("line-metric-px-threshold-11-999-vs12", line_metric_frac["scale"] == 1 und line_metric_frac["bitmap_width"] == 3 und line_metric_frac["bitmap_height"] == 5 und line_metric_frac["advance_x"] == 4 und line_metric_frac["line_height"] == 5 und line_metric_12["scale"] == 2 und line_metric_12["bitmap_width"] == 6 und line_metric_12["bitmap_height"] == 10 und line_metric_12["advance_x"] == 8 und line_metric_12["line_height"] == 10)
setze line_metric_99 auf zeilenmetrik_px(1, 99)
pruef("line-metric-px-large-size-scale2", line_metric_99["scale"] == 2 und line_metric_99["bitmap_width"] == 6 und line_metric_99["bitmap_height"] == 10 und line_metric_99["advance_x"] == 8 und line_metric_99["line_height"] == 10)

setze line_raster_1_k auf uim_surface_wurzel(8, 7)
setze line_raster_1_z auf uim_surface_handle(line_raster_1_k)
uim_surface_leeren(line_raster_1_k, 0, 0, 0, 0)
_uim_farbe(line_raster_1_k, line_raster_1_z, [10, 20, 30, 255])
_uim_zf_text(line_raster_1_k, line_raster_1_z, 1, 1, "A", 11)
pruef("line-metric-px-raster-scale1-extent-gap", uim_surface_pixel(line_raster_1_k, 2, 1)["alpha"] == 255 und uim_surface_pixel(line_raster_1_k, 1, 5)["alpha"] == 255 und uim_surface_pixel(line_raster_1_k, 3, 5)["alpha"] == 255 und uim_surface_pixel(line_raster_1_k, 4, 5)["alpha"] == 0 und uim_surface_pixel(line_raster_1_k, 1, 6)["alpha"] == 0)

setze line_raster_2_k auf uim_surface_wurzel(12, 13)
setze line_raster_2_z auf uim_surface_handle(line_raster_2_k)
uim_surface_leeren(line_raster_2_k, 0, 0, 0, 0)
_uim_farbe(line_raster_2_k, line_raster_2_z, [10, 20, 30, 255])
_uim_zf_text(line_raster_2_k, line_raster_2_z, 1, 1, "A", 12)
pruef("line-metric-px-raster-scale2-extent-gap", uim_surface_pixel(line_raster_2_k, 3, 1)["alpha"] == 255 und uim_surface_pixel(line_raster_2_k, 1, 10)["alpha"] == 255 und uim_surface_pixel(line_raster_2_k, 6, 10)["alpha"] == 255 und uim_surface_pixel(line_raster_2_k, 7, 10)["alpha"] == 0 und uim_surface_pixel(line_raster_2_k, 1, 11)["alpha"] == 0)

setze line_cross_ascii auf text_plan_px("ABC", plan_auto, 12)
pruef("line-metric-px-b12-per-glyph-advance", line_cross_ascii["scale"] == line_metric_12["scale"] und line_cross_ascii["pixel_advance"] == länge(line_cross_ascii["glyphs"]) * line_metric_12["advance_x"] und line_cross_ascii["pixel_advance"] == 24)
pruef("line-metric-px-b9-b10-terminal-cross", plan_px_cross["pixel_advance"] == länge(plan_px_cross["glyphs"]) * line_metric_12["advance_x"] und plan_px_cross_caret["pixel_advance_end"] == plan_px_cross["pixel_advance"] und plan_px_cross_selection["pixel_advance_end"] == plan_px_cross["pixel_advance"])

setze line_cross_decomp auf text_plan_px(selection_decomposed_text, plan_auto, 12)
setze line_cross_zwj auf text_plan_px(selection_family_plus, plan_auto, 12)
setze line_cross_ri auf text_plan_px(selection_ri_text, plan_auto, 12)
pruef("line-metric-px-bounded-cluster-advance", line_cross_decomp["pixel_advance"] == länge(line_cross_decomp["glyphs"]) * line_metric_12["advance_x"] und line_cross_zwj["pixel_advance"] == länge(line_cross_zwj["glyphs"]) * line_metric_12["advance_x"] und line_cross_ri["pixel_advance"] == länge(line_cross_ri["glyphs"]) * line_metric_12["advance_x"] und line_cross_decomp["pixel_advance"] == 16 und line_cross_zwj["pixel_advance"] == 16 und line_cross_ri["pixel_advance"] == 16)
setze line_cross_mixed auf text_plan_px(mixed_text, plan_auto, 12)
setze line_cross_malformed auf text_plan_px(kaputt_mixed, plan_auto, 12)
pruef("line-metric-px-mixed-malformed-count", line_cross_mixed["order"] == "logical" und line_cross_mixed["pixel_advance"] == länge(line_cross_mixed["glyphs"]) * line_metric_12["advance_x"] und line_cross_mixed["pixel_advance"] == 24 und line_cross_malformed["source_unit"] == "utf8-bytes" und line_cross_malformed["pixel_advance"] == länge(line_cross_malformed["glyphs"]) * line_metric_12["advance_x"] und line_cross_malformed["pixel_advance"] == 32)

setze line_metric_fresh_a auf zeilenmetrik_px(1, 12)
setze line_metric_fresh_b auf zeilenmetrik_px(1, 12)
line_metric_fresh_a["scale"] = 99
line_metric_fresh_a["line_height"] = 99
setze line_metric_fresh_c auf zeilenmetrik_px(1, 12)
pruef("line-metric-px-fresh-copy", line_metric_fresh_a["scale"] == 99 und line_metric_fresh_a["line_height"] == 99 und line_metric_fresh_b["scale"] == 2 und line_metric_fresh_b["line_height"] == 10 und line_metric_fresh_c["scale"] == 2 und line_metric_fresh_c["line_height"] == 10 und länge(line_metric_fresh_c) == 11)

pruef("line-metric-px-invalid-version", zeilenmetrik_px(0, 11) == nichts und zeilenmetrik_px(2, 11) == nichts und zeilenmetrik_px(1.1, 11) == nichts und zeilenmetrik_px(nichts, 11) == nichts und zeilenmetrik_px("1", 11) == nichts und zeilenmetrik_px(wahr, 11) == nichts)
pruef("line-metric-px-invalid-size", zeilenmetrik_px(1, 0) == nichts und zeilenmetrik_px(1, -1) == nichts und zeilenmetrik_px(1, nichts) == nichts und zeilenmetrik_px(1, "12") == nichts und zeilenmetrik_px(1, wahr) == nichts)

# B14: Logische Single-Line-Auswahlbox aus B10+B13. Das ist eine
# Advance-Zellenbox, keine Ink-Bounds und keine sichtbare Caret-Malerei.
funktion auswahl_box_px(s, optionen, groesse, start, ende):
    gib_zurück uim_text_auswahl_box_px(s, optionen, groesse, start, ende)

setze box_empty_1 auf auswahl_box_px("", plan_auto, 11, 0, 0)
setze box_empty_2 auf auswahl_box_px("", plan_auto, 12, 0, 0)
pruef("selection-box-px-empty-scales", länge(box_empty_1) == 20 und box_empty_1["collapsed"] == wahr und box_empty_1["scale"] == 1 und box_empty_1["box_model"] == "logical-advance-single-line-top-origin-v1" und box_empty_1["pixel_x"] == 0 und box_empty_1["pixel_y"] == 0 und box_empty_1["pixel_width"] == 0 und box_empty_1["pixel_height"] == 5 und länge(box_empty_2) == 20 und box_empty_2["scale"] == 2 und box_empty_2["pixel_width"] == 0 und box_empty_2["pixel_height"] == 10)

setze box_ascii auf auswahl_box_px("ABC", plan_auto, 11, 1, 3)
pruef("selection-box-px-ascii-schema-exact20", typ_von(box_ascii) == "Woerterbuch" und länge(box_ascii) == 20 und box_ascii["version"] == 1 und box_ascii["source_unit"] == "utf8-bytes" und box_ascii["cluster_model"] == "bounded-v1" und box_ascii["order"] == "logical" und box_ascii["full_bidi"] == falsch und box_ascii["selection_start"] == 1 und box_ascii["selection_end"] == 3 und box_ascii["cluster_start"] == 1 und box_ascii["cluster_end"] == 3 und box_ascii["advance_start"] == 4 und box_ascii["advance_end"] == 12 und box_ascii["collapsed"] == falsch und box_ascii["scale"] == 1 und box_ascii["pixel_advance_start"] == 4 und box_ascii["pixel_advance_end"] == 12 und box_ascii["box_model"] == "logical-advance-single-line-top-origin-v1" und box_ascii["pixel_x"] == 4 und box_ascii["pixel_y"] == 0 und box_ascii["pixel_width"] == 8 und box_ascii["pixel_height"] == 5)

setze box_frac auf auswahl_box_px("ABC", plan_auto, 11.999, 0, 3)
setze box_threshold auf auswahl_box_px("ABC", plan_auto, 12, 0, 3)
pruef("selection-box-px-threshold-11-999-vs12", box_frac["scale"] == 1 und box_frac["pixel_x"] == 0 und box_frac["pixel_width"] == 12 und box_frac["pixel_height"] == 5 und box_threshold["scale"] == 2 und box_threshold["pixel_x"] == 0 und box_threshold["pixel_width"] == 24 und box_threshold["pixel_height"] == 10)
setze box_large auf auswahl_box_px("ABC", plan_auto, 99, 1, 2)
pruef("selection-box-px-large-size", box_large["scale"] == 2 und box_large["pixel_x"] == 8 und box_large["pixel_width"] == 8 und box_large["pixel_height"] == 10)

setze box_collapsed auf auswahl_box_px("ABC", plan_auto, 12, 2, 2)
pruef("selection-box-px-collapsed-b9-b11-position", box_collapsed["collapsed"] == wahr und box_collapsed["selection_start"] == 2 und box_collapsed["pixel_x"] == 16 und box_collapsed["pixel_width"] == 0 und box_collapsed["pixel_height"] == 10 und box_collapsed["pixel_x"] == step_px_cross_b["pixel_advance_start"] und box_collapsed["pixel_x"] == step_px_cross_b9["pixel_advance_start"])

setze box_decomp auf auswahl_box_px(selection_decomposed_text, plan_auto, 12, 0, 3)
pruef("selection-box-px-decomposed", box_decomp["cluster_start"] == 0 und box_decomp["cluster_end"] == 1 und box_decomp["pixel_x"] == 0 und box_decomp["pixel_width"] == 8 und box_decomp["pixel_height"] == 10)
setze box_zwj auf auswahl_box_px(selection_family_plus, plan_auto, 12, 0, 25)
pruef("selection-box-px-zwj", box_zwj["selection_end"] == 25 und box_zwj["cluster_start"] == 0 und box_zwj["cluster_end"] == 1 und box_zwj["pixel_x"] == 0 und box_zwj["pixel_width"] == 8 und box_zwj["pixel_height"] == 10)
setze box_ri auf auswahl_box_px(selection_ri_text, plan_auto, 12, 0, 8)
pruef("selection-box-px-ri", box_ri["selection_end"] == 8 und box_ri["cluster_start"] == 0 und box_ri["cluster_end"] == 1 und box_ri["pixel_x"] == 0 und box_ri["pixel_width"] == 8 und box_ri["pixel_height"] == 10)

setze box_mixed auf auswahl_box_px(mixed_text, plan_auto, 12, 1, 5)
pruef("selection-box-px-mixed-logical", box_mixed["order"] == "logical" und box_mixed["full_bidi"] == falsch und box_mixed["cluster_start"] == 1 und box_mixed["cluster_end"] == 3 und box_mixed["pixel_x"] == 8 und box_mixed["pixel_width"] == 16 und box_mixed["pixel_height"] == 10)
setze box_malformed auf auswahl_box_px(kaputt_mixed, plan_auto, 12, 1, 3)
pruef("selection-box-px-malformed-resync", box_malformed["source_unit"] == "utf8-bytes" und box_malformed["cluster_start"] == 1 und box_malformed["cluster_end"] == 3 und box_malformed["pixel_x"] == 8 und box_malformed["pixel_width"] == 16 und box_malformed["pixel_height"] == 10)

setze box_full auf auswahl_box_px("ABC", plan_auto, 12, 0, 3)
pruef("selection-box-px-b12-b13-full-range-cross", box_full["pixel_x"] == 0 und box_full["pixel_width"] == plan_px_cross["pixel_advance"] und box_full["pixel_width"] == plan_px_cross_selection["pixel_advance_end"] und box_full["pixel_height"] == line_metric_12["line_height"] und box_full["pixel_width"] == 24 und box_full["pixel_height"] == 10)

setze box_fresh_a auf auswahl_box_px("ABC", plan_auto, 12, 1, 3)
setze box_fresh_b auf auswahl_box_px("ABC", plan_auto, 12, 1, 3)
box_fresh_a["pixel_x"] = 99
box_fresh_a["pixel_height"] = 99
setze box_fresh_c auf auswahl_box_px("ABC", plan_auto, 12, 1, 3)
pruef("selection-box-px-fresh-copy", box_fresh_a["pixel_x"] == 99 und box_fresh_a["pixel_height"] == 99 und box_fresh_b["pixel_x"] == 8 und box_fresh_b["pixel_width"] == 16 und box_fresh_b["pixel_height"] == 10 und box_fresh_c["pixel_x"] == 8 und box_fresh_c["pixel_height"] == 10 und länge(box_fresh_c) == 20)

pruef("selection-box-px-invalid-domain-options", auswahl_box_px(nichts, plan_auto, 11, 0, 0) == nichts und auswahl_box_px({}, plan_auto, 11, 0, 0) == nichts und auswahl_box_px("ABC", {}, 11, 0, 1) == nichts und auswahl_box_px("ABC", "ungueltig", 11, 0, 1) == nichts und auswahl_box_px("ABC", text_plan_optionen(2, "ltr", "latin"), 11, 0, 1) == nichts)
pruef("selection-box-px-invalid-size-offsets", auswahl_box_px("ABC", plan_auto, 0, 0, 1) == nichts und auswahl_box_px("ABC", plan_auto, -1, 0, 1) == nichts und auswahl_box_px("ABC", plan_auto, "12", 0, 1) == nichts und auswahl_box_px("ABC", plan_auto, wahr, 0, 1) == nichts und auswahl_box_px("ABC", plan_auto, 12, "0", 1) == nichts und auswahl_box_px("ABC", plan_auto, 12, 0.5, 1) == nichts und auswahl_box_px("ABC", plan_auto, 12, 0, wahr) == nichts und auswahl_box_px("ABC", plan_auto, 12, -1, 1) == nichts und auswahl_box_px("ABC", plan_auto, 12, 0, 4) == nichts und auswahl_box_px("ABC", plan_auto, 12, 2, 1) == nichts und auswahl_box_px(selection_decomposed_text, plan_auto, 12, 1, 3) == nichts)

# B15: U+0020 ist ein echtes, transparentes Glyph mit logischem Advance.
# Kein Tab/CRLF/NBSP-/Unicode-Whitespace- oder Layoutversprechen.
setze space_plan auf uim_text_plan(" ", plan_auto)
setze space_run auf space_plan["runs"][0]
pruef("space-plan-supported-transparent-glyph", typ_von(space_plan) == "Woerterbuch" und länge(space_plan) == 13 und space_plan["order"] == "logical" und space_plan["source_unit"] == "utf8-bytes" und space_plan["cluster_model"] == "bounded-v1" und space_plan["direction"] == "ltr" und space_plan["script"] == "common" und space_plan["supported"] == wahr und space_plan["fallback"] == "none" und länge(space_plan["glyphs"]) == 1 und space_plan["glyphs"][0] == " " und space_plan["advance"] == 4 und länge(space_plan["runs"]) == 1 und space_run["source_start"] == 0 und space_run["source_end"] == 1 und space_run["cluster_start"] == 0 und space_run["cluster_count"] == 1 und space_run["direction"] == "ltr" und space_run["script"] == "common" und space_run["supported"] == wahr und space_run["fallback"] == "none")

setze space_around_plan auf uim_text_plan("A A", plan_auto)
pruef("space-plan-a-space-a-order-advance", space_around_plan["order"] == "logical" und space_around_plan["supported"] == wahr und space_around_plan["fallback"] == "none" und länge(space_around_plan["glyphs"]) == 3 und space_around_plan["glyphs"][0] == "A" und space_around_plan["glyphs"][1] == " " und space_around_plan["glyphs"][2] == "A" und space_around_plan["advance"] == 12 und länge(space_around_plan["runs"]) == 3)

setze space_px_1 auf text_plan_px(" ", plan_auto, 11)
setze space_px_2 auf text_plan_px(" ", plan_auto, 12)
pruef("space-plan-px-scale1-scale2", space_px_1["scale"] == 1 und space_px_1["pixel_advance"] == 4 und space_px_2["scale"] == 2 und space_px_2["pixel_advance"] == 8)
pruef("space-actual-width-scale1-scale2", _uim_zf_text_breite(text_k, text_z, " ", 11) == 4 und _uim_zf_text_breite(text_k, text_z, " ", 12) == 8)

setze space_surface_1 auf uim_surface_wurzel(16, 12)
setze space_drawer_1 auf uim_surface_handle(space_surface_1)
uim_surface_leeren(space_surface_1, 0, 0, 0, 0)
setze space_clear_hash_1 auf uim_surface_hash(space_surface_1)
_uim_farbe(space_surface_1, space_drawer_1, [240, 230, 220, 255])
_uim_zf_text(space_surface_1, space_drawer_1, 0, 0, " ", 11)
pruef("space-raster-transparent-scale1", uim_surface_hash(space_surface_1) == space_clear_hash_1)

setze space_surface_2 auf uim_surface_wurzel(16, 12)
setze space_drawer_2 auf uim_surface_handle(space_surface_2)
uim_surface_leeren(space_surface_2, 0, 0, 0, 0)
setze space_clear_hash_2 auf uim_surface_hash(space_surface_2)
_uim_farbe(space_surface_2, space_drawer_2, [240, 230, 220, 255])
_uim_zf_text(space_surface_2, space_drawer_2, 0, 0, " ", 12)
pruef("space-raster-transparent-scale2", uim_surface_hash(space_surface_2) == space_clear_hash_2)

setze space_leading_surface auf uim_surface_wurzel(20, 12)
setze space_leading_drawer auf uim_surface_handle(space_leading_surface)
uim_surface_leeren(space_leading_surface, 0, 0, 0, 0)
_uim_farbe(space_leading_surface, space_leading_drawer, [240, 230, 220, 255])
_uim_zf_text(space_leading_surface, space_leading_drawer, 0, 0, " A", 11)
pruef("space-leading-ink-offset", uim_surface_pixel(space_leading_surface, 1, 0)["alpha"] == 0 und uim_surface_pixel(space_leading_surface, 5, 0)["alpha"] == 255 und uim_surface_pixel(space_leading_surface, 4, 0)["alpha"] == 0)

pruef("space-trailing-advance-no-added-ink", _uim_zf_text_breite(text_k, text_z, "A", 11) == 4 und _uim_zf_text_breite(text_k, text_z, "A ", 11) == 8 und raster_text_hash("A") == raster_text_hash("A "))

setze space_selection auf auswahl_plan_px("A A", plan_auto, 12, 1, 2)
setze space_box auf auswahl_box_px("A A", plan_auto, 12, 1, 2)
setze space_caret auf caret_treffer_px("A A", plan_auto, 12, 12)
pruef("space-selection-caret-box-cross", space_selection["selection_start"] == 1 und space_selection["selection_end"] == 2 und space_selection["pixel_advance_start"] == 8 und space_selection["pixel_advance_end"] == 16 und space_box["pixel_x"] == 8 und space_box["pixel_width"] == 8 und space_box["pixel_height"] == 10 und space_caret["selection_start"] == 2 und space_caret["pixel_advance_start"] == 16)

setze space_nbsp_bytes auf bytes_neu([194, 160])
setze space_nbsp_plan auf uim_text_plan(space_nbsp_bytes, plan_auto)
pruef("space-nbsp-remains-unsupported-scope-fence", space_nbsp_plan["supported"] == falsch und space_nbsp_plan["fallback"] == "logical-glyph" und länge(space_nbsp_plan["glyphs"]) == 1 und space_nbsp_plan["glyphs"][0] == "?" und space_nbsp_plan["advance"] == 4 und raster_text_hash(space_nbsp_bytes) == raster_text_hash("?"))

# B16: Common-Cluster bleiben in den rohen Runs sichtbar, konkurrieren
# aber bei auto nur dann um das Top-Level-Skript, wenn kein starkes Skript da ist.
setze b16_a_space_a auf uim_text_plan("A A", plan_auto)
setze b16_a_space_a_runs auf b16_a_space_a["runs"]
pruef("plan-auto-common-neutral-a-space-a", b16_a_space_a["script"] == "latin" und länge(b16_a_space_a_runs) == 3 und b16_a_space_a_runs[0]["script"] == "latin" und b16_a_space_a_runs[1]["script"] == "common" und b16_a_space_a_runs[2]["script"] == "latin")

setze b16_space_a_space auf uim_text_plan(" A ", plan_auto)
setze b16_space_a_space_runs auf b16_space_a_space["runs"]
pruef("plan-auto-common-neutral-space-a-space", b16_space_a_space["script"] == "latin" und länge(b16_space_a_space_runs) == 3 und b16_space_a_space_runs[0]["script"] == "common" und b16_space_a_space_runs[1]["script"] == "latin" und b16_space_a_space_runs[2]["script"] == "common")

setze b16_digits auf uim_text_plan("123", plan_auto)
setze b16_digits_runs auf b16_digits["runs"]
pruef("plan-auto-common-only-digits", b16_digits["script"] == "common" und länge(b16_digits_runs) == 1 und b16_digits_runs[0]["script"] == "common")

setze b16_punctuation auf uim_text_plan("!? ", plan_auto)
setze b16_punctuation_runs auf b16_punctuation["runs"]
pruef("plan-auto-common-only-punctuation", b16_punctuation["script"] == "common" und länge(b16_punctuation_runs) == 1 und b16_punctuation_runs[0]["script"] == "common")

setze b16_hebrew_common auf uim_text_plan("א!", plan_auto)
setze b16_hebrew_common_runs auf b16_hebrew_common["runs"]
pruef("plan-auto-common-neutral-hebrew", b16_hebrew_common["script"] == "hebrew" und länge(b16_hebrew_common_runs) == 2 und b16_hebrew_common_runs[0]["script"] == "hebrew" und b16_hebrew_common_runs[1]["script"] == "common")

setze b16_arabic_common auf uim_text_plan("ل!", plan_auto)
setze b16_arabic_common_runs auf b16_arabic_common["runs"]
pruef("plan-auto-common-neutral-arabic", b16_arabic_common["script"] == "arabic" und länge(b16_arabic_common_runs) == 2 und b16_arabic_common_runs[0]["script"] == "arabic" und b16_arabic_common_runs[1]["script"] == "common")

setze b16_strong_mixed auf uim_text_plan("A א ل", plan_auto)
setze b16_strong_mixed_runs auf b16_strong_mixed["runs"]
pruef("plan-auto-common-neutral-strong-mixed", b16_strong_mixed["script"] == "mixed" und länge(b16_strong_mixed_runs) == 5 und b16_strong_mixed_runs[0]["script"] == "latin" und b16_strong_mixed_runs[1]["script"] == "common" und b16_strong_mixed_runs[2]["script"] == "hebrew" und b16_strong_mixed_runs[3]["script"] == "common" und b16_strong_mixed_runs[4]["script"] == "arabic")

setze b16_explicit_hebrew auf uim_text_plan("A A", text_plan_optionen(1, "ltr", "hebrew"))
setze b16_explicit_hebrew_runs auf b16_explicit_hebrew["runs"]
pruef("plan-explicit-script-preserves-raw-runs", b16_explicit_hebrew["script"] == "hebrew" und b16_explicit_hebrew["direction"] == "ltr" und länge(b16_explicit_hebrew_runs) == 3 und b16_explicit_hebrew_runs[0]["script"] == "latin" und b16_explicit_hebrew_runs[1]["script"] == "common" und b16_explicit_hebrew_runs[2]["script"] == "latin")

# B17: Das explizite Latin-1-Alphabet-Subset wird im bestehenden
# bounded-v1-Plan als latin klassifiziert. Glyph-Support bleibt unverändert.
funktion b17_plan_skript_ok(p, skript):
    wenn p == nichts:
        gib_zurück falsch
    setze runs auf p["runs"]
    gib_zurück p["script"] == skript und p["direction"] == "ltr" und p["cluster_model"] == "bounded-v1" und länge(p["glyphs"]) == 1 und p["glyphs"][0] == "?" und p["supported"] == falsch und p["fallback"] == "logical-glyph" und länge(runs) == 1 und runs[0]["source_start"] == 0 und runs[0]["source_end"] == 2 und runs[0]["cluster_start"] == 0 und runs[0]["cluster_count"] == 1 und runs[0]["direction"] == "ltr" und runs[0]["script"] == skript und runs[0]["supported"] == falsch und runs[0]["fallback"] == "logical-glyph"

pruef("plan-latin1-range-c0-d6", b17_plan_skript_ok(uim_text_plan("À", plan_auto), "latin") und b17_plan_skript_ok(uim_text_plan("Ö", plan_auto), "latin") und b17_plan_skript_ok(uim_text_plan("É", plan_auto), "latin"))
pruef("plan-latin1-range-d8-f6", b17_plan_skript_ok(uim_text_plan("Ø", plan_auto), "latin") und b17_plan_skript_ok(uim_text_plan("ö", plan_auto), "latin") und b17_plan_skript_ok(uim_text_plan("é", plan_auto), "latin"))
pruef("plan-latin1-range-f8-ff", b17_plan_skript_ok(uim_text_plan("ø", plan_auto), "latin") und b17_plan_skript_ok(uim_text_plan("ÿ", plan_auto), "latin"))
pruef("plan-latin1-symbol-range-fence", b17_plan_skript_ok(uim_text_plan("×", plan_auto), "common") und b17_plan_skript_ok(uim_text_plan("÷", plan_auto), "common"))

# B18: Unicode15.1-letter-subset-v1 klassifiziert nur explizit aufgezaehlte griechische und
# kyrillische Buchstaben als starke LTR-Skripte. Coptic, Satzzeichen,
# Kombinationszeichen, nicht zugewiesene Codepoints und Nachbarbloecke bleiben
# common. Glyph-, BiDi-, Shaping- und Font-Fallback-Capabilities bleiben ehrlich.
funktion b18_cluster_skript_ok(s, skript, byte_count):
    setze p auf uim_text_plan(s, plan_auto)
    wenn p == nichts:
        gib_zurück falsch
    setze runs auf p["runs"]
    wenn länge(runs) != 1:
        gib_zurück falsch
    setze r auf runs[0]
    gib_zurück p["script"] == skript und p["direction"] == "ltr" und p["order"] == "logical" und p["full_bidi"] == falsch und p["full_shaping"] == falsch und p["cluster_model"] == "bounded-v1" und p["supported"] == falsch und p.enthält("unsupported") == falsch und p["fallback"] == "logical-glyph" und länge(p["glyphs"]) == 1 und p["glyphs"][0] == "?" und p["advance"] == 4 und raster_text_hash(s) == raster_text_hash("?") und r["source_start"] == 0 und r["source_end"] == byte_count und r["cluster_start"] == 0 und r["cluster_count"] == 1 und r["direction"] == "ltr" und r["script"] == skript und r["supported"] == falsch und r.enthält("unsupported") == falsch und r["fallback"] == "logical-glyph"

funktion b18_explizit_ok(skript):
    setze p auf uim_text_plan("A", text_plan_optionen(1, "ltr", skript))
    wenn p == nichts:
        gib_zurück falsch
    gib_zurück p["script"] == skript und p["direction"] == "ltr" und p["supported"] == wahr und p.enthält("unsupported") == falsch und p["fallback"] == "none" und länge(p["runs"]) == 1 und p["runs"][0]["script"] == "latin"

pruef("plan-greek-letter-ranges", b18_cluster_skript_ok(bytes_neu([205, 176]), "greek", 2) und b18_cluster_skript_ok(bytes_neu([205, 180]), "greek", 2) und b18_cluster_skript_ok(bytes_neu([205, 182]), "greek", 2) und b18_cluster_skript_ok(bytes_neu([205, 186]), "greek", 2) und b18_cluster_skript_ok(bytes_neu([205, 191]), "greek", 2) und b18_cluster_skript_ok(bytes_neu([206, 134]), "greek", 2) und b18_cluster_skript_ok(bytes_neu([206, 136]), "greek", 2) und b18_cluster_skript_ok(bytes_neu([206, 161]), "greek", 2) und b18_cluster_skript_ok(bytes_neu([206, 163]), "greek", 2) und b18_cluster_skript_ok(bytes_neu([207, 161]), "greek", 2) und b18_cluster_skript_ok(bytes_neu([207, 176]), "greek", 2) und b18_cluster_skript_ok(bytes_neu([207, 183]), "greek", 2) und b18_cluster_skript_ok(bytes_neu([207, 191]), "greek", 2) und b18_cluster_skript_ok(bytes_neu([225, 188, 128]), "greek", 3) und b18_cluster_skript_ok(bytes_neu([225, 188, 149]), "greek", 3) und b18_cluster_skript_ok(bytes_neu([225, 188, 152]), "greek", 3) und b18_cluster_skript_ok(bytes_neu([225, 191, 188]), "greek", 3))
pruef("plan-greek-coptic-common-holes", b18_cluster_skript_ok(bytes_neu([205, 181]), "common", 2) und b18_cluster_skript_ok(bytes_neu([205, 184]), "common", 2) und b18_cluster_skript_ok(bytes_neu([205, 190]), "common", 2) und b18_cluster_skript_ok(bytes_neu([206, 132]), "common", 2) und b18_cluster_skript_ok(bytes_neu([206, 135]), "common", 2) und b18_cluster_skript_ok(bytes_neu([206, 162]), "common", 2) und b18_cluster_skript_ok(bytes_neu([207, 162]), "common", 2) und b18_cluster_skript_ok(bytes_neu([207, 175]), "common", 2) und b18_cluster_skript_ok(bytes_neu([207, 182]), "common", 2) und b18_cluster_skript_ok(bytes_neu([225, 188, 150]), "common", 3) und b18_cluster_skript_ok(bytes_neu([225, 188, 158]), "common", 3) und b18_cluster_skript_ok(bytes_neu([225, 191, 189]), "common", 3))
pruef("plan-cyrillic-letter-ranges", b18_cluster_skript_ok(bytes_neu([208, 128]), "cyrillic", 2) und b18_cluster_skript_ok(bytes_neu([210, 129]), "cyrillic", 2) und b18_cluster_skript_ok(bytes_neu([210, 138]), "cyrillic", 2) und b18_cluster_skript_ok(bytes_neu([212, 175]), "cyrillic", 2) und b18_cluster_skript_ok(bytes_neu([225, 178, 128]), "cyrillic", 3) und b18_cluster_skript_ok(bytes_neu([225, 178, 136]), "cyrillic", 3) und b18_cluster_skript_ok(bytes_neu([234, 153, 128]), "cyrillic", 3) und b18_cluster_skript_ok(bytes_neu([234, 153, 174]), "cyrillic", 3) und b18_cluster_skript_ok(bytes_neu([234, 153, 191]), "cyrillic", 3) und b18_cluster_skript_ok(bytes_neu([234, 154, 157]), "cyrillic", 3))
pruef("plan-cyrillic-common-holes-fences", b18_cluster_skript_ok(bytes_neu([210, 130]), "common", 2) und b18_cluster_skript_ok(bytes_neu([210, 137]), "common", 2) und b18_cluster_skript_ok(bytes_neu([212, 176]), "common", 2) und b18_cluster_skript_ok(bytes_neu([225, 178, 137]), "common", 3) und b18_cluster_skript_ok(bytes_neu([225, 178, 138]), "common", 3) und b18_cluster_skript_ok(bytes_neu([225, 178, 139]), "common", 3) und b18_cluster_skript_ok(bytes_neu([226, 183, 160]), "common", 3) und b18_cluster_skript_ok(bytes_neu([226, 183, 191]), "common", 3) und b18_cluster_skript_ok(bytes_neu([234, 152, 191]), "common", 3) und b18_cluster_skript_ok(bytes_neu([234, 153, 175]), "common", 3) und b18_cluster_skript_ok(bytes_neu([234, 154, 158]), "common", 3) und b18_cluster_skript_ok(bytes_neu([234, 154, 159]), "common", 3) und b18_cluster_skript_ok(bytes_neu([234, 154, 160]), "common", 3))

setze b18_greek_only auf uim_text_plan(bytes_neu([206, 177, 206, 178]), plan_auto)
pruef("plan-greek-only", b18_greek_only["script"] == "greek" und b18_greek_only["direction"] == "ltr" und b18_greek_only["supported"] == falsch und b18_greek_only.enthält("unsupported") == falsch und b18_greek_only["fallback"] == "logical-glyph" und länge(b18_greek_only["runs"]) == 1 und b18_greek_only["runs"][0]["script"] == "greek")
setze b18_cyrillic_only auf uim_text_plan(bytes_neu([208, 145, 208, 175]), plan_auto)
pruef("plan-cyrillic-only", b18_cyrillic_only["script"] == "cyrillic" und b18_cyrillic_only["direction"] == "ltr" und b18_cyrillic_only["supported"] == falsch und b18_cyrillic_only.enthält("unsupported") == falsch und b18_cyrillic_only["fallback"] == "logical-glyph" und länge(b18_cyrillic_only["runs"]) == 1 und b18_cyrillic_only["runs"][0]["script"] == "cyrillic")

setze b18_common_wrap auf uim_text_plan(bytes_neu([33, 206, 177, 63]), plan_auto)
pruef("plan-common-wrap-greek", b18_common_wrap["script"] == "greek" und b18_common_wrap["direction"] == "ltr" und länge(b18_common_wrap["runs"]) == 3 und b18_common_wrap["runs"][0]["script"] == "common" und b18_common_wrap["runs"][1]["script"] == "greek" und b18_common_wrap["runs"][2]["script"] == "common")

setze b18_latin_greek auf uim_text_plan(bytes_neu([65, 206, 177]), plan_auto)
setze b18_latin_cyrillic auf uim_text_plan(bytes_neu([65, 208, 145]), plan_auto)
pruef("plan-latin-with-greek-and-cyrillic-mixed", b18_latin_greek["script"] == "mixed" und b18_latin_greek["direction"] == "ltr" und länge(b18_latin_greek["runs"]) == 2 und b18_latin_greek["runs"][0]["script"] == "latin" und b18_latin_greek["runs"][1]["script"] == "greek" und b18_latin_cyrillic["script"] == "mixed" und b18_latin_cyrillic["direction"] == "ltr" und länge(b18_latin_cyrillic["runs"]) == 2 und b18_latin_cyrillic["runs"][0]["script"] == "latin" und b18_latin_cyrillic["runs"][1]["script"] == "cyrillic")

setze b18_greek_cyrillic_source auf bytes_neu([206, 177, 208, 145])
setze b18_greek_cyrillic auf uim_text_plan(b18_greek_cyrillic_source, plan_auto)
pruef("plan-greek-cyrillic-mixed", b18_greek_cyrillic["script"] == "mixed" und b18_greek_cyrillic["direction"] == "ltr" und b18_greek_cyrillic["supported"] == falsch und b18_greek_cyrillic.enthält("unsupported") == falsch und b18_greek_cyrillic["fallback"] == "logical-glyph" und länge(b18_greek_cyrillic["glyphs"]) == 2 und b18_greek_cyrillic["glyphs"][0] == "?" und b18_greek_cyrillic["glyphs"][1] == "?" und länge(b18_greek_cyrillic["runs"]) == 2 und b18_greek_cyrillic["runs"][0]["script"] == "greek" und b18_greek_cyrillic["runs"][1]["script"] == "cyrillic")

setze b18_repeat_source auf bytes_neu([206, 177, 33, 206, 178])
setze b18_repeat_a auf uim_text_plan(b18_repeat_source, plan_auto)
setze b18_repeat_b auf uim_text_plan(b18_repeat_source, plan_auto)
b18_repeat_a["script"] = "mutiert"
b18_repeat_a["runs"][0]["script"] = "mutiert"
pruef("plan-greek-repeat-across-common-fresh", b18_repeat_b["script"] == "greek" und b18_repeat_b["direction"] == "ltr" und länge(b18_repeat_b["runs"]) == 3 und b18_repeat_b["runs"][0]["script"] == "greek" und b18_repeat_b["runs"][1]["script"] == "common" und b18_repeat_b["runs"][2]["script"] == "greek" und raster_text_hash(b18_repeat_source) == raster_text_hash("?!?"))

pruef("plan-explicit-greek-cyrillic-accept-near-miss", b18_explizit_ok("greek") und b18_explizit_ok("cyrillic") und uim_text_plan("A", text_plan_optionen(1, "ltr", "Greek")) == nichts und uim_text_plan("A", text_plan_optionen(1, "ltr", "cyrilic")) == nichts und uim_text_plan("A", text_plan_optionen(1, "ltr", "common")) == nichts)

setze b18_px11_a auf uim_text_plan_px(b18_greek_cyrillic_source, plan_auto, 11)
setze b18_px11_b auf uim_text_plan_px(b18_greek_cyrillic_source, plan_auto, 11)
setze b18_px12_a auf uim_text_plan_px(b18_greek_cyrillic_source, plan_auto, 12)
setze b18_px12_b auf uim_text_plan_px(b18_greek_cyrillic_source, plan_auto, 12)
b18_px11_a["script"] = "mutiert"
b18_px12_a["glyphs"][0] = "X"
pruef("plan-international-scale11-scale12-fresh-raster-width", b18_px11_b["script"] == "mixed" und b18_px11_b["scale"] == 1 und b18_px11_b["advance"] == 8 und b18_px11_b["pixel_advance"] == 8 und b18_px12_b["script"] == "mixed" und b18_px12_b["scale"] == 2 und b18_px12_b["advance"] == 8 und b18_px12_b["pixel_advance"] == 16 und b18_px12_b["glyphs"][0] == "?" und _uim_zf_text_breite(text_k, text_z, b18_greek_cyrillic_source, 11) == 8 und _uim_zf_text_breite(text_k, text_z, b18_greek_cyrillic_source, 12) == 16 und capability_raster_hash(b18_greek_cyrillic_source, 11) == capability_raster_hash("??", 11) und capability_raster_hash(b18_greek_cyrillic_source, 12) == capability_raster_hash("??", 12) und capability_raster_hash(b18_greek_cyrillic_source, 11) != capability_raster_hash(b18_greek_cyrillic_source, 12))

wenn ergebnis["fehler"] == 0 und ergebnis["checks"] == 228:
    zeige "P016-O2-TEXT-FOUNDATION-OK checks=228 order=logical source_unit=utf8-bytes cluster_model=bounded-v1 full_bidi=0 full_shaping=0 capabilities=embedded-pixel-v1 caret_selection=bounded-logical-v1 caret_hit=nearest-tie-forward-v1 caret_step=bounded-logical-v1 caret_hit_px=scaled-nearest-v1 selection_px=scaled-logical-v1 caret_step_px=scaled-logical-v1 text_plan_px=scaled-logical-v1 line_metrics_px=embedded-single-line-v1 selection_box_px=logical-advance-single-line-top-origin-v1 international_scripts=greek,cyrillic strong_model=unicode15_1-letter-subset-v1 digest=" + metrik_a + " raster=" + raster_1a + "|" + raster_2a
sonst:
    zeige "P016-O2-TEXT-FOUNDATION-RED checks=" + text(ergebnis["checks"]) + " fehler=" + text(ergebnis["fehler"]) + " digest=" + metrik_a
    wirf "P016-O2 Unicode-/Graphem-/Textplanvertrag verletzt"
