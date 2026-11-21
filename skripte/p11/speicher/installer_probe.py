"""USB-Installer, getrennte Zielboots und Abbruch in einer privaten QEMU-VM."""

import hashlib
import json
import shutil
import signal
import struct
import subprocess
import sys
import time
import uuid
from argparse import REMAINDER, ArgumentParser

from geschirr import MIB, Qmp, laufpfad
from host import fat_pruefen, gpt_pruefen, image


USB_SERIE = "MOOP11INSTUSB001"
NVME_SERIE = "MOOP11-INST-NVME-001"
AHCI_SERIE = "MOOP11-INST-AHCI-001"
ESP_TYP = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b").bytes_le
ARRAY_TYP = uuid.UUID("b98a8d36-8d8c-41cf-9a31-de10dc211501").bytes_le
INSTALL_OK = b"MOO-INSTALL-PROBE-OK"
INSTALL_FEHLER = b"MOO-INSTALL-PROBE-FEHLER"
ABBRUCH = b"MOO-INSTALL ABBRUCHPUNKT 0000000000000001"
BOOT = b"MOO-KERNEL OK"


def ausfuehren(*argv):
    subprocess.run(argv, check=True, timeout=60, stdout=subprocess.DEVNULL)


def digest(pfad):
    with image(pfad).open("rb") as quelle:
        return hashlib.file_digest(quelle, "sha256").hexdigest()


def usb_bauen(lauf, modus, lader, installer, installiert):
    stick = lauf / "quelle.img"
    with stick.open("xb") as datei:
        datei.truncate(96 * MIB)
    erkennbar = b"MOINST01" + bytes(8)
    daten = installer.read_bytes()
    if daten.count(erkennbar) != 1:
        raise ValueError("Eindeutiges Installerprofil fehlt in ELF")
    kern = lauf / "Installer-KERN.ELF"
    kern.write_bytes(daten.replace(erkennbar, b"MOINST01" + modus.to_bytes(8, "little")))
    ausfuehren("parted", "-s", stick, "mklabel", "gpt", "mkpart", "ESP", "fat32",
               "1MiB", "100%", "set", "1", "esp", "on")
    with stick.open("rb") as f:
        f.seek(512 + 72)
        tab_lba, = struct.unpack("<Q", f.read(8))
        f.seek(tab_lba * 512 + 32)
        anfang, ende = struct.unpack("<QQ", f.read(16))
    volume = f"{stick}@@{anfang * 512}"
    ausfuehren("mformat", "-i", volume, "-F", "-T", str(ende - anfang + 1), "::")
    ausfuehren("mmd", "-i", volume, "::/EFI", "::/EFI/BOOT", "::/EFI/MOO", "::/MOO", "::/MOO/INSTALL")
    for quelle, ziel in ((lader, "::/EFI/BOOT/BOOTX64.EFI"), (kern, "::/EFI/MOO/KERN.ELF"),
                         (lader, "::/MOO/INSTALL/BOOTX64.EFI"), (installiert, "::/MOO/INSTALL/KERN.ELF")):
        ausfuehren("mcopy", "-i", volume, quelle, ziel)
    return stick


def gastgeraete(stick, ziel, typ):
    argv = []
    if stick is not None:
        argv.extend([
            "-device", "qemu-xhci,id=xhci,addr=0xd",
            "-device", "usb-kbd,bus=xhci.0,port=1",
            "-drive", f"if=none,id=quelle,format=raw,readonly=on,file={image(stick)}",
            "-device", f"usb-storage,drive=quelle,bus=xhci.0,port=2,bootindex=1,serial={USB_SERIE}",
        ])
    if ziel is not None:
        bootindex = 1 if stick is None else 2
        argv.extend(["-drive", f"if=none,id=zielimage,format=raw,file={image(ziel)}"])
        if typ == "ahci":
            argv.extend(["-device", "ahci,id=ziel,addr=0xc", "-device",
                         f"ide-hd,bus=ziel.0,drive=zielimage,bootindex={bootindex},serial={AHCI_SERIE}"])
        else:
            argv.extend(["-device", f"nvme,drive=zielimage,addr=0xe,bootindex={bootindex},"
                         f"serial={NVME_SERIE},msix-exclusive-bar=on,msix_qsize=2,mdts=6"])
    return argv


class FirmwareStartFehler(RuntimeError):
    pass


