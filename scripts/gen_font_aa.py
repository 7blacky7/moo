#!/usr/bin/env python3
# Generator fuer compiler/runtime/moo_font_aa.h.
# Fuenf echte DejaVu-Schnitte, deterministisch vorgerastert fuer moo_surface.
#
# Nutzung:
#   uv run --with pillow --with fonttools python scripts/gen_font_aa.py \
#       [/usr/share/fonts/TTF] > compiler/runtime/moo_font_aa.h

from __future__ import annotations

import hashlib
from pathlib import Path
import sys

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont, __version__ as PILLOW_VERSION

PX = 16
FIRST = 32
LAST = 255
BASE_COUNT = LAST - FIRST + 1
OLD_EXTRA = [
    0x2013, 0x2014, 0x2018, 0x2019, 0x201C,
    0x201D, 0x201E, 0x2026, 0x20AC,
]
NEW_EXTRA = [
    0x03A9, 0x2022, 0x2190, 0x2191, 0x2192, 0x2193,
    0x25B2, 0x25BC, 0x2713, 0x2717, 0x26A0, 0x1D55A,
]
EXTRA = OLD_EXTRA + NEW_EXTRA
CPS = list(range(FIRST, LAST + 1)) + EXTRA

FONT_DIR = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/usr/share/fonts/TTF")
FACES = [
    ("normal", "DejaVuSans.ttf"),
    ("fett", "DejaVuSans-Bold.ttf"),
    ("kursiv", "DejaVuSans-Oblique.ttf"),
    ("fett_kursiv", "DejaVuSans-BoldOblique.ttf"),
    ("monospace", "DejaVuSansMono.ttf"),
]


def c_ident(prefix: str) -> str:
    return prefix.upper()


def glyph_text(cp: int) -> str:
    return " " if 127 <= cp <= 159 else chr(cp)


def font_cmap(path: Path) -> set[int]:
    with TTFont(path, lazy=True) as ttf:
        return {cp for table in ttf["cmap"].tables for cp in table.cmap}


def render_face(prefix: str, path: Path) -> dict[str, object]:
    missing = [cp for cp in EXTRA if cp not in font_cmap(path)]
    if missing:
        cps = ", ".join(f"U+{cp:04X}" for cp in missing)
        raise SystemExit(f"{prefix}: Pflichtglyphen fehlen in {path}: {cps}")

    font = ImageFont.truetype(str(path), PX)
    ascent, descent = font.getmetrics()
    height = ascent + descent
    advances: list[int] = []
    ink_widths: list[int] = []
    for cp in CPS:
        ch = glyph_text(cp)
        advance = max(1, int(round(font.getlength(ch))))
        bbox = font.getbbox(ch, anchor="ls")
        advances.append(advance)
        ink_widths.append(max(advance, bbox[2] if bbox else advance))
    width = max(ink_widths) + 1

    glyphs: list[list[int]] = []
    for cp in CPS:
        image = Image.new("L", (width, height), 0)
        ImageDraw.Draw(image).text(
            (0, ascent), glyph_text(cp), font=font, fill=255, anchor="ls"
        )
        glyphs.append(list(image.get_flattened_data()))

    return {
        "prefix": prefix,
        "path": path,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "width": width,
        "height": height,
        "advances": advances,
        "glyphs": glyphs,
    }


def emit_face(lines: list[str], face: dict[str, object]) -> None:
    prefix = str(face["prefix"])
    upper = c_ident(prefix)
    width = int(face["width"])
    height = int(face["height"])
    advances = face["advances"]
    glyphs = face["glyphs"]
    lines.extend([
        f"#define MOO_FONT_AA_{upper}_W {width}",
        f"#define MOO_FONT_AA_{upper}_H {height}",
        f"#define MOO_FONT_AA_{upper}_FIRST {FIRST}",
        f"#define MOO_FONT_AA_{upper}_BASE_COUNT {BASE_COUNT}",
        f"#define MOO_FONT_AA_{upper}_EXTRA_COUNT {len(EXTRA)}",
        f"#define MOO_FONT_AA_{upper}_TOTAL_COUNT {len(CPS)}",
        f"static const uint8_t moo_font_aa_{prefix}_adv[{len(CPS)}] = "
        + "{" + ",".join(str(value) for value in advances) + "};",
        f"static const uint8_t moo_font_aa_{prefix}[{len(CPS)}][{width * height}] = {{",
    ])
    for glyph in glyphs:
        lines.append("{" + ",".join(str(value) for value in glyph) + "},")
    lines.extend([
        "};",
        f"_Static_assert(MOO_FONT_AA_{upper}_TOTAL_COUNT == "
        f"MOO_FONT_AA_{upper}_BASE_COUNT + MOO_FONT_AA_{upper}_EXTRA_COUNT, "
        f"\"{prefix} font count drift\");",
        f"_Static_assert(MOO_FONT_AA_{upper}_BASE_COUNT == 224, "
        f"\"{prefix} base count drift\");",
        "",
    ])


