"""Vergleicht die beiden QEMU-Kernüberschriften mit DejaVu Sans Bold."""

import re
import subprocess
import sys
from pathlib import Path


def schriftwert(header, name):
    return int(re.search(rf"^#define MOO_FONT_AA_FETT_{name} (\d+)$", header, re.M)[1])


def schriftdaten(header, name):
    inhalt = re.search(rf"\b{name}\s*\[[^\]]+\]\s*=\s*\{{(.*?)\}};", header, re.S)[1]
    return bytes(int(wert) for wert in re.findall(r"\d+", inhalt))


def pixelvergleich(bild, bildbreite, bildhoehe, header, text, x, y, skala, vorn, hinten):
    breite = schriftwert(header, "W")
    hoehe = schriftwert(header, "H")
    erstes = schriftwert(header, "FIRST")
    glyphen = schriftdaten(header, "moo_font_aa_fett")
    vorschuebe = schriftdaten(header, "moo_font_aa_fett_adv")
    indizes = [ord(zeichen) - erstes for zeichen in text]
    feldbreite = (sum(vorschuebe[i] for i in indizes[:-1]) + breite) * skala
    feldhoehe = hoehe * skala
    if x + feldbreite > bildbreite or y + feldhoehe > bildhoehe:
        sys.exit(f"QEMU-Bild ist zu klein für den Pixelvergleich von {text!r}")

    zeilen = [bytearray(bytes(hinten) * feldbreite) for _ in range(feldhoehe)]
    start = 0
    for index in indizes:
        maske = index * breite * hoehe
        for zeile in range(hoehe):
            for spalte in range(breite):
                alpha = glyphen[maske + zeile * breite + spalte]
                if alpha == 0:
                    continue
                for sy in range(zeile * skala, (zeile + 1) * skala):
                    for sx in range(start + spalte * skala, start + (spalte + 1) * skala):
                        for kanal in range(3):
                            offset = sx * 3 + kanal
                            bg = zeilen[sy][offset]
                            zeilen[sy][offset] = (vorn[kanal] * alpha + bg * (255 - alpha) + 127) // 255
        start += vorschuebe[index] * skala

    for zeile, erwartung in enumerate(zeilen):
        bildstart = ((y + zeile) * bildbreite + x) * 3
        wirklich = bild[bildstart:bildstart + len(erwartung)]
        if wirklich != erwartung:
            byte = next(i for i, (ist, soll) in enumerate(zip(wirklich, erwartung)) if ist != soll)
            px = (byte // 3) * 3
            sys.exit(f"{text!r} bei ({x + px // 3},{y + zeile}): "
                     f"RGB {tuple(wirklich[px:px+3])}, erwartet {tuple(erwartung[px:px+3])}")
    print(f"      OK: {text!r} DejaVu Sans Bold {skala}×, {feldbreite * feldhoehe} Pixel bytegleich")


def main():
    header, screenshot = map(Path, sys.argv[1:])
    schrift = header.read_text(encoding="utf-8")
    breite, hoehe = map(int, subprocess.check_output(
        ["magick", "identify", "-format", "%w %h", screenshot]).split())
    bild = subprocess.check_output(["magick", screenshot, "-alpha", "off", "-depth", "8", "RGB:-"])
    if len(bild) != breite * hoehe * 3:
        sys.exit("QEMU-Pixeldatei hat unerwartete RGB-Größe")

    pixelvergleich(bild, breite, hoehe, schrift, "MOO-KERNEL OK", 64, 60, 4,
                   (20, 20, 20), (255, 196, 64))
    pixelvergleich(bild, breite, hoehe, schrift, "LADER UND KERNSTEUERUNG IN MOO", 64, 220, 2,
                   (240, 240, 240), (32, 96, 128))


if __name__ == "__main__":
    main()
