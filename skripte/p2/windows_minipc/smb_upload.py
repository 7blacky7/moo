#!/usr/bin/env python3
"""Moo-Testdateien und den Projekt-Testspiegel ueber Windows-SMB uebertragen.

Einzeldatei:  smb_upload.py <lokale Testdatei>          -> D:\\MooTest\\<name>
Testspiegel:  smb_upload.py --spiegeln [--erzwingen]    -> D:\\MooTest\\repo
              smb_upload.py --spiegeln --nur-liste      (nur Manifest, kein Transfer)

Der Spiegel ist EINWEG (Userentscheid 08.11.2026, Thoughts 39bbf950/87d5d702):
Linux/Synapse ist die einzige Quelle, Windows nur Testziel. Es wird nie etwas
von Windows zurueckkopiert. Dateien, die auf Windows seit dem letzten Spiegel
veraendert wurden, fuehren zum Abbruch (Befund), ausser mit --erzwingen.
"""

import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time

# Import immer aus dem Projektordner, egal aus welchem CWD gestartet.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from minipc_winrm import CONFIG, REMOTE_DIR, sitzung_oeffnen  # noqa: E402

WINDOWS_IP = os.environ.get("MOO_MINIPC_HOST", "192.168.77.2")
PROJEKT = Path(__file__).resolve().parents[3]
SPIEGEL_ZIEL = REMOTE_DIR + "\\repo"
SPIEGEL_TAR = "moo_spiegel.tar"
SPIEGEL_SKRIPT = Path(__file__).with_name("spiegel_anwenden.ps1")
MAX_BYTES = 20 * 1024 * 1024

# Verzeichnisnamen, die nirgends gespiegelt werden (VCS, Caches, Build-Ausgaben).
AUSSCHLUSS_ORDNER = {".git", "target", "build", "node_modules", "__pycache__", ".venv",
                     ".pytest_cache", ".mypy_cache", ".ruff_cache", ".moo-spiegel"}
# Projektrelative Praefixe: lokale Lauf-/Arbeitsartefakte, Testausgaben,
# Synapse-Agentendaten, Forschungs-/KI-Daten (kein Testquellstand).
AUSSCHLUSS_PRAEFIXE = ("scratch/", "tmp/", "tests/local/", ".synapse/", "daten/",
                       "docs/research/", "beispiele/test_screenshots/", "beispiele/snapshots/")
# Auf der Projektwurzel nur Konfiguration/Text, keine losen Build-Ausgaben.
WURZEL_ENDUNGEN = {".toml", ".lock", ".md", ".txt", ".json", ".yml", ".yaml", ".ps1", ".cmd", ".sh"}
# Secrets und lokale Zugangsdaten.
GEHEIM = re.compile(r"(^|/)(\.env(\..*)?|.*\.(pem|key|pfx|p12|kdbx)|id_(rsa|ed25519).*"
                    r"|winrm\.json|.*secret.*|.*credential.*)$", re.IGNORECASE)
# Unter Windows unzulaessige Namen.
WINDOWS_UNGUELTIG = re.compile(r'[<>:"|?*\x00-\x1f]|[. ]$')


