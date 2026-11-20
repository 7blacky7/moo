"""Eigene QEMU-Images, QMP und Schreib-/Fehlerprobe für P11."""

import errno
import hashlib
import json
import os
import signal
import socket
import subprocess
import sys
import time
from argparse import ArgumentParser
from pathlib import Path
from tempfile import mkdtemp


WURZEL = Path(__file__).resolve().parents[3]
BAU = WURZEL / "build"
MIB = 1024 * 1024
SEKTOR = 512


def laufpfad(pfad):
    lauf = Path(pfad).resolve()
    if BAU not in lauf.parents:
        raise ValueError(f"Testlaufverzeichnis muss unter {BAU} liegen: {lauf}")
    return lauf


def befehlszeile(platte, fehler_sektor=None):
    debug = {"driver": "blkdebug", "image": {"driver": "file", "filename": platte["image"]}}
    if platte.get("nvme_probe"):
        debug["node-name"] = platte["knoten"] + "_debug"
        debug["discard"] = "unmap"
        debug["image"]["discard"] = "unmap"
    if fehler_sektor is not None:
        debug["inject-error"] = [
            {"event": "write_aio", "errno": errno.EIO, "sector": fehler_sektor,
             "once": True, "immediately": True}
        ]
    # raw meldet write_aio an sein Kind. Dort beobachtet blkdebug das Ereignis.
    roh = {"driver": "raw", "node-name": platte["knoten"], "file": debug}
    if platte.get("nvme_probe"):
        roh["discard"] = "unmap"
    return roh


