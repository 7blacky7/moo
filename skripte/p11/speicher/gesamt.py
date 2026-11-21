"""P11-Abnahme auf privaten QEMU-Images über drei Boots der installierten ESP."""

import hashlib
import io
import json
import os
import struct
import subprocess
import sys
import uuid
from argparse import ArgumentParser, REMAINDER

from geschirr import MIB, laufpfad
from host import gpt_pruefen, image, paritaets_hash
from installer_probe import (
    INSTALL_OK, ausfuehren, digest, gastgeraete, starten, target_pruefen, usb_bauen,
)


LAST_BYTES = 2 * 1024 * MIB
TRANSFER_BYTES = 128 * 1024
DATEN_START = MIB
DATEN_BYTES = 2 * MIB
ESP = b"MOO-LADER ESP-KERN EIGENES GERAET"
KERN_OK = b"MOO-KERNEL OK"
FEHLER = b"MOO-GESAMT FEHLER"
RING3_EXIT = b"MOO-GESAMT RING3 EXIT 0000000000000000"
RING3_SCHREIBEN = b"MOO-GESAMT RING3 SCHREIBEN OK"
RING3_LESEN = b"MOO-GESAMT RING3 LESEN OK"
METADATA_TYP = "b98a8d36-8d8c-41cf-9a31-de10dc211501"
DATEN_TYP = "8a36f3c8-e91a-4bd8-a4e9-202611210125"


