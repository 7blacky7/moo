"""Prüft USB-HID, Schriftwahl und die daraus gerasterten QEMU-Pixel."""

import re
import sys
from pathlib import Path


TEXT = 'azy!"§$%&/()=?@€{}[]\\|~äöüÄÖÜ°´ßÄäyz'
TASTEN = TEXT[:-4] + "€\bä\b" + TEXT[-4:]
ASCII_GRUPPEN = tuple("".join(map(chr, range(a, b))) for a, b in ((32, 64), (64, 96), (96, 127)))
VORDERGRUND = bytes((255, 230, 160))
HINTERGRUND = bytes((20, 55, 75))
CURSOR = bytes((255, 196, 64))

# Eigenständiger Pixelbeleg für die bisherige Kompaktschrift.
KOMPAKT = {
    "a": "75755", "y": "55222", "z": "71247", "!": "20222",
    '"': "55000", "§": "36536", "$": "27632", "%": "51245",
    "&": "25253", "/": "11244", "(": "12221", ")": "42224",
    "=": "07070", "?": "71302", "@": "25743", "€": "34643",
    "{": "12621", "}": "42324", "[": "32223", "]": "62226",
    "\\": "44211", "|": "22222", "~": "00520",
    "ä": "50357", "ö": "50757", "ü": "50557",
    "Ä": "50275", "Ö": "50252", "Ü": "50552", "ß": "65654",
    "°": "25200", "´": "12000",
}


def ascii_tasten(gruppe):
    richtung = {
        " ": "spc", "-": "minus", "=": "equal", "[": "bracket_left",
        "]": "bracket_right", "\\": "backslash", ";": "semicolon",
        "'": "apostrophe", "\x60": "grave_accent", ",": "comma",
        ".": "dot", "/": "slash",
    }
    unshift = dict(zip(
        '~!@#$%^&*()_+{}|:"<>?',
        "\x601234567890-=[]\\;',./",
        strict=True,
    ))
    for c in ASCII_GRUPPEN[gruppe]:
        if c.isupper() or c in unshift:
            taste = unshift.get(c, c.lower())
            print("shift-" + richtung.get(taste, taste))
        else:
            print(richtung.get(c, c))


def pruefe_tasten(pfad):
    log = pfad.read_bytes().replace(b"\r", b"")
    codes = re.findall(rb"MOO-KERNEL USB TASTE ([0-9A-F]{16})\n", log)
    ist = "".join(chr(int(code, 16)) for code in codes)
    soll = TASTEN + "".join(ASCII_GRUPPEN)
    if ist != soll:
        sys.exit(f"HID-Zeichen {ist!r}, erwartet {soll!r}")
    if re.findall(rb"MOO-KERNEL TASTATUR LAYOUT (DE|US)\n", log) != [b"US", b"DE"] * 2:
        sys.exit("F12-Umschaltungen zwischen US und DE fehlen")
    if re.findall(rb"MOO-KERNEL SCHRIFT (MONO|KOMPAKT)\n", log) != [b"MONO", b"KOMPAKT", b"MONO"]:
        sys.exit("DejaVu-Standard oder F11-Schriftwahl fehlt")
    for zeichen in "§°´äöüÄÖÜß€":
        marker = f"MOO-KERNEL USB TASTE {ord(zeichen):016X}\n".encode()
        if marker + zeichen.encode("utf-8") + b"\n" not in log:
            sys.exit(f"Serielles UTF-8-Echo für {zeichen!r} fehlt")


def bild_lesen(pfad):
    with pfad.open("rb") as datei:
        if datei.readline() != b"P6\n":
            sys.exit("Screendump ist kein P6-PPM")
        breite, hoehe = map(int, datei.readline().split())
        if datei.readline() != b"255\n":
            sys.exit("Screendump hat keine 8-Bit-Farbkanäle")
        bild = datei.read()
    if len(bild) != breite * hoehe * 3 or breite < 160 or hoehe < 200:
        sys.exit("Ungültige Framebuffer-Abmessungen")

    def pixel(x, y, erwartet, kontext):
        if not (0 <= x < breite and 0 <= y < hoehe):
            sys.exit(f"{kontext}: Pixel ({x}, {y}) liegt außerhalb des Framebuffers")
        index = 3 * (y * breite + x)
        gelesen = bild[index : index + 3]
        if gelesen != erwartet:
            sys.exit(f"{pfad.name}, {kontext} bei ({x}, {y}): {gelesen.hex()} statt {erwartet.hex()}")

    return breite, hoehe, pixel


def pruefe_cursor(pixel, anfang_x, y, dx):
    for x in range(dx):
        for zeile in range(2):
            pixel(anfang_x + x, y + zeile, CURSOR if x < 8 else HINTERGRUND, "Cursor")