def main() -> None:
    rendered = [render_face(prefix, FONT_DIR / filename) for prefix, filename in FACES]
    heights = {int(face["height"]) for face in rendered}
    if heights != {19}:
        raise SystemExit(f"unerwartete Face-Hoehen: {sorted(heights)}")

    lines = [
        "/* Auto-generiert durch scripts/gen_font_aa.py; NICHT von Hand editieren.",
        f" * Pillow {PILLOW_VERSION}, Pixelgroesse {PX}.",
        " * Faces:",
    ]
    for face in rendered:
        lines.append(
            f" *   {face['prefix']}: {Path(face['path']).name} sha256={face['sha256']}"
        )
    lines.extend([
        " * Extras: " + ", ".join(f"U+{cp:04X}" for cp in EXTRA) + ". */",
        "#ifndef MOO_FONT_AA_GENERATED_H",
        "#define MOO_FONT_AA_GENERATED_H",
        "#include <stdint.h>",
        "",
        f"#define MOO_FONT_AA_FACE_COUNT {len(FACES)}",
        f"#define MOO_FONT_AA_BASE_COUNT {BASE_COUNT}",
        f"#define MOO_FONT_AA_EXTRA_COUNT {len(EXTRA)}",
        f"#define MOO_FONT_AA_TOTAL_COUNT {len(CPS)}",
        "#define MOO_FONT_AA_HEIGHT 19",
        "",
        "typedef struct MooFontAaFace {",
        "    const uint8_t* glyphs;",
        "    const uint8_t* advances;",
        "    const uint32_t* extra_cp;",
        "    uint32_t width;",
        "    uint32_t height;",
        "    uint32_t first;",
        "    uint32_t total_count;",
        "    uint32_t extra_count;",
        "} MooFontAaFace;",
        "",
        f"static const uint32_t moo_font_aa_extra_cp[{len(EXTRA)}] = "
        + "{" + ",".join(str(cp) for cp in EXTRA) + "};",
        "",
    ])
    for face in rendered:
        emit_face(lines, face)

    # Private Kompatibilitaetsaliase fuer die bestehenden Stil-0-Wrapper.
    # B2 stellt die Verbraucher auf die Registry um; die oeffentliche API bleibt alt.
    lines.extend([
        "#define MOO_FONT_AA_W MOO_FONT_AA_NORMAL_W",
        "#define MOO_FONT_AA_H MOO_FONT_AA_NORMAL_H",
        "#define MOO_FONT_AA_FIRST MOO_FONT_AA_NORMAL_FIRST",
        "#define MOO_FONT_AA_COUNT MOO_FONT_AA_NORMAL_TOTAL_COUNT",
        "#define MOO_FONT_AA_EXTRA MOO_FONT_AA_NORMAL_EXTRA_COUNT",
        "#define moo_font_aa moo_font_aa_normal",
        "#define moo_font_aa_adv moo_font_aa_normal_adv",
        "",
    ])
    lines.append("static const MooFontAaFace moo_font_aa_faces[MOO_FONT_AA_FACE_COUNT] = {")
    for face in rendered:
        prefix = str(face["prefix"])
        upper = c_ident(prefix)
        lines.append(
            "    {&moo_font_aa_" + prefix + "[0][0], moo_font_aa_" + prefix
            + "_adv, moo_font_aa_extra_cp, MOO_FONT_AA_" + upper + "_W, "
            + "MOO_FONT_AA_" + upper + "_H, MOO_FONT_AA_" + upper + "_FIRST, "
            + "MOO_FONT_AA_" + upper + "_TOTAL_COUNT, MOO_FONT_AA_" + upper
            + "_EXTRA_COUNT},"
        )
    lines.extend([
        "};",
        "_Static_assert(MOO_FONT_AA_FACE_COUNT == 5, \"font face registry drift\");",
        "_Static_assert(MOO_FONT_AA_TOTAL_COUNT == MOO_FONT_AA_BASE_COUNT + MOO_FONT_AA_EXTRA_COUNT,",
        "               \"global font count drift\");",
        "_Static_assert(MOO_FONT_AA_EXTRA_COUNT == 21, \"font extra count drift\");",
        "#endif /* MOO_FONT_AA_GENERATED_H */",
    ])
    print("\n".join(lines))


if __name__ == "__main__":
    main()