def starten(lauf, fw, vars_vorlage, frist, accel, geraete, marker, abbrechen=False, fehler_marker=INSTALL_FEHLER):
    try:
        return gast_starten(lauf, fw, vars_vorlage, frist, accel, geraete, marker, abbrechen, fehler_marker)
    except FirmwareStartFehler as fehler:
        print(f"{fehler}. Ein neuer OVMF-Kaltstart folgt.", flush=True)
        zweiter_lauf = lauf.with_name(lauf.name + "-wiederholung")
        return gast_starten(zweiter_lauf, fw, vars_vorlage, frist, accel, geraete, marker, abbrechen, fehler_marker)


def gast_starten(lauf, fw, vars_vorlage, frist, accel, geraete, marker, abbrechen, fehler_marker):
    lauf.mkdir()
    vars_kopie = lauf / "vars.fd"
    shutil.copyfile(vars_vorlage, vars_kopie)
    serial = lauf / "serial.log"
    sockel = lauf / "qmp.sock"
    befehl = ["qemu-system-x86_64", *accel, "-machine", "q35", "-smp", "4", "-m", "512M",
              "-net", "none", "-display", "none", "-monitor", "none", "-no-reboot",
              "-serial", f"file:{serial}", "-debugcon", f"file:{lauf / 'debug.log'}",
              "-global", "isa-debugcon.iobase=0xe9", "-qmp", f"unix:{sockel},server=on,wait=off",
              "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={fw}",
              "-drive", f"if=pflash,format=raw,unit=1,file={vars_kopie}", *geraete]
    (lauf / "qemu.json").write_text(json.dumps(befehl, indent=2) + "\n")
    with (lauf / "qemu.log").open("wb") as qemu_log, \
         subprocess.Popen(befehl, stdin=subprocess.DEVNULL, stdout=qemu_log, stderr=qemu_log) as qemu:
        try:
            beginn = time.monotonic()
            ende = beginn + frist
            firmware_ende = beginn + min(45, frist / 2)
            taste_frist = 0
            firmware_frist = None
            with Qmp(sockel) as qmp:
                while time.monotonic() < ende:
                    daten = serial.read_bytes() if serial.exists() else b""
                    if (marker != BOOT or fehler_marker != INSTALL_FEHLER) and fehler_marker in daten:
                        zeilen = b"\n".join(s for s in daten.splitlines() if fehler_marker.split()[0] in s)
                        raise RuntimeError(f"Gastfehler {serial}\n{zeilen.decode(errors='replace')[-4000:]}")
                    if marker in daten:
                        if abbrechen:
                            qemu.kill()
                            if qemu.wait(timeout=10) != -signal.SIGKILL:
                                raise RuntimeError("Eigene QEMU-VM meldet keinen SIGKILL-Exit")
                            print(f"SIGKILL der eigenen QEMU-VM {qemu.pid} bei abgeglichenem Kern", flush=True)
                        else:
                            try:
                                qmp.ausfuehren("quit")
                            except RuntimeError:
                                if qemu.wait(timeout=10) != 0:
                                    raise
                            if qemu.wait(timeout=10) != 0:
                                raise RuntimeError("QEMU endete fehlerhaft")
                        return daten
                    if qemu.poll() is not None:
                        raise RuntimeError(f"QEMU endete vor dem erwarteten Marker {marker!r}")
                    jetzt = time.monotonic()
                    # Nur vor dem Loader-Einstieg kommt eine Wiederholung in Frage.
                    if (b"MOO-LADER START" not in daten and b"BdsDxe: failed to load" in daten
                            and b": Not Found" in daten):
                        if firmware_frist is None:
                            firmware_frist = jetzt + 12
                        if jetzt >= firmware_frist:
                            raise FirmwareStartFehler(f"OVMF fand das Bootgerät nicht ({serial})")
                    if jetzt >= firmware_ende and b"MOO-LADER START" not in daten:
                        debug = lauf / "debug.log"
                        if not debug.exists() or b"MOO-LADER START" not in debug.read_bytes():
                            raise FirmwareStartFehler(f"OVMF erreichte den Loader nicht ({serial})")
                    if b"TASTATUR BEREIT" in daten and jetzt >= taste_frist:
                        qmp.ausfuehren("human-monitor-command", **{"command-line": "sendkey a 50"})
                        taste_frist = jetzt + 0.75
                    time.sleep(0.04)
            raise RuntimeError(f"Marker {marker!r} blieb aus in {serial}")
        finally:
            if qemu.poll() is None:
                qemu.kill()
                qemu.wait(timeout=10)