def pruefe_kompakt(pfad):
    _, hoehe, pixel = bild_lesen(pfad)
    for index, zeichen in enumerate(TEXT):
        for dy in range(15):
            maske = int(KOMPAKT[zeichen][dy // 3])
            for dx in range(12):
                gesetzt = dx < 9 and maske & (4 >> (dx // 3))
                pixel(52 + index * 12 + dx, hoehe - 52 + dy,
                      VORDERGRUND if gesetzt else HINTERGRUND, repr(zeichen))
    cursor_x = 52 + len(TEXT) * 12
    for dx in range(12):
        for dy in range(15):
            pixel(cursor_x + dx, hoehe - 52 + dy, HINTERGRUND, "Textende")
    pruefe_cursor(pixel, cursor_x, hoehe - 35, 12)


def mono_lesen(pfad):
    quelle = pfad.read_text()

    def define(name):
        return int(re.search(rf"^#define MOO_FONT_AA_MONOSPACE_{name} (\d+)$", quelle, re.M)[1])

    def array(name):
        treffer = re.search(rf"\b{name}\[[^]]+\] = \{{([^}}]+)\}};", quelle)
        return tuple(map(int, re.findall(r"\d+", treffer[1])))

    breite, hoehe, erstes = (define(name) for name in ("W", "H", "FIRST"))
    basis = define("BASE_COUNT")
    extras = array("moo_font_aa_extra_cp")
    vorschuebe = array("moo_font_aa_monospace_adv")
    daten = bytes(array("moo_font_aa_monospace"))
    groesse = breite * hoehe
    cps = tuple(range(erstes, erstes + basis)) + extras
    if len(cps) != define("TOTAL_COUNT") or len(daten) != len(cps) * groesse:
        sys.exit("Monospace-Masken unvollständig")
    if len(vorschuebe) != len(cps) or vorschuebe[0] == 0:
        sys.exit("Monospace-Metrik unvollständig")

    masken = {chr(cp): daten[i * groesse : (i + 1) * groesse] for i, cp in enumerate(cps)}
    zeichen = set("".join(ASCII_GRUPPEN) + "§°´äöüÄÖÜß€")
    metrik = dict(zip(map(chr, cps), vorschuebe, strict=True))
    if any(metrik.get(c) != vorschuebe[0] for c in zeichen):
        sys.exit("Druckbares Prüfzeichen hat einen abweichenden Vorschub")
    if any(c not in masken or (c != " " and not any(masken[c])) for c in zeichen):
        sys.exit("Druckbare DejaVu-Glyphe fehlt")
    if len({masken[c] for c in zeichen}) != len(zeichen):
        sys.exit("Verschiedene Prüfzeichen teilen eine Glyphenmaske")
    return breite, hoehe, vorschuebe[0], masken


def pruefe_mono(pfad, text, schrift):
    fb_breite, fb_hoehe, pixel = bild_lesen(pfad)
    b, h, dx, masken = schrift
    # Der Kernel behält so viele Skalare, wie mit beiden Schriften passen.
    max_zeichen = min(255, (fb_breite - 116) // 12)
    text = text[-max_zeichen:]
    panel_breite = max(len(text) * dx + dx, (len(text) - 1) * dx + b)
    erwartet = [[HINTERGRUND] * panel_breite for _ in range(h)]
    for i, c in enumerate(text):
        maske = masken[c]
        for y in range(h):
            for x in range(b):
                a = maske[y * b + x]
                px = i * dx + x
                erwartet[y][px] = bytes(
                    (fg * a + bg * (255 - a) + 127) // 255
                    for fg, bg in zip(VORDERGRUND, erwartet[y][px], strict=True)
                )
    for y, zeile in enumerate(erwartet):
        for x, farbe in enumerate(zeile):
            pixel(52 + x, fb_hoehe - 52 + y, farbe, "AA-Text")
    pruefe_cursor(pixel, 52 + len(text) * dx, fb_hoehe - 50 + h, dx)
    return set(text)


def pruefe(pfad, font):
    pruefe_tasten(pfad / "serial.log")
    schrift = mono_lesen(font)
    belegt = pruefe_mono(pfad / "tastatur.ppm", TEXT, schrift)
    pruefe_kompakt(pfad / "tastatur-kompakt.ppm")
    text = TEXT
    for i, gruppe in enumerate(ASCII_GRUPPEN):
        text += gruppe
        belegt.update(pruefe_mono(pfad / f"tastatur-ascii-{i}.ppm", text, schrift))
    soll = set("".join(ASCII_GRUPPEN) + "§°´äöüÄÖÜß€")
    if belegt != soll:
        sys.exit(f"Glyphen außerhalb der geprüften Eingabezeilen: {sorted(soll - belegt)}")
    print(f"      OK: HID DE/US, F11, {len(belegt)} AA-Glyphen und UTF-8-Cursor pixelgleich")


if __name__ == "__main__":
    if sys.argv[1] == "--ascii-tasten":
        ascii_tasten(int(sys.argv[2]))
    else:
        pruefe(Path(sys.argv[1]), Path(sys.argv[2]))