def erzeugen(lauf, nvme, ahci, usb, groesse, fehler, nvme_probe=False, ahci_probe=False):
    if ahci_probe and (nvme, ahci, usb, groesse) != (0, 4, 0, 64 * MIB):
        raise ValueError("AHCI-Kerngate erwartet genau vier 64-MiB-SATA-Images")
    if nvme_probe and (nvme, ahci, usb, groesse) != (2, 0, 0, 64 * MIB):
        raise ValueError("NVMe-Kerngate erwartet genau zwei 64-MiB-NVMe-Images")
    namen = [(art, nr) for art, anzahl in (("nvme", nvme), ("ahci", ahci), ("usb", usb))
             for nr in range(anzahl)]
    if min(nvme, ahci, usb) < 0 or not namen or groesse < MIB:
        raise ValueError("Mindestens ein Testlaufwerk und eine positive Imagegröße sind erforderlich")
    fehler_name, fehler_sektor = fehler
    if fehler_name and (fehler_name not in {f"{art}{nr}" for art, nr in namen}
                        or not 0 <= fehler_sektor < groesse // SEKTOR):
        raise ValueError("EIO-Ziel oder Sektor liegt außerhalb der Testimages")

    sockel = lauf / "qmp.sock"
    if "," in str(sockel) or len(os.fsencode(sockel)) > 107:
        raise ValueError("Pfad für den QMP-Unix-Socket ist zu lang oder enthält ein Komma")
    lauf.mkdir(parents=True)
    argumente = ["-qmp", f"unix:{sockel},server=on,wait=off"]
    if ahci_probe:
        argumente.extend([
            "-machine", "q35",
            "-global", "ICH9-LPC.acpi-pci-hotplug-with-bridge-support=off",
            "-device", "pcie-root-port,id=t8slot,port=1,chassis=41,slot=1,addr=0xc",
        ])
    platten = []
    for art, nr in namen:
        name = f"{art}{nr}"
        geraet = f"p11_{name}"
        knoten = f"{geraet}_disk"
        image = lauf / f"{name}.img"
        with image.open("xb") as datei:
            datei.truncate(groesse)
        platte = {"name": name, "art": art, "geraet": geraet, "knoten": knoten, "image": str(image)}
        if nvme_probe:
            platte["nvme_probe"] = True
            platte["block_bytes"] = (512, 4096)[nr]
        platten.append(platte)
        optionen = befehlszeile(platte, fehler_sektor if name == fehler_name else None)
        argumente.extend(["-blockdev", json.dumps(optionen)])
        if art == "nvme":
            ctrl = f"nvme,id={geraet},serial=P11-NVME-{nr:02}"
            namespace = f"nvme-ns,id={geraet}_ns,bus={geraet},drive={knoten},nsid=1"
            if nvme_probe:
                serie = ("MOOP11-NVME-512-0001", "MOOP11-NVME-4KN-0002")[nr]
                block_bytes = platte["block_bytes"]
                ctrl = (f"nvme,id={geraet},serial={serie},addr={0xE + nr:#x},"
                        "msix-exclusive-bar=on,msix_qsize=2,mdts=6")
                namespace += f",logical_block_size={block_bytes},physical_block_size={block_bytes}"
            argumente.extend(["-device", ctrl, "-device", namespace])
        elif art == "ahci":
            sata, port = divmod(nr, 3 if ahci_probe else 6)
            controller = f"p11_sata{sata}"
            if port == 0:
                controller_optionen = f"ahci,id={controller}"
                if ahci_probe:
                    controller_optionen += ",addr=0xd" if sata == 0 else ",bus=t8slot,addr=0"
                argumente.extend(["-device", controller_optionen])
            optionen = f"ide-hd,bus={controller}.{port},id={geraet},drive={knoten}"
            if ahci_probe:
                optionen += f",serial=MOOP11-AHCI-ATA-{nr + 1:04},logical_block_size=512,physical_block_size=512"
            argumente.extend(["-device", optionen])
        else:
            xhci, port = divmod(nr, 4)
            controller = f"p11_xhci{xhci}"
            if port == 0:
                argumente.extend(["-device", f"qemu-xhci,id={controller}"])
            argumente.extend([
                "-device", f"usb-storage,bus={controller}.0,id={geraet},drive={knoten},serial=P11-USB-{nr:02}",
            ])

    (lauf / "platten.json").write_text(json.dumps(platten, indent=2) + "\n")
    (lauf / "qemu-argumente.nul").write_bytes(b"\0".join(map(os.fsencode, argumente)) + b"\0")
    print(f"P11-Lauf {lauf}: {nvme} NVMe, {ahci} AHCI, {usb} USB")
    return platten, argumente


class Qmp:
    def __init__(self, sockel):
        self.sockel = sockel
        self.ereignisse = []

    def __enter__(self):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            self.sock.settimeout(10)
            frist = time.monotonic() + 10
            while True:
                try:
                    self.sock.connect(str(self.sockel))
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    if time.monotonic() >= frist:
                        raise
                    time.sleep(0.05)
            self.stream = self.sock.makefile("rwb")
            if "QMP" not in self.lesen():
                raise RuntimeError("QMP-Begrüßung fehlt")
            self.ausfuehren("qmp_capabilities")
        except Exception:
            if hasattr(self, "stream"):
                self.stream.close()
            self.sock.close()
            raise
        return self

    def __exit__(self, *_):
        self.stream.close()
        self.sock.close()

    def lesen(self):
        zeile = self.stream.readline()
        if not zeile:
            raise RuntimeError("QMP hat die Verbindung geschlossen")
        return json.loads(zeile)

    def ausfuehren(self, befehl, **argumente):
        nachricht = {"execute": befehl}
        if argumente:
            nachricht["arguments"] = argumente
        self.stream.write(json.dumps(nachricht).encode("utf-8") + b"\n")
        self.stream.flush()
        while True:
            antwort = self.lesen()
            if "error" in antwort:
                raise RuntimeError(f"QMP {befehl}: {antwort['error']['desc']}")
            if "return" in antwort:
                return antwort["return"]
            self.ereignisse.append(antwort)

    def entfernen(self, geraet):
        self.ausfuehren("device_del", id=geraet)
        frist = time.monotonic() + 10
        while True:
            for ereignis in self.ereignisse:
                if ereignis.get("event") == "DEVICE_DELETED" and ereignis["data"].get("device") == geraet:
                    print(f"QMP DEVICE_DELETED {geraet}")
                    return
            rest = frist - time.monotonic()
            if rest <= 0:
                raise TimeoutError(f"QMP DEVICE_DELETED für {geraet} fehlt")
            self.sock.settimeout(rest)
            self.ereignisse.append(self.lesen())


def datei_hash(pfad):
    with open(pfad, "rb") as datei:
        return hashlib.file_digest(datei, "sha256").hexdigest()


def io_ausfuehren(lauf, optionen, *befehle):
    qio = ["qemu-io"]
    for befehl in befehle:
        qio.extend(("-c", befehl))
    qio.append("json:" + json.dumps(optionen))
    ergebnis = subprocess.run(qio, capture_output=True, text=True, timeout=15,
                              env={**os.environ, "LC_ALL": "C"})
    with (lauf / "qemu-io.log").open("a") as log:
        log.write(f"{optionen['node-name']} {', '.join(befehle)} RC={ergebnis.returncode}\n")
        log.write(ergebnis.stdout + ergebnis.stderr)
    return ergebnis


def pruefen(lauf):
    groesse = 8 * MIB
    platten, argumente = erzeugen(lauf, 2, 1, 1, groesse, ("nvme0", 8))
    platte = platten[0]
    vorher = datei_hash(platte["image"])
    eio = io_ausfuehren(lauf, befehlszeile(platte, 8), "write -P 0x88 4096 512")
    if eio.returncode != 1 or "Input/output error" not in eio.stdout + eio.stderr:
        raise RuntimeError("blkdebug-EIO bei Sektor 8 fehlt, siehe qemu-io.log")
    if datei_hash(platte["image"]) != vorher:
        raise RuntimeError("Fehlgeschlagener Schreibzugriff hat das Abbild verändert")
    print("blkdebug EIO Sektor 8, SHA-256 unverändert")

    soll = {}
    for nr, platte in enumerate(platten):
        muster = 0x50 + nr
        eine_seite = bytes([muster]) * SEKTOR
        erwartung = bytearray(groesse)
        erwartung[4096:4608] = eine_seite
        befehle = (f"write -P {muster} 4096 512", "flush")
        if nr == 0:
            erwartung[8192:8704] = eine_seite
            befehle = (
                *befehle,
                "break write_aio p11_warte", f"aio_write -P {muster} 8192 512",
                "wait_break p11_warte", "sleep 150", "resume p11_warte", "aio_flush", "flush",
            )
        start = time.monotonic()
        ergebnis = io_ausfuehren(lauf, befehlszeile(platte), *befehle)
        if ergebnis.returncode != 0:
            raise RuntimeError(f"QEMU-Schreibzugriff auf {platte['name']} fehlgeschlagen, siehe qemu-io.log")
        if nr == 0:
            ausgabe = ergebnis.stdout + ergebnis.stderr
            if not ("Suspended request" in ausgabe and "Resuming request" in ausgabe
                    and time.monotonic() - start >= 0.15):
                raise RuntimeError("Die erwartete blkdebug-Verzögerung fehlt")
            print("blkdebug Schreibanforderung gehalten und nach 150 ms freigegeben")
        soll[platte["name"]] = hashlib.sha256(erwartung).hexdigest()
        if datei_hash(platte["image"]) != soll[platte["name"]]:
            raise RuntimeError(f"Abweichender Schreibfingerabdruck auf {platte['name']}")

    cmd = ["qemu-system-x86_64", "-machine", "q35", "-nodefaults", "-S", "-display", "none", *argumente]
    with (lauf / "qemu.log").open("wb") as log, subprocess.Popen(cmd, stdout=log, stderr=log) as qemu:
        try:
            with Qmp(lauf / "qmp.sock") as qmp:
                blocke = qmp.ausfuehren("query-block")
                schreibbar = {block["inserted"]["node-name"] for block in blocke
                              if block.get("inserted") and not block["inserted"]["ro"]}
                erwartet = {platte["knoten"] for platte in platten}
                if not erwartet <= schreibbar:
                    raise RuntimeError(f"Schreibbare QEMU-Geräte fehlen: {erwartet - schreibbar}")
                print("QMP: alle vier Testgeräte schreibbar")
                qmp.entfernen("p11_usb0")
        finally:
            if qemu.poll() is None:
                qemu.kill()
            rc = qemu.wait(timeout=10)
        if rc != -signal.SIGKILL:
            raise RuntimeError(f"QEMU-Abbruch hatte Rückgabestatus {rc}, SIGKILL erwartet")

    for platte in platten:
        name = platte["name"]
        ist = datei_hash(platte["image"])
        if ist != soll[name]:
            raise RuntimeError(f"Abweichender Fingerabdruck nach SIGKILL auf {name}")
        print(f"SHA-256 nach SIGKILL {name} {ist}")


def eio_ziel(text):
    name, trennzeichen, sektor = text.partition(":")
    if trennzeichen != ":" or not name or not sektor.isdecimal():
        raise ValueError("EIO-Ziel im Format PLATTE:SEKTOR angeben, etwa nvme0:8")
    return name, int(sektor)


def main():
    parser = ArgumentParser(description=__doc__)
    aktionen = parser.add_subparsers(dest="aktion", required=True)
    bau = aktionen.add_parser("aufbauen")
    bau.add_argument("lauf")
    bau.add_argument("--nvme", type=int, default=2)
    bau.add_argument("--ahci", type=int, default=1)
    bau.add_argument("--usb", type=int, default=1)
    bau.add_argument("--mib", type=int, default=64)
    bau.add_argument("--eio", help="Einmaliges EIO für write_aio, Format PLATTE:SEKTOR")
    bau.add_argument("--nvme-probe", action="store_true", help="Feste NVMe-Ziele für das Kerngate")
    bau.add_argument("--ahci-probe", action="store_true", help="Vier SATA-Platten und PCIe-Hotplug für das Kerngate")
    probe = aktionen.add_parser("pruefen")
    probe.add_argument("--lauf")
    entfernen = aktionen.add_parser("entfernen")
    entfernen.add_argument("lauf")
    entfernen.add_argument("platte")
    args = parser.parse_args()
    try:
        if args.aktion == "aufbauen":
            fehler = eio_ziel(args.eio) if args.eio else ("", 0)
            erzeugen(laufpfad(args.lauf), args.nvme, args.ahci, args.usb, args.mib * MIB, fehler, args.nvme_probe, args.ahci_probe)
        elif args.aktion == "entfernen":
            lauf = laufpfad(args.lauf)
            platten = json.loads((lauf / "platten.json").read_text())
            geraet = next((p["geraet"] for p in platten if p["name"] == args.platte), None)
            if geraet is None:
                raise ValueError(f"Unbekannte Testplatte: {args.platte}")
            with Qmp(lauf / "qmp.sock") as qmp:
                qmp.entfernen(geraet)
        else:
            if args.lauf:
                lauf = laufpfad(args.lauf)
            else:
                basis = laufpfad(BAU / "p11" / "speicher")
                basis.mkdir(parents=True, exist_ok=True)
                lauf = Path(mkdtemp(prefix="schreiben-", dir=basis)) / "platten"
            pruefen(lauf)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as fehler:
        sys.exit(f"P11-Speicherprobe: {fehler}")


if __name__ == "__main__":
    main()
