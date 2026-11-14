#!/usr/bin/env python3
"""Windows-Testbox über die bereits vorhandene direkte WinRM-Verbindung verwalten."""

import argparse
import base64
import getpass
import hashlib
import json
import os
import sys
from pathlib import Path

# Nur Benutzerkonfiguration, niemals im Synapse-Projekt versionieren.
CONFIG = Path.home() / ".config" / "moo-minipc" / "winrm.json"
ENDPOINT = "http://" + os.environ.get("MOO_MINIPC_HOST", "192.168.77.2") + ":5985/wsman"
REMOTE_DIR = r"D:\MooTest"


def konfiguration_speichern() -> None:
    """Benutzer und Kennwort einmal lokal abfragen und privat speichern."""
    name = input("Windows-Benutzer [MoTest]: ").strip() or "MoTest"
    kennwort = getpass.getpass("MoTest-Passwort: ")
    if not kennwort:
        raise ValueError("Ein nichtleeres Windows-Passwort wird benötigt.")
    CONFIG.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(CONFIG.parent, 0o700)
    flags = os.O_WRONLY | os.O_CREAT | os.O_TRUNC
    fd = os.open(CONFIG, flags, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as datei:
        json.dump({"user": name, "password": kennwort}, datei)
    os.chmod(CONFIG, 0o600)
    print("Lokaler Zugang gespeichert. Kein Kennwort in der Kommandozeile.")


def sitzung_oeffnen():
    """Öffnet dieselbe NTLM-Sitzung für Befehle und Dateiübertragung."""
    import winrm

    daten = json.loads(CONFIG.read_text(encoding="utf-8"))
    benutzer = daten["user"]
    if "\\" not in benutzer and "@" not in benutzer:
        benutzer = "DESKTOP-LOJN555\\" + benutzer
    return winrm.Session(
        ENDPOINT,
        auth=(benutzer, daten["password"]),
        transport="ntlm",
        operation_timeout_sec=20,
        read_timeout_sec=35,
    )


def powershell_ausfuehren(befehl: str) -> int:
    """Führt einen PowerShell-Befehl mit NTLM über das direkte Kabel aus."""
    if not CONFIG.is_file():
        print("Erst lokal auf Linux 'setup' ausführen.", file=sys.stderr)
        return 2
    try:
        antwort = sitzung_oeffnen().run_ps(befehl)
        sys.stdout.write(antwort.std_out.decode("utf-8", "replace"))
        sys.stderr.write(antwort.std_err.decode("utf-8", "replace"))
        return antwort.status_code
    except Exception as fehler:
        print(f"Windows-Verbindung fehlgeschlagen: {type(fehler).__name__}: {fehler}",
              file=sys.stderr)
        return 1


def datei_hochladen(quellpfad: str) -> int:
    """Kopiert eine Build-Datei in den Windows-Testordner und prüft SHA-256.

    Base64-Blöcke bleiben klein genug für standardmäßige WinRM-Grenzen.
    Passwort und Dateiinhalt werden nicht in Synapse-Ausgaben ausgegeben.
    """
    if not CONFIG.is_file():
        print("Zuerst die lokale Anmeldung einrichten.", file=sys.stderr)
        return 2
    quelle = Path(quellpfad).resolve()
    if not quelle.is_file() or "'" in quelle.name or not quelle.name.isascii():
        print("Fehler: Datei fehlt oder Dateiname ist ungeeignet.", file=sys.stderr)
        return 2
    ziel = REMOTE_DIR + "\\" + quelle.name
    try:
        sitzung = sitzung_oeffnen()
        vorbereiten = (
            "$ErrorActionPreference='Stop';"
            f"[IO.Directory]::CreateDirectory('{REMOTE_DIR}') | Out-Null;"
            f"[IO.File]::WriteAllBytes('{ziel}', [byte[]]@())"
        )
        antwort = sitzung.run_ps(vorbereiten)
        if antwort.status_code:
            raise RuntimeError(antwort.std_err.decode("utf-8", "replace"))
        with quelle.open("rb") as datei:
            while block := datei.read(1024):
                daten = base64.b64encode(block).decode("ascii")
                befehl = (
                    "$ErrorActionPreference='Stop';"
                    f"$daten=[Convert]::FromBase64String('{daten}');"
                    f"$f=[IO.File]::Open('{ziel}',[IO.FileMode]::Append,"
                    "[IO.FileAccess]::Write,[IO.FileShare]::None);"
                    "try {$f.Write($daten,0,$daten.Length)} finally {$f.Dispose()}"
                )
                antwort = sitzung.run_ps(befehl)
                if antwort.status_code:
                    raise RuntimeError(antwort.std_err.decode("utf-8", "replace"))
        pruefung = sitzung.run_ps(
            f"(Get-FileHash -LiteralPath '{ziel}' -Algorithm SHA256).Hash"
        )
        lokal = hashlib.sha256(quelle.read_bytes()).hexdigest()
        entfernt = pruefung.std_out.decode("utf-8", "replace").strip().lower()
        if pruefung.status_code or lokal != entfernt:
            raise RuntimeError("SHA-256-Prüfung nach Übertragung fehlgeschlagen")
        print(f"UPLOAD OK: {quelle.name} -> {ziel}, SHA256 {lokal}")
        return 0
    except Exception as fehler:
        print(f"Dateiübertragung fehlgeschlagen: {type(fehler).__name__}: {fehler}",
              file=sys.stderr)
        return 1


def main() -> int:
    """Stellt Einmal-Anmeldung, Verbindungstest und Remote-Befehle bereit."""
    parser = argparse.ArgumentParser(description="Moo-Mini-PC: Windows per WinRM")
    parser.add_argument("aktion", choices=["setup", "check", "run", "runfile", "upload"])
    parser.add_argument("befehl", nargs=argparse.REMAINDER)
    argumente = parser.parse_args()
    if argumente.aktion == "setup":
        konfiguration_speichern()
        return powershell_ausfuehren("$env:COMPUTERNAME; whoami")
    if argumente.aktion == "check":
        return powershell_ausfuehren("$env:COMPUTERNAME; whoami; $PSVersionTable.PSVersion.ToString()")
    if argumente.aktion == "upload":
        if len(argumente.befehl) != 1:
            parser.error("'upload' benötigt genau einen lokalen Dateipfad.")
        return datei_hochladen(argumente.befehl[0])
    if argumente.aktion == "runfile":
        if len(argumente.befehl) != 1:
            parser.error("'runfile' benoetigt genau einen Projektdateipfad.")
        quelle = Path(argumente.befehl[0])
        if not quelle.is_file() or quelle.suffix.lower() != ".ps1":
            parser.error("PowerShell-Projektdatei (.ps1) fehlt.")
        return powershell_ausfuehren(quelle.read_text(encoding="utf-8"))
    if not argumente.befehl:
        parser.error("Bei 'run' einen PowerShell-Befehl angeben.")
    return powershell_ausfuehren(" ".join(argumente.befehl))


if __name__ == "__main__":
    sys.exit(main())
