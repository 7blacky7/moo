"""Bindet einen Ed25519-Laufschlüssel an die private Kernkopie."""
import base64
import binascii
import pathlib
import struct
import sys

MAGIE = b"MOOSAU01"
VERTRAGSGROESSE = 48
ED25519 = b"ssh-ed25519"
SCHLUESSELKOPF = struct.pack("!I", len(ED25519)) + ED25519 + struct.pack("!I", 32)


def public_key_datei(pfad):
    with pathlib.Path(pfad).open("rb") as datei:
        roh = datei.read(1025)
    felder = roh.split(maxsplit=2)
    if len(roh) > 1024 or len(felder) < 2 or felder[0] != ED25519:
        raise SystemExit("Ed25519-Laufkey hat kein OpenSSH-Public-Key-Format")
    try:
        blob = base64.b64decode(felder[1], validate=True)
    except binascii.Error as fehler:
        raise SystemExit("Ed25519-Laufkey hat ungültiges Base64") from fehler
    if len(blob) != 51 or not blob.startswith(SCHLUESSELKOPF) or not any(blob[19:]):
        raise SystemExit("Ed25519-Laufkey ist leer oder hat ein ungültiges Format")
    return blob[19:]


def auth_konfigurieren(elf, public_key=None):
    if len(elf) < 64 or elf[:7] != b"\x7fELF\x02\x01\x01" or struct.unpack_from("<HH", elf, 16) != (2, 62):
        raise SystemExit("SSH-Laufprofil braucht ELF64 x86-64 EXEC")
    start = struct.unpack_from("<Q", elf, 32)[0]
    groesse, anzahl = struct.unpack_from("<HH", elf, 54)
    if groesse != 56 or start + groesse * anzahl > len(elf):
        raise SystemExit("Ungültige ELF-Programmkopftabelle")
    fundstellen = []
    for i in range(anzahl):
        p = start + groesse * i
        typ, flags, basis = struct.unpack_from("<IIQ", elf, p)
        if typ != 1 or not flags & 2:
            continue
        ende = basis + struct.unpack_from("<Q", elf, p + 32)[0]
        if ende > len(elf):
            raise SystemExit("Schreibbares PT_LOAD reicht über die ELF-Datei hinaus")
        pos = elf.find(MAGIE, basis, ende)
        while pos != -1:
            if pos + VERTRAGSGROESSE <= ende:
                fundstellen.append(pos)
            pos = elf.find(MAGIE, pos + 1, ende)
    if len(fundstellen) != 1:
        raise SystemExit(f"SSH-Auth-Vertrag nicht eindeutig in schreibbarem PT_LOAD (Treffer: {len(fundstellen)})")
    pos = fundstellen[0]
    if any(elf[pos + 8:pos + VERTRAGSGROESSE]):
        raise SystemExit("SSH-Auth-Vertrag muss unprovisioniert sein")
    if public_key is not None:
        schluessel = public_key_datei(public_key)
        struct.pack_into("<Q", elf, pos + 8, 2)
        elf[pos + 16:pos + VERTRAGSGROESSE] = schluessel


if __name__ == "__main__":
    kernel, oeffentlicher_key = sys.argv[1:]
    datei = pathlib.Path(kernel)
    daten = bytearray(datei.read_bytes())
    auth_konfigurieren(daten, oeffentlicher_key)
    datei.write_bytes(daten)
    print("SSH-Auth-Vertrag mit öffentlichem Ed25519-Laufkey aktiviert")
