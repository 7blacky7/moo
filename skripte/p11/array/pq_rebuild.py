"""Physische P/Q-Paarfehler, Rebuild, Journal und unabhängige Referenz."""
from itertools import combinations
import os
from pathlib import Path
import struct
import subprocess
import sys

from gate import ROOT, SEITE, datenabbild, hashdatei, laufpfad, setze_seite, starten
from host import gf8_mul

SEITEN = (4, 7, 7, 7)


def pruefe(binaer, pfade, modus, soll):
    ist = starten(binaer, pfade, modus)
    if ist != soll:
        raise RuntimeError(f"{modus}: BlockFehler {ist >> 32}/{ist & 0xffffffff}, erwartet {soll}, Wert {ist}")


def bilder(ort):
    ort.mkdir()
    pfade = [ort / f"{name}-{i}.img" for name in ("daten", "meta") for i in range(6)]
    links = bytearray(datenabbild(4, 11))
    rechts = bytearray(datenabbild(7, 153))
    for i, pfad in enumerate(pfade):
        with pfad.open("xb") as datei:
            if i == 0:
                datei.write(links)
            elif i == 3:
                datei.write(rechts)
            else:
                datei.truncate((7 if i < 6 else 10) * SEITE)
    return pfade, links, rechts


def p_q_referenz(links, rechts):
    a = bytes(links).ljust(len(rechts), b"\0")
    # Datenplätze 0 und 3 erhalten Koeffizienten 1 und 8 in GF(0x11D).
    skaliert = bytes(gf8_mul(k, 8) for k in range(256))
    return (
        bytes(x ^ y for x, y in zip(a, rechts, strict=True)),
        bytes(x ^ y for x, y in zip(a, bytes(rechts).translate(skaliert), strict=True)),
    )


def paare(binaer, ort):
    for a, b in combinations(range(4), 2):
        key = f"{a}{b}"
        pfade, links, rechts = bilder(ort / f"paar-{key}")
        pruefe(binaer, pfade, "a", 7)
        p, q = p_q_referenz(links, rechts)
        assert pfade[1].read_bytes() == p and pfade[2].read_bytes() == q
        fehlend = [pfade[i] for i in (a, b, a + 6, b + 6)]
        vor = [hashdatei(x) for x in fehlend]
        pruefe(binaer, pfade, "d" + key, 1)
        setze_seite(links, 1, 0xA1)
        setze_seite(rechts, 5, 0xB5)
        pruefe(binaer, pfade, "b" + key, 2)
        setze_seite(links, 0, 0xC0)
        setze_seite(rechts, 3, 0xD3)
        pruefe(binaer, pfade, "r" + key, ((SEITEN[a] - 2) << 32) | SEITEN[b])
        assert vor == [hashdatei(x) for x in fehlend]
        pfad_aktuell = pfade[:4]
        pfad_aktuell[a], pfad_aktuell[b] = pfade[4:6]
        p, q = p_q_referenz(links, rechts)
        for i, soll in enumerate((links, p, q, rechts)):
            ist = pfad_aktuell[i].read_bytes()
            assert ist[:len(soll)] == soll
            assert ist[len(soll):] == bytes(len(ist) - len(soll))
        alle_vor = [hashdatei(x) for x in pfade]
        pruefe(binaer, pfade, "x" + key, 1)
        assert alle_vor == [hashdatei(x) for x in pfade]
        print(f"PASS Paar {key}, Rebuild bei 2 fortgesetzt, alle Daten/P/Q bytegleich, drittes fehlendes Mitglied abgewiesen")


def sektorheilung(binaer, ort):
    pfade, links, rechts = bilder(ort / "sektorheilung")
    pruefe(binaer, pfade, "a", 7)
    vor = [hashdatei(x) for x in (pfade[1], pfade[7])]
    with pfade[0].open("r+b") as datei:
        datei.seek(SEITE)
        datei.write(bytes(512))
        datei.flush()
        os.fsync(datei.fileno())
    pruefe(binaer, pfade, "h", 1)
    assert pfade[0].read_bytes() == links
    assert vor == [hashdatei(x) for x in (pfade[1], pfade[7])]
    p, q = p_q_referenz(links, rechts)
    assert pfade[1].read_bytes() == p and pfade[2].read_bytes() == q
    print("PASS P fehlt, 512-Byte-Lesefehler über Q geheilt, Datenmitglied aktiv und Bytes korrekt")


def q_bild_beschaedigen(meta):
    inhalt = meta.read_bytes()
    koepfe = []
    for seite in (2, 6):
        kopf = seite * SEITE + 3968
        if inhalt[kopf:kopf + 8] == b"MOOPQST1":
            folge = struct.unpack_from("<Q", inhalt, kopf + 8)[0]
            beine = struct.unpack_from("<I", inhalt, kopf + 32)[0]
            koepfe.append((folge, seite, beine))
    _, seite, beine = max(koepfe)
    assert beine == 7
    with meta.open("r+b") as datei:
        adr = (seite + 3) * SEITE + 73
        datei.seek(adr)
        datei.write(bytes([inhalt[adr] ^ 1]))
        datei.flush()
        os.fsync(datei.fileno())


def journal(binaer, ort):
    pfade, links, rechts = bilder(ort / "journal")
    pruefe(binaer, pfade, "a", 7)
    altes_q = pfade[2].read_bytes()
    starten(binaer, pfade, "c", abbruch=True)
    setze_seite(links, 2, 0xE2)
    p, q = p_q_referenz(links, rechts)
    assert pfade[0].read_bytes() == links and pfade[1].read_bytes() == p
    assert pfade[2].read_bytes() == altes_q and altes_q != q
    q_bild_beschaedigen(pfade[8])
    fehlend = [pfade[i] for i in (0, 1, 6, 7)]
    vor = [hashdatei(x) for x in fehlend]
    pruefe(binaer, pfade, "j01", 1)
    assert vor == [hashdatei(x) for x in fehlend]
    assert pfade[2].read_bytes() == q
    print(f"PASS SIGKILL nach D und P, D/P fehlen beim Neustart, Q-Journal-Fallback und rekonstruiertes D, SHA-256 {hashdatei(pfade[2])}")


def gate(ort, compiler):
    ort = laufpfad(ort)
    if any(ort.iterdir()):
        raise RuntimeError(f"Für das Gate ein leeres Laufverzeichnis verwenden: {ort}")
    binaer = ort / "array_pq_rebuild_host"
    subprocess.run([compiler, "compile", "compiler/tests/p11/array_pq_rebuild_host.moos", "-o", binaer], cwd=ROOT, check=True)
    paare(binaer, ort)
    sektorheilung(binaer, ort)
    journal(binaer, ort)


if __name__ == "__main__":
    gate(Path(sys.argv[1]), sys.argv[2])
