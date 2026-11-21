"""Bytevergleich und Resync eines Zweier-Spiegels mit regulären Abbildern."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "array"))
from gate import ROOT, SEITE, datenabbild, hashdatei, laufpfad, setze_seite, starten


def pruefe(binaer, pfade, modus, soll):
    wert = starten(binaer, pfade, modus)
    if wert != soll:
        raise RuntimeError(f"Spiegel-Probe {modus}: {wert}, BlockFehler {wert >> 32}/{wert & 0xffffffff}, erwartet {soll}")


def gate(ort, compiler):
    ort = laufpfad(ort)
    if any(ort.iterdir()):
        raise RuntimeError(f"Für das Gate ein leeres Laufverzeichnis verwenden: {ort}")
    binaer = ort / "spiegel_host"
    subprocess.run([compiler, "compile", "compiler/tests/p11/spiegel_host.moos", "-o", binaer], cwd=ROOT, check=True)
    pfade = [ort / (name + ".img") for name in ("daten-a", "daten-b", "meta-a", "meta-b")]
    soll = bytearray(datenabbild(520, 37))
    with pfade[0].open("xb") as datei:
        datei.write(soll)
    for p, seiten in zip(pfade[1:], (520, 10, 138), strict=True):
        with p.open("xb") as datei:
            datei.truncate(seiten * SEITE)

    pruefe(binaer, pfade, "a", 3)
    assert pfade[0].read_bytes() == pfade[1].read_bytes() == soll
    print("PASS Aufbau, 520 Seiten und Sektoren 512/4096 bytegleich")

    pruefe(binaer, pfade, "w", 1)
    setze_seite(soll, 4, 0x44)
    assert pfade[1].read_bytes() == soll
    setze_seite(soll, 300, 0xA5)
    assert pfade[0].read_bytes() == soll
    vor = [hashdatei(x) for x in pfade]
    pruefe(binaer, pfade, "x", 1)
    assert vor == [hashdatei(x) for x in pfade]
    print("PASS Schreibbetrieb mit einem Träger und Ablehnung des veralteten einzelnen Trägers")

    pruefe(binaer, pfade, "b", 257)
    setze_seite(soll, 0, 0xF0)
    setze_seite(soll, 303, 0xF3)
    assert pfade[0].read_bytes() == soll
    assert pfade[1].read_bytes()[:SEITE] == soll[:SEITE]
    assert pfade[1].read_bytes()[303 * SEITE:304 * SEITE] != soll[303 * SEITE:304 * SEITE]
    pruefe(binaer, pfade, "r", 255)
    assert pfade[0].read_bytes() == pfade[1].read_bytes() == soll
    print(f"PASS 256 kopierte Dirty-Seiten, Neustart bei 257, Online-Schreiben, beide SHA-256 {hashdatei(pfade[0])}")


if __name__ == "__main__":
    gate(Path(sys.argv[1]), sys.argv[2])
