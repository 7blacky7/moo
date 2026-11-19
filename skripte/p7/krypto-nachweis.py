#!/usr/bin/env python3
"""Krypto-Statusmarker aus QEMU-COM1 oder dem N100-Netzlog prüfen."""
import pathlib
import re
import sys

KATS = (
    "SHA256", "SHA512", "HMAC-SHA256", "HMAC-SHA512", "X25519",
    "ED25519", "CHACHA20-POLY1305", "HMAC-DRBG", "ENTROPIE-LOGIK",
)
PREFIX = "MOO-KERNEL SAMMEL KRYPTO"


def pruefe(log, profil):
    muster = rb"MOO-KERNEL SAMMEL KRYPTO ([A-Z0-9-]+) ([0-9A-F]{16})(?![0-9A-F])"
    marker = [(name.decode("ascii"), int(wert, 16)) for name, wert in re.findall(muster, log)]
    erwartet = ("START", *KATS, "RDSEED", "RDRAND", "QUELLE", "ENDE")
    if tuple(name for name, _ in marker) != erwartet:
        raise SystemExit("Krypto-Statusmarker fehlen, wiederholen sich oder stehen in falscher Reihenfolge")
    werte = dict(marker)
    for name in ("START", *KATS):
        if werte[name] != 1:
            raise SystemExit(f"Krypto-Selbsttest fehlgeschlagen ({name})")

    rdseed, rdrand, quelle, ende = (werte[name] for name in erwartet[-4:])
    soll = {"1": (1, 1, 1, 1), "rdrand": (0, 1, 2, 1), "ohne-quelle": (0, 0, 0, 0)}
    if profil == "n100":
        ok = ende == 1 and (
            (rdseed == 1 and rdrand in (0, 1, 2) and quelle == 1) or
            (rdseed in (0, 2) and rdrand == 1 and quelle == 2)
        )
    else:
        ok = (rdseed, rdrand, quelle, ende) == soll[profil]
    for name, wert in marker:
        print(f"{PREFIX} {name} {wert:016X}")
    if not ok:
        raise SystemExit(f"Entropie-Status passt nicht zum Profil {profil}")
    print(f"Krypto-KATs und Entropie-Status für Profil {profil} belegt")


if __name__ == "__main__":
    pfad, profil = sys.argv[1:]
    pruefe(pathlib.Path(pfad).read_bytes(), profil)
