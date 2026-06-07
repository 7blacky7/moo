#!/usr/bin/env python3
# ============================================================
# voxel_screenshot_check.py — BMP-Validator fuer den
# Voxel-Visual-Test (Plan-006 R5).
#
# Prueft fuer ein vom moo-Selftest geschriebenes 24/32-bit-BMP:
#   - Datei existiert und ist > 0 Byte
#   - nicht blank (mehr als eine Farbe; nicht nur Himmel)
#   - Anzahl eindeutiger Farben >= Schwellwert (Terrain sichtbar)
#
# Optional vergleicht es zwei BMPs (z.B. gl33 vs. vulkan) auf
# "grobe Aehnlichkeit": beide nicht blank und Farbanzahl in der
# gleichen Groessenordnung (Faktor <= 8). Pixel-Identitaet ist NICHT
# gefordert (Backend-Unterschiede in Rasterisierung/Shading).
#
# Reine Standardbibliothek (kein uv/pip noetig). BMP-Parser
# unterstuetzt BITMAPINFOHEADER (40 Byte) mit 24/32 bpp, unkomprimiert
# (BI_RGB) — genau das, was raum_screenshot_bmp schreibt.
#
# Aufruf:
#   python3 voxel_screenshot_check.py check  <bmp> [min_colors]
#   python3 voxel_screenshot_check.py compare <bmp_a> <bmp_b> [min_colors]
#
# Exit 0 = OK, Exit 1 = Validierung fehlgeschlagen, Exit 2 = Parse-/IO-Fehler.
# ============================================================
import struct
import sys

# Default-Schwellwert: deutlich ueber "nur Himmel" (1-2 Farben) bzw.
# leichtem Dithering. Terrain mit AO/Tiefen-Shading liefert hunderte+.
DEFAULT_MIN_COLORS = 64


def read_bmp(path):
    """Liest ein unkomprimiertes 24/32-bpp BMP, gibt (w, h, list[(r,g,b)])."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 54 or data[0:2] != b"BM":
        raise ValueError(f"{path}: kein BMP (Signatur fehlt, {len(data)} Byte)")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40:
        raise ValueError(f"{path}: unerwarteter DIB-Header ({dib_size} Byte)")
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    # BI_RGB (0) und BI_BITFIELDS (3) werden unterstuetzt. raum_screenshot_bmp
    # schreibt einen BITMAPV5HEADER (124 Byte) mit comp=3 und Standard-BGRA-
    # Masks (R=0x00ff0000 G=0x0000ff00 B=0x000000ff A=0xff000000) -> Byte-Layout
    # ist B,G,R,A wie bei unkomprimiertem 32bpp, daher gleicher Pixel-Parse.
    if compression not in (0, 3):
        raise ValueError(f"{path}: komprimiert (BI={compression}) nicht unterstuetzt")
    if compression == 3 and dib_size >= 56:
        rm, gm, bm = struct.unpack_from("<III", data, 54)
        if (rm, gm, bm) != (0x00FF0000, 0x0000FF00, 0x000000FF):
            raise ValueError(
                f"{path}: BI_BITFIELDS mit Nicht-Standard-Masks "
                f"(R{rm:08x} G{gm:08x} B{bm:08x}) nicht unterstuetzt")
    if bpp not in (24, 32):
        raise ValueError(f"{path}: {bpp} bpp nicht unterstuetzt (nur 24/32)")

    top_down = height < 0
    h = abs(height)
    w = width
    bytes_per_px = bpp // 8
    row_size = ((bpp * w + 31) // 32) * 4  # 4-Byte-aligned Zeilen

    pixels = []
    for row in range(h):
        src_row = row if top_down else (h - 1 - row)
        base = pixel_offset + src_row * row_size
        for col in range(w):
            o = base + col * bytes_per_px
            if o + 3 > len(data):
                raise ValueError(f"{path}: Pixel-Daten zu kurz")
            b = data[o]
            g = data[o + 1]
            r = data[o + 2]
            pixels.append((r, g, b))
    return w, h, pixels


def analyze(path):
    w, h, px = read_bmp(path)
    colors = set(px)
    # haeufigste Farbe (typ. Himmel) und ihr Anteil
    counts = {}
    for c in px:
        counts[c] = counts.get(c, 0) + 1
    dom_color, dom_count = max(counts.items(), key=lambda kv: kv[1])
    dom_frac = dom_count / len(px) if px else 1.0
    return {
        "path": path,
        "w": w,
        "h": h,
        "pixels": len(px),
        "unique_colors": len(colors),
        "dominant": dom_color,
        "dominant_frac": dom_frac,
    }


def cmd_check(path, min_colors):
    info = analyze(path)
    print(f"[check] {path}: {info['w']}x{info['h']}, "
          f"unique_colors={info['unique_colors']}, "
          f"dominant={info['dominant']} ({info['dominant_frac']*100:.1f}%)")
    ok = True
    if info["unique_colors"] < 2:
        print(f"[check] FAIL: blank (nur {info['unique_colors']} Farbe)")
        ok = False
    if info["unique_colors"] < min_colors:
        print(f"[check] FAIL: zu wenige Farben "
              f"({info['unique_colors']} < {min_colors}) -> Terrain nicht sichtbar?")
        ok = False
    if info["dominant_frac"] > 0.995:
        print(f"[check] FAIL: {info['dominant_frac']*100:.1f}% einfarbig "
              f"-> faktisch blank")
        ok = False
    print(f"[check] {'OK' if ok else 'FAILED'}: {path}")
    return ok


def cmd_compare(a, b, min_colors):
    ia = analyze(a)
    ib = analyze(b)
    print(f"[compare] A {a}: unique={ia['unique_colors']}")
    print(f"[compare] B {b}: unique={ib['unique_colors']}")
    ok = cmd_check(a, min_colors) & cmd_check(b, min_colors)
    ca, cb = ia["unique_colors"], ib["unique_colors"]
    lo, hi = (ca, cb) if ca <= cb else (cb, ca)
    ratio = hi / max(lo, 1)
    print(f"[compare] Farbanzahl-Verhaeltnis = {ratio:.2f}x "
          f"(Schwelle <= 8x fuer 'gleiche Groessenordnung')")
    if ratio > 8:
        print("[compare] FAIL: Farbanzahl-Groessenordnung weicht zu stark ab")
        ok = False
    print(f"[compare] {'OK' if ok else 'FAILED'}")
    return ok


def main(argv):
    if len(argv) < 3:
        print(__doc__ or "usage: check|compare ...")
        return 2
    mode = argv[1]
    try:
        if mode == "check":
            mn = int(argv[3]) if len(argv) > 3 else DEFAULT_MIN_COLORS
            return 0 if cmd_check(argv[2], mn) else 1
        if mode == "compare":
            if len(argv) < 4:
                print("compare braucht zwei BMP-Pfade")
                return 2
            mn = int(argv[4]) if len(argv) > 4 else DEFAULT_MIN_COLORS
            return 0 if cmd_compare(argv[2], argv[3], mn) else 1
        print(f"unbekannter Modus: {mode}")
        return 2
    except (OSError, ValueError) as e:
        print(f"[error] {e}")
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
