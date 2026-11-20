"""Lesende Host-Prüfer für GPT, FAT und P/Q-Parität auf Testimages."""

import hashlib
import io
import os
import re
import stat
import subprocess
import sys
from argparse import ArgumentParser
from contextlib import ExitStack
from pathlib import Path


def image(pfad):
    pfad = Path(pfad).resolve(strict=True)
    if not stat.S_ISREG(pfad.stat().st_mode):
        raise ValueError(f"Reguläre Image-Datei erwartet: {pfad}")
    return pfad


def gpt_pruefen(pfad):
    ergebnis = subprocess.run(["sgdisk", "-v", image(pfad)], capture_output=True, text=True,
                             env={**os.environ, "LC_ALL": "C"}, timeout=30)
    print(ergebnis.stdout + ergebnis.stderr, end="")
    if ergebnis.returncode or not re.search(r"^No problems found\.", ergebnis.stdout, re.M):
        raise ValueError("GPT-Prüfung meldet Abweichungen")


def fat_pruefen(pfad):
    pfad = image(pfad)
    subprocess.run(["fsck.vfat", "-n", pfad], check=True, timeout=30)
    subprocess.run(["mdir", "-i", pfad, "::/"], check=True, timeout=30)


def gf8_mul(a, b):
    produkt = 0
    while b:
        if b & 1:
            produkt ^= a
        a <<= 1
        if a & 0x100:
            a ^= 0x11D
        b >>= 1
    return produkt


def paritaets_hash(daten):
    """Spur i hat Q-Koeffizient 2^i. Fehlende Sektoren am Ende sind null."""
    if not 1 <= len(daten) <= 255:
        raise ValueError("P/Q braucht 1 bis 255 Datenspuren")
    tabellen = []
    faktor = 1
    for _ in daten:
        tabellen.append(bytes(gf8_mul(faktor, byte) for byte in range(256)))
        faktor = gf8_mul(faktor, 2)
    p_hash, q_hash = hashlib.sha256(), hashlib.sha256()
    laenge = 0
    while True:
        bloecke = [datei.read(1024 * 1024) for datei in daten]
        groesse = max(map(len, bloecke))
        if groesse == 0:
            return laenge, p_hash.hexdigest(), q_hash.hexdigest()
        p, q = 0, 0
        for block, tabelle in zip(bloecke, tabellen, strict=True):
            p ^= int.from_bytes(block, "little")
            q ^= int.from_bytes(block.translate(tabelle), "little")
        p_hash.update(p.to_bytes(groesse, "little"))
        q_hash.update(q.to_bytes(groesse, "little"))
        laenge += groesse


def paritaet_pruefen(args):
    with ExitStack() as stack:
        daten = [stack.enter_context(image(pfad).open("rb")) for pfad in args.daten]
        laenge, p, q = paritaets_hash(daten)
    for name, soll, vergleich in (("P", p, args.p), ("Q", q, args.q)):
        print(f"{name} {laenge} Bytes SHA-256 {soll}")
        if vergleich:
            with image(vergleich).open("rb") as datei:
                if hashlib.file_digest(datei, "sha256").hexdigest() != soll:
                    raise ValueError(f"Abweichende Parität {name}: {vergleich}")


def selbsttest():
    daten = [io.BytesIO(bytes.fromhex(werte)) for werte in ("57 80 ff 12", "83 80 01 34", "00 ff")]
    laenge, p, q = paritaets_hash(daten)
    if laenge != 4 or p != hashlib.sha256(bytes.fromhex("d4 ff fe 26")).hexdigest():
        raise ValueError("P-Referenzvektor fehlgeschlagen")
    if q != hashlib.sha256(bytes.fromhex("4c 46 fd 7a")).hexdigest():
        raise ValueError("Q-Referenzvektor fehlgeschlagen")
    print("P/Q-Referenzvektor GF(2^8)/0x11D bytegleich")


def main():
    parser = ArgumentParser(description=__doc__)
    aktionen = parser.add_subparsers(dest="aktion", required=True)
    aktionen.add_parser("gpt").add_argument("image")
    aktionen.add_parser("fat").add_argument("image")
    paritaet = aktionen.add_parser("paritaet")
    paritaet.add_argument("daten", nargs="+")
    paritaet.add_argument("--p", help="Optionales P-Image zum SHA-Vergleich")
    paritaet.add_argument("--q", help="Optionales Q-Image zum SHA-Vergleich")
    aktionen.add_parser("selbsttest")
    args = parser.parse_args()
    try:
        if args.aktion == "gpt":
            gpt_pruefen(args.image)
        elif args.aktion == "fat":
            fat_pruefen(args.image)
        elif args.aktion == "paritaet":
            paritaet_pruefen(args)
        else:
            selbsttest()
    except (OSError, ValueError, subprocess.SubprocessError) as fehler:
        sys.exit(f"P11-Hostprüfung: {fehler}")


if __name__ == "__main__":
    main()
