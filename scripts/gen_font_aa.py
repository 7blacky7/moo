#!/usr/bin/env python3
# Generator fuer compiler/runtime/moo_font_aa.h — vorgerasterter
# Antialiasing-Font fuer moo_surface_text (NATIVE-UI-3c/3d).
# Ersetzt das verschollene scratchpad/gen_font.py, jetzt versioniert.
#
# Rastert DejaVu Sans Mono in PX Pixel Groesse, Codepoints 32..255
# (ASCII + Latin-1 inkl. deutscher Umlaute und Eszett) als 8-bit-
# Alpha-Zellen fester Groesse. Deterministisch: gleiche Pillow/Font-
# Version => gleiche Tabelle.
#
# Nutzung:  python3 scripts/gen_font_aa.py [pfad-zur-ttf] > compiler/runtime/moo_font_aa.h

import sys
from PIL import Image, ImageDraw, ImageFont

PX = 16
FIRST = 32
LAST = 255  # inklusiv
# Typografische Extras jenseits Latin-1 (Slots nach 255): – — ‘ ’ “ ” „ … €
EXTRA = [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x201E, 0x2026, 0x20AC]
FONT_PFAD = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/TTF/DejaVuSans.ttf"

font = ImageFont.truetype(FONT_PFAD, PX)
ascent, descent = font.getmetrics()
H = ascent + descent
COUNT = LAST - FIRST + 1 + len(EXTRA)
CPS = list(range(FIRST, LAST + 1)) + EXTRA

# Proportional: per-Glyph-Advance; Zelle = Maximum aus Ink und Advance.
advances = []
ink_w = []
for cp in CPS:
    ch = chr(cp)
    if 127 <= cp <= 159:
        ch = " "
    adv = max(1, int(round(font.getlength(ch))))
    bbox = font.getbbox(ch, anchor="ls")
    w = max(adv, (bbox[2] if bbox else adv))
    advances.append(adv)
    ink_w.append(w)
W = max(ink_w) + 1

zeilen = []
zeilen.append("/* Auto-generiert (scripts/gen_font_aa.py): DejaVu Sans %dpx (proportional)," % PX)
zeilen.append(" * Codepoints %d..%d (ASCII + Latin-1, deutsche Umlaute + Eszett)," % (FIRST, LAST))
zeilen.append(" * Graustufen-Alpha (Antialiasing eingebacken).")
zeilen.append(" * Deterministische Tabelle - NICHT von Hand editieren. */")
zeilen.append("#ifndef MOO_FONT_AA_H")
zeilen.append("#define MOO_FONT_AA_H")
zeilen.append("#include <stdint.h>")
zeilen.append("#define MOO_FONT_AA_W %d" % W)
zeilen.append("#define MOO_FONT_AA_H %d" % H)
zeilen.append("#define MOO_FONT_AA_FIRST %d" % FIRST)
zeilen.append("#define MOO_FONT_AA_COUNT %d" % COUNT)
zeilen.append("#define MOO_FONT_AA_EXTRA %d" % len(EXTRA))
zeilen.append("/* Codepoints der Extra-Slots (Index 224..): typografische Zeichen. */")
zeilen.append("static const uint16_t moo_font_aa_extra_cp[%d] = {%s};" % (len(EXTRA), ",".join(str(c) for c in EXTRA)))
zeilen.append("/* Advance pro Glyph in Pixeln (proportional). */")
zeilen.append("static const uint8_t moo_font_aa_adv[%d] = {%s};" % (COUNT, ",".join(str(a) for a in advances)))
zeilen.append("static const uint8_t moo_font_aa[%d][%d] = {" % (COUNT, W * H))

for cp in CPS:
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    ch = chr(cp)
    if 127 <= cp <= 159:
        ch = " "  # C1-Steuerzeichen: leere Zelle
    d.text((0, ascent), ch, font=font, fill=255, anchor="ls")
    px = list(img.getdata())
    zeilen.append("{" + ",".join(str(v) for v in px) + "},")

zeilen.append("};")
zeilen.append("#endif /* MOO_FONT_AA_H */")
print("\n".join(zeilen))
