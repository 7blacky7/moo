"""Prüft Rebuild, Einzelstörung und das persistente Journal auf Testabbildern."""

import os
from pathlib import Path
import struct
import subprocess
import sys

from gate import ROOT, SEITE, datenabbild, hashdatei, laufpfad, setze_seite, starten, xor_referenz


def pruefe(binaer, pfade, modus, erwartet):
    ergebnis = starten(binaer, pfade, modus)
    if ergebnis != erwartet:
        raise RuntimeError(
            f"Rebuild-Probe {modus}: Wert {ergebnis}, BlockFehler "
            f"{ergebnis >> 32}/{ergebnis & 0xffffffff}, erwartet {erwartet}"
        )


def intent_kopie_beschaedigen(meta):
    inhalt = meta.read_bytes()
    koepfe = []
    for seite in (2, 5):
        kopf = seite * SEITE + 1088
        if inhalt[kopf:kopf + 8] == b"MOORBLD1":
            folge = struct.unpack_from("<Q", inhalt, kopf + 8)[0]
            beine = struct.unpack_from("<I", inhalt, kopf + 36)[0]
            koepfe.append((folge, seite, beine))
    _, seite, beine = max(koepfe)
    if beine != 3:
        raise RuntimeError(f"Anstehende Schreibbilder fehlen: Journalbeine {beine}")
    with meta.open("r+b") as datei:
        adr = (seite + 1) * SEITE + 23
        datei.seek(adr)
        datei.write(bytes([inhalt[adr] ^ 1]))
        datei.flush()
        os.fsync(datei.fileno())


def gate(ort, compiler):
    ort = laufpfad(ort)
    if any(ort.iterdir()):
        raise RuntimeError(f"Für das Gate ein leeres Laufverzeichnis verwenden: {ort}")
    binaer = ort / "array_rebuild_host"
    subprocess.run(
        [compiler, "compile", "compiler/tests/p11/array_rebuild_host.moos", "-o", binaer],
        cwd=ROOT, check=True,
    )
    namen = ("daten-a", "daten-b", "p", "ersatz-a", "ersatz-p",
             "meta-a", "meta-b", "meta-p", "meta-ersatz-a", "meta-ersatz-p")
    pfade = [ort / f"{name}.img" for name in namen]
    daten_a = bytearray(datenabbild(4, 11))
    daten_b = bytearray(datenabbild(7, 153))
    for pfad, inhalt in zip(pfade[:2], (daten_a, daten_b), strict=True):
        with pfad.open("xb") as datei:
            datei.write(inhalt)
    for pfad, seiten in zip(pfade[2:], (7, 4, 7, 8, 8, 136, 8, 136), strict=True):
        with pfad.open("xb") as datei:
            datei.truncate(seiten * SEITE)

    pruefe(binaer, pfade, "a", 1)
    vor = [hashdatei(x) for x in pfade]
    pruefe(binaer, pfade, "v", 1)
    assert vor == [hashdatei(x) for x in pfade]
    with pfade[0].open("r+b") as datei:
        datei.seek(SEITE)
        datei.write(bytes(512))
        datei.flush()
        os.fsync(datei.fileno())
    pruefe(binaer, pfade, "h", 1)
    assert pfade[0].read_bytes() == daten_a
    print("PASS 512-Byte-Lesestörung geheilt, Daten bytegleich, Mitglied aktiv")

    pruefe(binaer, pfade, "d", 1)
    assert pfade[0].read_bytes() == daten_a
    setze_seite(daten_a, 2, 0x51)
    setze_seite(daten_b, 3, 0x62)
    pruefe(binaer, pfade, "b", 2)
    setze_seite(daten_a, 0, 0xA0)
    setze_seite(daten_a, 3, 0xA3)
    assert pfade[3].read_bytes() == daten_a[:2 * SEITE] + bytes(2 * SEITE)
    print("PASS Datenplatte ausgefallen, Schreibaufträge auf Ersatzpräfix und logischen Rest")

    altes_p = pfade[2].read_bytes()
    starten(binaer, pfade, "c", abbruch=True)
    setze_seite(daten_b, 2, 0xC2)
    assert pfade[1].read_bytes() == daten_b
    assert pfade[2].read_bytes() == altes_p
    intent_kopie_beschaedigen(pfade[8])
    pruefe(binaer, pfade, "r", 2)
    assert pfade[3].read_bytes() == daten_a
    assert pfade[1].read_bytes() == daten_b
    print(f"PASS SIGKILL zwischen Daten und P, Journal-Replikat wiederholt, Fortschritt 2/4 erhalten, P {xor_referenz([pfade[3], pfade[1]], pfade[2])}")

    pruefe(binaer, pfade, "w", 1)
    setze_seite(daten_a, 3, 0xB3)
    setze_seite(daten_b, 6, 0xB6)
    setze_seite(daten_b, 0, 0xE0)
    setze_seite(daten_b, 5, 0xE5)
    pruefe(binaer, pfade, "s", 6)
    assert pfade[3].read_bytes() == daten_a
    assert pfade[1].read_bytes() == daten_b
    print(f"PASS P-Ausfall, Schreibaufträge und Neustart 1/7, Ersatz-P {xor_referenz([pfade[3], pfade[1]], pfade[4])}")

    vor = [hashdatei(x) for x in pfade]
    pruefe(binaer, pfade, "x", 1)
    pruefe(binaer, pfade, "t", 1)
    assert vor == [hashdatei(x) for x in pfade]
    print("PASS veraltete Identität und zwei fehlende Mitglieder abgewiesen, Abbilder bytegleich")

    altes_p = pfade[4].read_bytes()
    starten(binaer, pfade, "j", abbruch=True)
    setze_seite(daten_a, 2, 0xDA)
    assert pfade[3].read_bytes() == daten_a
    assert pfade[4].read_bytes() == altes_p
    fehlend = [pfade[3], pfade[8]]
    vor = [hashdatei(x) for x in fehlend]
    pruefe(binaer, pfade, "o", 1)
    assert vor == [hashdatei(x) for x in fehlend]
    print(f"PASS SIGKILL und Datenziel beim Neustart fehlend, logisches Lesen bytegleich, P {xor_referenz([pfade[3], pfade[1]], pfade[4])}")


if __name__ == "__main__":
    gate(Path(sys.argv[1]), sys.argv[2])
