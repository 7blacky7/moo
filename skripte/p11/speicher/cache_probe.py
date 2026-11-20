"""QEMU nach der Blockcache-Barriere mit SIGKILL beenden und Images prüfen."""

import hashlib
import re
import signal
import struct
import subprocess
import sys
import time
from argparse import REMAINDER, ArgumentParser

from geschirr import laufpfad


BARRIERE = b"MOO-CACHE-BARRIERE-OK"


def booten(lauf, frist, befehl):
    if not befehl or befehl[0] != "qemu-system-x86_64":
        raise ValueError("Erwartet den QEMU-Befehl für das private Testprofil")
    serial = lauf / "serial.log"
    with (lauf / "monitor.log").open("wb") as monitor:
        with subprocess.Popen(befehl, stdin=subprocess.DEVNULL, stdout=monitor, stderr=monitor) as qemu:
            try:
                ende = time.monotonic() + frist
                while time.monotonic() < ende:
                    daten = serial.read_bytes() if serial.exists() else b""
                    if b"MOO-CACHE-PROBE-FEHLER" in daten:
                        raise RuntimeError("Gastprobe gescheitert")
                    if BARRIERE in daten and qemu.poll() is None:
                        qemu.kill()
                        if qemu.wait(timeout=10) != -signal.SIGKILL:
                            raise RuntimeError("QEMU meldet keinen SIGKILL-Exit")
                        print(f"Eigene QEMU-VM PID {qemu.pid} nach Barriere per SIGKILL beendet", flush=True)
                        return
                    if qemu.poll() is not None:
                        raise RuntimeError(f"QEMU vor Barriere beendet, Exit {qemu.returncode}")
                    time.sleep(0.01)
                raise RuntimeError("Barriere-Marker blieb aus")
            finally:
                if qemu.poll() is None:
                    qemu.kill()
                    qemu.wait(timeout=10)


def muster(laenge, saat):
    return b"".join(struct.pack("<Q", (saat << 56) | i) for i in range(laenge // 8))


def pruefen(lauf):
    daten = (lauf / "serial.log").read_bytes().replace(b"\r", b"")
    for marker in (b"MOO-CACHE PFA SEITEN 0000000000000006",
                   b"MOO-CACHE HASH LRU ZEILEN 0000000000000004",
                   b"MOO-CACHE ABSCHNITT 0000000000000001", BARRIERE):
        if daten.count(marker) != 1:
            raise RuntimeError(f"Cache-Marker fehlt oder mehrdeutig: {marker.decode()}")
    eio = re.findall(rb"MOO-CACHE EIO (?:RUECKSCHREIBER|AUFRUFER) ([0-9A-F]{16})", daten)
    if len(eio) != 2 or eio[0] != eio[1] or int(eio[0], 16) == 0:
        raise RuntimeError("Rückschreibfehler erreicht den Barriere-Aufrufer nicht")
    fua_start = daten.index(b"MOO-CACHE FUA START ")
    fua_ok = daten.index(b"MOO-CACHE FUA OK ", fua_start)
    zwischen = daten[fua_start:fua_ok]
    if b"MOO-KERNEL NVME QUEUE 0000000000000001\nMOO-KERNEL NVME OPCODE 0000000000000000\n" not in zwischen:
        raise RuntimeError("Geräte-Flush vor der FUA-Rückkehr fehlt")
    trace = (lauf / "cache.trace").read_bytes()
    flush = re.compile(rb"pci_nvme_io_cmd cid (\d+) nsid 0x1 sqid 1 opc 0x0 ")
    abschluss = re.compile(rb"pci_nvme_enqueue_req_completion cid (\d+) cqid 1 .* status 0x([0-9a-f]+)")
    offen = set()
    abgeschlossen = 0
    for zeile in trace.splitlines():
        eingereicht = flush.search(zeile)
        if eingereicht:
            offen.add(eingereicht[1])
        fertig = abschluss.search(zeile)
        if fertig and fertig[1] in offen:
            offen.remove(fertig[1])
            if int(fertig[2], 16) != 0:
                raise RuntimeError("QEMU meldet einen Geräte-Flush-Fehler")
            abgeschlossen += 1
    if offen or abgeschlossen != 5 or trace.count(b"pci_nvme_flush_ns ") != 5:
        raise RuntimeError(f"QEMU-Geräte-Flush-Abschlüsse fehlen: {abgeschlossen}")
    print(f"NVMe-Flush-Abschlüsse im Trace: {abgeschlossen}")

    bereiche = (
        ((8 * 512, 512, 0x51), (32 * 512, 1024, 0x52)),
        ((64 * 4096, 24576, 0x61), (80 * 4096, 4096, 0x62)),
    )
    for nr, lagen in enumerate(bereiche):
        datei = lauf / "cache" / f"nvme{nr}.img"
        erwartet = bytearray(64 * 1024 * 1024)
        for offset, laenge, saat in lagen:
            erwartet[offset:offset + laenge] = muster(laenge, saat)
        with datei.open("rb") as bild:
            ist = hashlib.file_digest(bild, "sha256").hexdigest()
        soll = hashlib.sha256(erwartet).hexdigest()
        if ist != soll:
            raise RuntimeError(f"{datei.name} SHA-256 {ist}, erwartet {soll}")
        print(f"{datei.name} nach SIGKILL SHA-256 {ist}")


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("lauf")
    parser.add_argument("frist", type=float)
    parser.add_argument("befehl", nargs=REMAINDER)
    args = parser.parse_args()
    try:
        lauf = laufpfad(args.lauf)
        booten(lauf, args.frist, args.befehl)
        pruefen(lauf)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as fehler:
        sys.exit(f"Blockcache-Probe: {fehler}")


if __name__ == "__main__":
    main()