def target_pruefen(lauf, ziel, lader, normal):
    gpt_pruefen(ziel)
    with image(ziel).open("rb") as f:
        f.seek(2 * 512)
        erster, zweiter = f.read(128), f.read(128)
        if (erster[:16] != ESP_TYP or zweiter[:16] != ARRAY_TYP
                or struct.unpack_from("<QQ", erster, 32) != (2048, 1050623)
                or struct.unpack_from("<QQ", zweiter, 32) != (1050624, 1083391)):
            raise ValueError("ESP oder Array-Metadatenpartition weichen ab")
    part = lauf / "esp-pruefung.img"
    ausfuehren("dd", f"if={ziel}", f"of={part}", "bs=1M", "skip=1", "count=512", "conv=sparse", "status=none")
    fat_pruefen(part)
    for name, erf in (("BOOT/BOOTX64.EFI", lader), ("MOO/KERN.ELF", normal)):
        kopie = lauf / (name.rsplit("/", 1)[1] + ".ausgelesen")
        ausfuehren("mcopy", "-i", part, f"::/EFI/{name}", kopie)
        sha = digest(kopie)
        if sha != digest(erf):
            raise ValueError(f"SHA-256 weicht ab für EFI/{name}")
        print(f"EFI/{name} SHA-256 {sha}", flush=True)


def lauf(args):
    basis = laufpfad(args.ausgabe)
    lader = image(args.lader)
    installer = image(args.installer)
    normal = image(args.normal)
    fw, vars_vorlage = image(args.fw), image(args.vars)
    bootparameter = (fw, vars_vorlage, args.frist, args.accel)
    for modus, name in ((1, "nvme"), (2, "ahci"), (3, "abbruch")):
        ort = basis / name
        ort.mkdir()
        stick = usb_bauen(ort, modus, lader, installer, normal)
        vor_sha = digest(stick)
        ziel = ort / "ziel.img"
        with ziel.open("xb") as datei:
            datei.truncate(768 * MIB)
        stoppen = modus == 3
        daten = starten(ort / "install", *bootparameter, gastgeraete(stick, ziel, name),
                        ABBRUCH if stoppen else INSTALL_OK, abbrechen=stoppen)
        if b"MOO-INSTALL UNBERECHTIGT 0000000000000001" not in daten or b"MOO-INSTALL SHA256 DATEI 0000000000000001" not in daten:
            raise ValueError("Rechte- und Kern-SHA-Beleg fehlt")
        if digest(stick) != vor_sha:
            raise ValueError("Installationsstick wurde verändert")
        if stoppen:
            if b"MOO-INSTALL SHA256 DATEI 0000000000000002" in daten:
                raise ValueError("Abbruch muss vor Veröffentlichung des UEFI-Loaders liegen")
            bdaten = starten(ort / "usb-neustart", *bootparameter, gastgeraete(stick, None, name), BOOT)
            if b"MOO-LADER ESP-KERN EIGENES GERAET" not in bdaten or digest(stick) != vor_sha:
                raise ValueError("Unveränderter ESP-Start des Sticks nach Abbruch unbelegt")
            print(f"Abbruch: Quelle bleibt bootfähig und SHA-256-identisch {vor_sha}", flush=True)
        else:
            if b"MOO-INSTALL SHA256 DATEI 0000000000000002" not in daten:
                raise ValueError("Loader-SHA-Beleg fehlt")
            target_pruefen(ort, ziel, lader, normal)
            bdaten = starten(ort / "ziel-neustart", *bootparameter, gastgeraete(None, ziel, name), BOOT)
            if b"MOO-LADER ESP-KERN EIGENES GERAET" not in bdaten:
                raise ValueError("Eigenständiger ESP-Start des installierten Datenträgers unbelegt")
            print(f"{name.upper()}: MoOS startet von der installierten ESP ohne USB; Quelle SHA-256 {vor_sha}", flush=True)


def main():
    parser = ArgumentParser(description=__doc__)
    for name in ("ausgabe", "lader", "installer", "normal", "fw", "vars"):
        parser.add_argument(name)
    parser.add_argument("frist", type=float)
    parser.add_argument("accel", nargs=REMAINDER)
    args = parser.parse_args()
    try:
        lauf(args)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as fehler:
        sys.exit(f"Installer-QEMU: {fehler}")


if __name__ == "__main__":
    main()
