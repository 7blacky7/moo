"""Prüft echte Partitionsabbilder, RMW und den Wiederaufbau nach SIGKILL."""

import hashlib
import os
from pathlib import Path
import signal
import stat
import struct
import subprocess
import sys
import time
from contextlib import ExitStack

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "skripte/p11/speicher"))
from geschirr import laufpfad
from host import paritaets_hash

SEITE = 4096
REGION = 256


def datenabbild(seiten, salt):
    return b"".join(bytes((salt + lba * 7 + i * 13) & 255 for i in range(SEITE)) for lba in range(seiten))


def hashdatei(pfad):
    with pfad.open("rb") as datei:
        return hashlib.file_digest(datei, "sha256").hexdigest()


def xor_referenz(daten, p):
    with ExitStack() as offen:
        dateien = [offen.enter_context(pfad.open("rb")) for pfad in daten]
        bytes_, soll, _ = paritaets_hash(dateien)
    if p.stat().st_size != bytes_ or hashdatei(p) != soll:
        raise RuntimeError(f"P weicht von der Host-Referenz ab ({p})")
    return soll


def starten(binaer, dateien, modus, abbruch=False):
    with ExitStack() as offen:
        fds = [offen.enter_context(pfad.open("r+b")).fileno() for pfad in dateien]
        if any(not stat.S_ISREG(os.fstat(fd).st_mode) for fd in fds):
            raise RuntimeError("Das Gate verwendet ausschließlich reguläre Abbilder")
        env = dict(os.environ, MOO_ARRAY_MODE=modus)
        for i, fd in enumerate(fds):
            env[f"MOO_ARRAY_FD_{i}"] = str(fd)
        if not abbruch:
            r = subprocess.run([binaer], env=env, pass_fds=fds, capture_output=True, text=True, timeout=60)
            if r.returncode != 0:
                raise RuntimeError(f"Moo-Probe {modus}: RC {r.returncode}\n{r.stderr}\n{r.stdout}")
            return int(r.stdout.strip())
        with subprocess.Popen([binaer], env=env, pass_fds=fds, stdout=subprocess.DEVNULL) as kind:
            try:
                frist = time.monotonic() + 20
                while time.monotonic() < frist:
                    pid, status = os.waitpid(kind.pid, os.WNOHANG | os.WUNTRACED)
                    if pid == 0:
                        time.sleep(0.01)
                    elif os.WIFSTOPPED(status):
                        if os.WSTOPSIG(status) != signal.SIGSTOP:
                            raise RuntimeError("Unerwartetes Stoppsignal der Array-Probe")
                        kind.kill()
                        if kind.wait(timeout=5) != -signal.SIGKILL:
                            raise RuntimeError("SIGKILL der Array-Probe ist unbelegt")
                        return
                    else:
                        kind.returncode = os.waitstatus_to_exitcode(status)
                        raise RuntimeError(f"Moo-Abbruchprobe endete vor dem Haltepunkt ({kind.returncode})")
                raise RuntimeError("Haltepunkt zwischen Daten-Write und P-Write blieb aus")
            finally:
                if kind.returncode is None:
                    kind.kill()
                    kind.wait(timeout=5)


def setze_seite(daten, lba, byte):
    pos = lba * SEITE
    daten[pos:pos + SEITE] = bytes([byte]) * SEITE


def alte_bitmapkopie_beschaedigen(meta):
    # Region 1 liegt im Shard 1, also in den Metadatenseiten 4 und 5.
    with meta.open("r+b") as datei:
        datei.seek(4 * SEITE)
        kopien = [datei.read(SEITE), datei.read(SEITE)]
        folgen = [struct.unpack_from("<Q", seite, 24)[0] for seite in kopien]
        alt = 0 if folgen[0] < folgen[1] else 1
        if folgen[0] == folgen[1] or kopien[1 - alt][32] & 1 == 0:
            raise RuntimeError("Dauerhaft markierte Abbruchregion fehlt")
        datei.seek((4 + alt) * SEITE + 4092)
        datei.write(bytes([kopien[alt][4092] ^ 1]))
        datei.flush()
        os.fsync(datei.fileno())


def gate(ort, compiler):
    ort = laufpfad(ort)
    if any(ort.iterdir()):
        raise RuntimeError(f"Für das Gate ein leeres Laufverzeichnis verwenden: {ort}")
    binaer = ort / "array_host"
    subprocess.run([compiler, "compile", "compiler/tests/p11/array_host.moos", "-o", binaer], cwd=ROOT, check=True)
    namen = ("daten-a.img", "daten-b.img", "p.img", "meta-a.img", "meta-b.img", "meta-p.img")
    pfade = [ort / name for name in namen]
    daten = [bytearray(datenabbild(260, 21)), bytearray(datenabbild(520, 157))]
    for pfad, inhalt in zip(pfade[:2], daten, strict=True):
        with pfad.open("xb") as f:
            f.write(inhalt)
    for pfad, seiten in zip(pfade[2:], (520, 2, 2, 130), strict=True):
        with pfad.open("xb") as f:
            f.truncate(seiten * SEITE)

    assert starten(binaer, pfade, "a") == 3
    assert [x.read_bytes() for x in pfade[:2]] == daten
    print(f"PASS Aufbau mit 260/520 Seiten, 512/4096-Sektoren, P SHA-256 {xor_referenz(pfade[:2], pfade[2])}")

    vor = [hashdatei(x) for x in pfade]
    assert starten(binaer, pfade, "b") == 1
    assert [hashdatei(x) for x in pfade] == vor
    print("PASS unveränderte Medien bei abweichendem letztem Seriennummernbyte")

    assert starten(binaer, pfade, "w") == 2
    setze_seite(daten[0], 3, 90)
    setze_seite(daten[1], 517, 163)
    assert [x.read_bytes() for x in pfade[:2]] == daten
    p_vorher = pfade[2].read_bytes()
    print(f"PASS zwei RMW und unabhängige Raw-Datenplatten, P SHA-256 {xor_referenz(pfade[:2], pfade[2])}")

    starten(binaer, pfade, "c", abbruch=True)
    setze_seite(daten[1], 300, 199)
    assert [x.read_bytes() for x in pfade[:2]] == daten
    assert pfade[2].read_bytes() == p_vorher
    alte_bitmapkopie_beschaedigen(pfade[5])
    print("PASS SIGKILL nach dauerhaftem Datenblock und vor P-Write, alte Bitmapkopie mit CRC-Fehler")

    assert starten(binaer, pfade, "r") == 1
    assert [x.read_bytes() for x in pfade[:2]] == daten
    p_nachher = pfade[2].read_bytes()
    assert p_vorher[:REGION * SEITE] == p_nachher[:REGION * SEITE]
    assert p_vorher[2 * REGION * SEITE:] == p_nachher[2 * REGION * SEITE:]
    assert starten(binaer, pfade, "v") == 0
    print(f"PASS nur eine Dirty-Region (256 P-Seiten) repariert, danach sauber, P SHA-256 {xor_referenz(pfade[:2], pfade[2])}")


if __name__ == "__main__":
    gate(sys.argv[1], sys.argv[2])
