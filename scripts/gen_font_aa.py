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
FONT_PFAD = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/TTF/DejaVuSansMono.ttf"

font = ImageFont.truetype(FONT_PFAD, PX)
ascent, descent = font.getmetrics()
H = ascent + descent
W = int(round(font.getlength("M")))
COUNT = LAST - FIRST + 1

zeilen = []
zeilen.append("/* Auto-generiert (scripts/gen_font_aa.py): DejaVu Sans Mono %dpx," % PX)
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
zeilen.append("static const uint8_t moo_font_aa[%d][%d] = {" % (COUNT, W * H))

for cp in range(FIRST, LAST + 1):
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
