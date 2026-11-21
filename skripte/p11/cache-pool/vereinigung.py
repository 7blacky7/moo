"""MooFS-Mover über zwei dauerhafte Haltepunkte und einen Cache-Ausfall."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "array"))
from gate import ROOT, SEITE, laufpfad, starten


def pruefe(binaer, pfade, mode, soll):
    ist = starten(binaer, pfade, mode)
    if ist != soll:
        raise RuntimeError(
            f"Mover {mode}: VfsFehler {ist >> 48}/{(ist >> 32) & 0xffff}/{ist & 0xffffffff}, erwartet {soll}"
        )


def gate(ort, compiler):
    ort = laufpfad(ort)
    if any(ort.iterdir()):
        raise RuntimeError(f"Für das Gate ein leeres Laufverzeichnis verwenden: {ort}")
    binaer = ort / "vereinigung_host"
    subprocess.run([compiler, "compile", "compiler/tests/p11/vereinigung_host.moos", "-o", binaer], cwd=ROOT, check=True)
    pfade = [ort / (name + ".img") for name in ("daten-a", "daten-b", "cache")]
    for p in pfade:
        with p.open("xb") as datei:
            datei.truncate(256 * SEITE)
    pruefe(binaer, pfade, "a", 2)
    print("PASS offene MooFS-Datei bleibt im Cache; 8193 Byte über /freigabe/beleg gelesen")
    starten(binaer, pfade, "b", abbruch=True)
    pruefe(binaer, pfade, "p", 1)
    print("PASS SIGKILL vor Rename; Original sichtbar und vollständige Staging-Datei verborgen")
    starten(binaer, pfade, "c", abbruch=True)
    pruefe(binaer, pfade, "d", 1)
    print("PASS SIGKILL nach Rename+Flush; ein Verzeichniseintrag, Datei bei Cache-Ausfall bytegleich und Schatten beim Wiederanlauf entfernt")
    pruefe(binaer, pfade, "e", 1)
    print("PASS Namenshash verteilt neue Dateien direkt auf beide Datenvolumes")


if __name__ == "__main__":
    gate(Path(sys.argv[1]), sys.argv[2])