def _smb_put(datei: Path, zielname: str) -> bool:
    """Kopiert eine lokale Datei per SMB nach D:\\MooTest\\<zielname>."""
    daten = json.loads(CONFIG.read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory(prefix="moo-win-smb-") as temp:
        geheim = Path(temp) / "auth"
        fd = os.open(geheim, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(f"username = {daten['user']}\n")
            f.write(f"password = {daten['password']}\n")
            f.write("domain = DESKTOP-LOJN555\n")
        befehl = [
            "smbclient", f"//{WINDOWS_IP}/MooTest",
            "-A", str(geheim), "-m", "SMB3",
            "-c", f'put "{datei}" "{zielname}"',
        ]
        ergebnis = subprocess.run(befehl, capture_output=True, text=True, check=False)
        if ergebnis.returncode != 0:
            print(f"SMB-Kopie fehlgeschlagen: {ergebnis.stderr.strip()}", file=sys.stderr)
            return False
    return True


def _voraussetzungen() -> bool:
    if not CONFIG.is_file() or shutil.which("smbclient") is None:
        print("Lokale WinRM-Anmeldung oder smbclient fehlt", file=sys.stderr)
        return False
    return True


def _sha256(datei: Path) -> str:
    h = hashlib.sha256()
    with datei.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def datei_kopieren(quelltext: str) -> int:
    """UEbertraegt eine Build-Datei mit lokalen Zugangsdaten ohne Shell-Leaks."""
    datei = Path(quelltext).resolve()
    if not datei.is_file() or not re.fullmatch(r"[A-Za-z0-9_.-]+", datei.name):
        print("Quelldatei nicht vorhanden oder Dateiname ungueltig", file=sys.stderr)
        return 2
    if not _voraussetzungen():
        return 2
    if not _smb_put(datei, datei.name):
        return 1

    ziel = REMOTE_DIR + "\\" + datei.name
    try:
        antwort = sitzung_oeffnen().run_ps(
            f"(Get-FileHash -Algorithm SHA256 -LiteralPath '{ziel}').Hash"
        )
        ferne_pruefsumme = antwort.std_out.decode("utf-8", "replace").strip().lower()
        lokale_pruefsumme = _sha256(datei)
        if antwort.status_code != 0 or ferne_pruefsumme != lokale_pruefsumme:
            print("Windows-SHA256 weicht vom Linux-Artefakt ab", file=sys.stderr)
            return 1
        print(f"MOO_SMB_OK {datei.name}, {datei.stat().st_size} Bytes, SHA256 {lokale_pruefsumme}")
        return 0
    except Exception as fehler:
        print(f"WinRM-Hash-Pruefung fehlgeschlagen: {type(fehler).__name__}: {fehler}",
              file=sys.stderr)
        return 1


def spiegel_dateien(wurzel: Path) -> tuple[list[str], dict[str, int]]:
    """Relevanter Projektstand als sortierte relative POSIX-Pfade (ohne git)."""
    dateien: list[str] = []
    ausgelassen: dict[str, int] = {}

    def auslassen(grund: str) -> None:
        ausgelassen[grund] = ausgelassen.get(grund, 0) + 1

    for ordner, unterordner, namen in os.walk(wurzel):
        rel_ordner = Path(ordner).relative_to(wurzel).as_posix()
        rel_ordner = "" if rel_ordner == "." else rel_ordner + "/"
        behalten = []
        for name in sorted(unterordner):
            pfad = rel_ordner + name + "/"
            if (name in AUSSCHLUSS_ORDNER or name.startswith(".p5-")
                    or pfad.startswith(AUSSCHLUSS_PRAEFIXE)
                    or (pfad.startswith("beispiele/") and name.startswith("run_"))
                    or os.path.islink(os.path.join(ordner, name))):
                auslassen("ordner")
                continue
            behalten.append(name)
        unterordner[:] = behalten
        for name in sorted(namen):
            rel = rel_ordner + name
            voll = Path(ordner) / name
            if voll.is_symlink() or not voll.is_file():
                auslassen("keine_datei")
            elif GEHEIM.search(rel):
                auslassen("geheim")
            elif not rel_ordner and not name.startswith(".") and Path(name).suffix.lower() not in WURZEL_ENDUNGEN:
                auslassen("wurzel_artefakt")
            elif any(WINDOWS_UNGUELTIG.search(teil) for teil in rel.split("/")):
                auslassen("windows_name")
            elif voll.stat().st_size > MAX_BYTES:
                auslassen("zu_gross")
            else:
                dateien.append(rel)
    return sorted(dateien), ausgelassen


def spiegel_manifest(wurzel: Path, dateien: list[str], stand: str = "") -> dict:
    eintraege = [{"pfad": p, "sha256": _sha256(wurzel / p), "bytes": (wurzel / p).stat().st_size}
                 for p in dateien]
    kennung = hashlib.sha256(
        "".join(f"{e['pfad']}\t{e['sha256']}\n" for e in eintraege).encode("utf-8")
    ).hexdigest()
    return {"version": 1, "manifest_id": kennung, "quelle": "linux-synapse",
            "stand": stand or "unbenannt",
            "erzeugt": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "dateien": eintraege}


def spiegeln(erzwingen: bool, nur_liste: bool, stand: str = "") -> int:
    """Einweg-Spiegel des Projektstands nach D:\\MooTest\\repo.

    Nie Dateien aus D:\\MooTest\\repo zurueckkopieren: Fixes nur in Synapse,
    danach erneut spiegeln (Userentscheid 08.11.2026, #37353).
    """
    if not re.fullmatch(r"[A-Za-z0-9_.+:-]{0,80}", stand):
        print("Ungueltiger Stand (erlaubt: A-Z a-z 0-9 _ . + : -)", file=sys.stderr)
        return 2
    dateien, ausgelassen = spiegel_dateien(PROJEKT)
    manifest = spiegel_manifest(PROJEKT, dateien, stand)
    summe = sum(e["bytes"] for e in manifest["dateien"])
    print(f"MOO_SPIEGEL_LISTE dateien={len(dateien)} bytes={summe} stand={manifest['stand']} "
          f"manifest={manifest['manifest_id']} ausgelassen={json.dumps(ausgelassen, sort_keys=True)}")
    lokal = PROJEKT / "build" / "p2" / "windows_minipc"
    lokal.mkdir(parents=True, exist_ok=True)
    (lokal / "spiegel_manifest.json").write_text(
        json.dumps(manifest, indent=1, ensure_ascii=False), encoding="utf-8")
    if nur_liste:
        return 0
    if not _voraussetzungen() or not SPIEGEL_SKRIPT.is_file():
        return 2

    tar_pfad = lokal / SPIEGEL_TAR
    with tarfile.open(tar_pfad, "w", format=tarfile.PAX_FORMAT) as tar:
        for pfad in dateien:
            info = tar.gettarinfo(str(PROJEKT / pfad), arcname=pfad)
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            with (PROJEKT / pfad).open("rb") as f:
                tar.addfile(info, f)
        roh = json.dumps(manifest, ensure_ascii=False).encode("utf-8")
        info = tarfile.TarInfo(".moo-spiegel/manifest.json")
        info.size = len(roh)
        info.mtime = int(time.time())
        tar.addfile(info, io.BytesIO(roh))
    if not _smb_put(tar_pfad, SPIEGEL_TAR) or not _smb_put(SPIEGEL_SKRIPT, SPIEGEL_SKRIPT.name):
        return 1

    skript = REMOTE_DIR + "\\" + SPIEGEL_SKRIPT.name
    befehl = (
        f"& ([scriptblock]::Create([IO.File]::ReadAllText('{skript}'))) "
        f"-Tar '{REMOTE_DIR}\\{SPIEGEL_TAR}' -Ziel '{SPIEGEL_ZIEL}'"
        + (" -Erzwingen" if erzwingen else "")
    )
    try:
        antwort = sitzung_oeffnen().run_ps(befehl)
    except Exception as fehler:
        print(f"WinRM-Spiegel fehlgeschlagen: {type(fehler).__name__}: {fehler}", file=sys.stderr)
        return 1
    sys.stdout.write(antwort.std_out.decode("utf-8", "replace"))
    sys.stderr.write(antwort.std_err.decode("utf-8", "replace"))
    ausgabe = antwort.std_out.decode("utf-8", "replace")
    if antwort.status_code != 0 or f"manifest={manifest['manifest_id']}" not in ausgabe:
        print(f"MOO_SPIEGEL_FEHLER status={antwort.status_code}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    argumente = sys.argv[1:]
    if argumente and argumente[0] == "--spiegeln":
        stand = os.environ.get("MOO_SPIEGEL_STAND", "")
        rest = set()
        for arg in argumente[1:]:
            if arg.startswith("--stand="):
                stand = arg.split("=", 1)[1]
            else:
                rest.add(arg)
        if not rest <= {"--erzwingen", "--nur-liste"}:
            print("Nutzung: python smb_upload.py --spiegeln [--erzwingen] [--nur-liste] [--stand=<Synapse-Batch>]",
                  file=sys.stderr)
            sys.exit(2)
        sys.exit(spiegeln("--erzwingen" in rest, "--nur-liste" in rest, stand))
    if len(argumente) != 1:
        print("Nutzung: python smb_upload.py <lokale Testdatei> | --spiegeln [--erzwingen] [--nur-liste]",
              file=sys.stderr)
        sys.exit(2)
    sys.exit(datei_kopieren(argumente[0]))
