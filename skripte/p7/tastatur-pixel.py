"""Prüft den xHCI-HID-Eingabepfad bis zum Framebuffer in uefi-smoke.sh."""

import re
import sys
from pathlib import Path


TEXT = 'azy!"§$%&/()=?@€{}[]\\|~äöüÄÖÜßÄäyz'
TASTEN = TEXT[:-4] + "€\bä\b" + TEXT[-4:]
# Erwartete 3x5-Normalformen, eine Oktalziffer pro Pixelzeile, MSB links.
GLYPHEN = {
    "a": "75755",
    "y": "55222",
    "z": "71247",
    "!": "20222",
    '"': "55000",
    "§": "36536",
    "$": "27632",
    "%": "51245",
    "&": "25253",
    "/": "11244",
    "(": "12221",
    ")": "42224",
    "=": "07070",
    "?": "71302",
    "@": "25743",
    "€": "34643",
    "{": "12621",
    "}": "42324",
    "[": "32223",
    "]": "62226",
    "\\": "44211",
    "|": "22222",
    "~": "00520",
    "ä": "50357",
    "ö": "50757",
    "ü": "50557",
    "Ä": "50275",
    "Ö": "50252",
    "Ü": "50552",
    "ß": "65654",
}
VORDERGRUND = bytes((255, 230, 160))
HINTERGRUND = bytes((20, 55, 75))
CURSOR = bytes((255, 196, 64))


def pruefe_tasten(pfad):
    log = pfad.read_bytes().replace(b"\r", b"")
    codes = re.findall(rb"MOO-KERNEL USB TASTE ([0-9A-F]{16})\n", log)
    ist = "".join(chr(int(code, 16)) for code in codes)
    if ist != TASTEN:
        sys.exit(f"HID-Zeichen {ist!r}, erwartet {TASTEN!r}")
    if re.findall(rb"MOO-KERNEL TASTATUR LAYOUT (DE|US)\n", log) != [b"US", b"DE"]:
        sys.exit("F12-Umschaltung US und zurück zu DE fehlt")
    for zeichen in "§äöüÄÖÜß€":
        marker = f"MOO-KERNEL USB TASTE {ord(zeichen):016X}\n".encode()
        if marker + zeichen.encode("utf-8") + b"\n" not in log:
            sys.exit(f"Serielles UTF-8-Echo für {zeichen!r} fehlt")


def pruefe_pixel(pfad):
    with pfad.open("rb") as datei:
        if datei.readline() != b"P6\n":
            sys.exit("Screendump ist kein P6-PPM")
        breite, hoehe = map(int, datei.readline().split())
        if datei.readline() != b"255\n":
            sys.exit("Screendump hat keine 8-Bit-Farbkanäle")
        bild = datei.read()
    if len(bild) != breite * hoehe * 3 or breite < 64 + len(TEXT) * 12 or hoehe < 80:
        sys.exit("Ungültige Framebuffer-Abmessungen")

    def pixel(x, y, erwartet, kontext):
        index = 3 * (y * breite + x)
        gelesen = bild[index : index + 3]
        if gelesen != erwartet:
            sys.exit(f"{kontext} bei ({x}, {y}): {gelesen.hex()} statt {erwartet.hex()}")

    for index, zeichen in enumerate(TEXT):
        for dy in range(15):
            maske = int(GLYPHEN[zeichen][dy // 3])
            for dx in range(12):
                gesetzt = dx < 9 and maske & (4 >> (dx // 3))
                pixel(52 + index * 12 + dx, hoehe - 52 + dy,
                      VORDERGRUND if gesetzt else HINTERGRUND, repr(zeichen))

    # Leere Zelle hinter dem Text und Cursor am nächsten Zeichenplatz.
    cursor_x = 52 + len(TEXT) * 12
    for dx in range(12):
        for dy in range(15):
            pixel(cursor_x + dx, hoehe - 52 + dy, HINTERGRUND, "Textende")
        for dy in range(2):
            pixel(cursor_x + dx, hoehe - 35 + dy,
                  CURSOR if dx < 8 else HINTERGRUND, "Cursor")


if __name__ == "__main__":
    pruefe_tasten(Path(sys.argv[2]))
    pruefe_pixel(Path(sys.argv[1]))
    print(f"      OK: HID DE/US, {len(TEXT)} Glyphen und UTF-8-Cursor pixelgleich")