def vertragsbytes(elf, daten, vorlage, ersatz):
    if len(vorlage) != len(ersatz):
        raise ValueError("Längenänderung in ELF-Vertrag")
    if daten[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError(f"Kein Little-Endian-ELF64: {elf}")
    phoff, = struct.unpack_from("<Q", daten, 32)
    phgroesse, phzahl = struct.unpack_from("<HH", daten, 54)
    if phgroesse != 56 or phoff + phgroesse * phzahl > len(daten):
        raise ValueError(f"Ungültige ELF-Programmkopftabelle: {elf}")
    treffer = set()
    for pos in range(phoff, phoff + phgroesse * phzahl, phgroesse):
        art, flags, ab, _, _, bytes_im_elf, _, _ = struct.unpack_from("<IIQQQQQQ", daten, pos)
        if art == 1 and flags & 2:
            if ab + bytes_im_elf > len(daten):
                raise ValueError(f"Ungültiges ELF-Segment: {elf}")
            i = daten.find(vorlage, ab, ab + bytes_im_elf)
            while i >= 0:
                treffer.add(i)
                i = daten.find(vorlage, i + 1, ab + bytes_im_elf)
    if len(treffer) != 1:
        raise ValueError(f"Vertrag {vorlage[:8]!r} nicht eindeutig in schreibbarem PT_LOAD: {elf}")
    pos = treffer.pop()
    daten[pos:pos + len(ersatz)] = ersatz


def vertrag(pfad, marke, vorher, nachher):
    elf = image(laufpfad(pfad))
    daten = bytearray(elf.read_bytes())
    vertragsbytes(elf, daten, marke + struct.pack("<Q", vorher), marke + struct.pack("<Q", nachher))
    elf.write_bytes(daten)


def n100_inventar(pfad):
    elf = image(laufpfad(pfad))
    daten = bytearray(elf.read_bytes())
    # P7-T27, Thought 1adb934c: freigegebene Lesediagnose für diese SSD.
    serie = b"2024082400000056"
    leere_nvme = struct.pack("<8Q", 0x4E564D45524F3031, 65536, 1, 0, 0, 0, 0, 0)
    ziel_nvme = struct.pack("<4Q", 0x4E564D45524F3031, 0x3000, 1, len(serie)) + serie.ljust(32, b"\0")
    vertragsbytes(elf, daten, leere_nvme, ziel_nvme)
    vertragsbytes(elf, daten, b"MOOSAMML" + bytes(16), b"MOOSAMML" + struct.pack("<2Q", 1, 0x2263126F))
    # Auf dem Hardware-Stick bleiben Installer- und QEMU-Schreibprofile aus.
    for marke in (b"MOGESAMT", b"MOINST01", b"MOONVWR1", b"MOAHCI01", b"MOUWRK01"):
        deaktiviert = marke + bytes(8)
        vertragsbytes(elf, daten, deaktiviert, deaktiviert)
    elf.write_bytes(daten)
    print("N100-Inventar: NVMe 06:00.0, NSID 1 und GPT, SATA 00:17.0 im PCI-Inventar")


def platte_anlegen(lauf, nummer):
    pfad = lauf / f"platte-{nummer}.img"
    with pfad.open("xb") as datei:
        datei.truncate(8 * MIB)
    disk = uuid.UUID(int=0x20261121012500008000000000000001 + nummer)
    ausfuehren("sgdisk", "--clear", f"--disk-guid={disk}", "--new=1:2048:6143",
               f"--typecode=1:{DATEN_TYP}", "--change-name=1:MooFS",
               "--new=2:6144:10239", f"--typecode=2:{METADATA_TYP}",
               "--change-name=2:MoOS-Array", pfad)
    gpt_pruefen(pfad)
    return pfad


def array_geraete(platten, ersatz):
    args = ["-device", "ahci,id=gesamt,addr=0xc"]
    for nummer in range(1 if ersatz else 0, 6):
        args.extend(sata_geraet(platten, nummer, f"gesamt.{nummer}"))
    if ersatz:
        args.extend(["-device", "ahci,id=ersatz,addr=0xb"])
        args.extend(sata_geraet(platten, 6, "ersatz.0"))
    return args


def sata_geraet(platten, nummer, bus):
    platte = image(platten[nummer])
    name = f"platte{nummer}"
    # Die QEMU-Phasen enden jeweils per QMP quit. Der Host flusht die privaten
    # Images vor dem Abgleich. Einzelne Journal-Flushs sollen das Hostvolume
    # bei der Schreiblast nicht zu zusätzlichen CoW-Kopien zwingen.
    return ["-drive", f"if=none,id={name},format=raw,cache=unsafe,file={platte}",
            "-device", f"ide-hd,bus={bus},drive={name},serial=MOOP11-GESAMT-DISK0{nummer}"]


def daten(pfad):
    with image(pfad).open("rb") as datei:
        datei.seek(DATEN_START)
        roh = datei.read(DATEN_BYTES)
    if len(roh) != DATEN_BYTES:
        raise ValueError(f"Testpartition unvollständig: {pfad}")
    return roh


def host_vergleich(platten, ersatz=False):
    for platte in platten:
        with image(platte).open("rb") as datei:
            os.fsync(datei.fileno())
    erste, zweite = (daten(platten[6 if ersatz else 0]), daten(platten[1]))
    p, q = (daten(platten[2]), daten(platten[3]))
    anzahl, *erwartet = paritaets_hash([io.BytesIO(erste), io.BytesIO(zweite)])
    gemessen = tuple(hashlib.sha256(x).hexdigest() for x in (p, q))
    if anzahl != DATEN_BYTES or gemessen != tuple(erwartet):
        raise ValueError(f"P/Q-Referenz weicht ab: {gemessen} statt {erwartet}")
    if daten(platten[4]) != daten(platten[5]):
        raise ValueError("Cache-Spiegel hat verschiedene Datenblöcke")
    if ersatz and erste != daten(platten[0]):
        raise ValueError("Rebuild-Ersatz weicht von der entnommenen Datenplatte ab")
    return {"daten0_sha256": hashlib.sha256(erste).hexdigest(),
            "daten1_sha256": hashlib.sha256(zweite).hexdigest(),
            "p_sha256": gemessen[0], "q_sha256": gemessen[1],
            "cache_sha256": hashlib.sha256(daten(platten[4])).hexdigest()}


def marker_in_folge(log, *marken):
    ab = 0
    for marke in marken:
        stelle = log.find(marke, ab)
        if stelle < 0:
            raise ValueError(f"Marker fehlt oder Reihenfolge stimmt nicht: {marke!r}")
        ab = stelle + len(marke)


def phase(basis, name, boot, geraete, nummer):
    log = starten(basis / name, *boot, geraete, KERN_OK, fehler_marker=FEHLER)
    ende = f"MOO-GESAMT ENDE {nummer:016X}".encode()
    marker_in_folge(log, ESP, b"MOO-GESAMT START", ende, KERN_OK)
    return log


def lauf(args):
    basis = laufpfad(args.ausgabe)
    if "," in str(basis) or len(str(basis / "wiederanlauf-wiederholung/qmp.sock").encode()) > 107:
        raise ValueError("QEMU-Ausgabepfad ist zu lang oder enthält Kommas")
    lader, installer, normal = map(image, (args.lader, args.installer, args.normal))
    fw, vars_vorlage = map(image, (args.fw, args.vars))
    platten = [platte_anlegen(basis, i) for i in range(7)]
    stick = usb_bauen(basis, 1, lader, installer, normal)
    stick_sha = digest(stick)
    ziel = basis / "ziel.img"
    with ziel.open("xb") as datei:
        datei.truncate(768 * MIB)
    boot = (fw, vars_vorlage, min(args.frist, 240), args.accel)
    schreiben_boot = (fw, vars_vorlage, args.frist, args.accel)
    install = starten(basis / "install", *boot, gastgeraete(stick, ziel, "nvme"), INSTALL_OK)
    marker_in_folge(install, ESP, b"MOO-INSTALL UNBERECHTIGT 0000000000000001",
                    b"MOO-INSTALL SHA256 DATEI 0000000000000001",
                    b"MOO-INSTALL SHA256 DATEI 0000000000000002", INSTALL_OK)
    target_pruefen(basis, ziel, lader, normal)
    if digest(stick) != stick_sha:
        raise ValueError("Quelle änderte sich beim Installieren")
    zielgeraet = gastgeraete(None, ziel, "nvme")
    vor = phase(basis, "schreiben", schreiben_boot, zielgeraet + array_geraete(platten, False), 1)
    bytes_marker = f"MOO-GESAMT RING3 BYTES {args.bytes:016X}".encode()
    marker_in_folge(vor, b"MOO-GESAMT P/Q 0000000000000001", b"MOO-GESAMT CACHE 0000000000000002",
                    b"MOO-GESAMT MOVER-BYTES 000000000000001D", bytes_marker, RING3_SCHREIBEN, RING3_EXIT)
    print(f"Ring 3: {args.bytes} bestätigte und rückgelesene Schreibbytes über MooFS/P+Q", flush=True)
    roh_vorher = host_vergleich(platten)
    alt_sha = digest(platten[0])
    print(f"Datenplatte 0 aus der QEMU-Konfiguration entfernt, SHA-256 {alt_sha}", flush=True)
    mit_ersatz = zielgeraet + array_geraete(platten, True)
    nach = phase(basis, "rebuild", boot, mit_ersatz, 2)
    marker_in_folge(nach, b"MOO-GESAMT DATENPLATTE FEHLT 0000000000000001", RING3_LESEN,
                    RING3_EXIT, b"MOO-GESAMT REBUILD-SEITEN 0000000000000200",
                    b"MOO-GESAMT SCRUB-SEITEN 0000000000000200",
                    b"MOO-GESAMT SCRUB-REPARATUREN 0000000000000001")
    if host_vergleich(platten, ersatz=True) != roh_vorher or digest(platten[0]) != alt_sha:
        raise ValueError("Persistenter Daten-/Paritätsabgleich nach Rebuild und Scrub weicht ab")
    kalt = phase(basis, "wiederanlauf", boot, mit_ersatz, 3)
    marker_in_folge(kalt, ESP, RING3_LESEN, RING3_EXIT,
                    b"MOO-GESAMT SCRUB-SEITEN 0000000000000200",
                    b"MOO-GESAMT SCRUB-REPARATUREN 0000000000000000")
    if host_vergleich(platten, ersatz=True) != roh_vorher:
        raise ValueError("Persistenter Daten-/Paritätsabgleich nach dem Kaltstart weicht ab")
    protokoll = {
        "quelle_sha256": stick_sha, "lader_sha256": digest(lader), "kern_sha256": digest(normal),
        "ring3_schreibbytes": args.bytes, "ring3_arbeitsmenge": TRANSFER_BYTES,
        "sata_persistenz": "QMP quit und Host-fsync pro Phase",
        "entnommene_platte_sha256": alt_sha, "ersatzseiten": DATEN_BYTES // 4096,
        "scrub_reparaturen_vor_kaltstart": 1, "scrub_reparaturen_nach_kaltstart": 0,
        "partitions_hashes": roh_vorher, "qemu_bootphasen": ["install", "schreiben", "rebuild", "wiederanlauf"],
    }
    (basis / "gesamt-protokoll.json").write_text(json.dumps(protokoll, indent=2) + "\n")
    marker = "P11-GESAMT OK" if args.bytes == LAST_BYTES else "P11-GESAMT KURZ OK"
    print(f"{marker}: Installation, Ring-3-Daten, P/Q, Cache, Ersatz, Scrub und Festplattenboot", flush=True)


def main():
    parser = ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="aktion", required=True)
    profil = sub.add_parser("profil")
    profil.add_argument("art", choices=("kern", "benutzer"))
    profil.add_argument("elf")
    profil.add_argument("bytes", type=int)
    inventar = sub.add_parser("n100-inventar")
    inventar.add_argument("elf")
    pruefen = sub.add_parser("pruefen")
    for name in ("ausgabe", "lader", "installer", "normal", "fw", "vars"):
        pruefen.add_argument(name)
    pruefen.add_argument("bytes", type=int)
    pruefen.add_argument("frist", type=float)
    pruefen.add_argument("accel", nargs=REMAINDER)
    args = parser.parse_args()
    try:
        if args.aktion == "n100-inventar":
            n100_inventar(args.elf)
            return
        if not TRANSFER_BYTES <= args.bytes <= LAST_BYTES or args.bytes % TRANSFER_BYTES != 0:
            raise ValueError("Ring-3-Schreibmenge muss ein Vielfaches von 128 KiB bis 2 GiB sein")
        if args.aktion == "pruefen":
            lauf(args)
        elif args.art == "kern":
            vertrag(args.elf, b"MOGESAMT", 0, 1)
        else:
            vertrag(args.elf, b"MOGSLOAD", LAST_BYTES, args.bytes)
    except (OSError, RuntimeError, ValueError, struct.error, subprocess.SubprocessError) as fehler:
        sys.exit(f"P11-Gesamtprobe: {fehler}")


if __name__ == "__main__":
    main()
