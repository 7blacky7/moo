"""Privater Profilpatch für den eigenen PE32+-Moo-Loader.

Bit 0 setzt vor dem Kernsprung Windows-BootNext, Bit 1 bindet den Kern an
die ESP des gestarteten EFI-Images. Der alte Vertrag erlaubt nur Bit 0.
"""
import pathlib
import struct
import sys
src = pathlib.Path(sys.argv[1])
dst = pathlib.Path(sys.argv[2]) if sys.argv[2] else None
unveraendert = sys.argv[3] == "1"
modus = int(sys.argv[4])
if modus not in (0, 1, 2, 3):
    raise SystemExit("Unbekanntes Loader-Zielprofil")
if dst is not None and src.resolve() == dst.resolve():
    raise SystemExit("Quell-Loader darf nicht ueberschrieben werden")
original = src.read_bytes()
data = bytearray(original)
if data[:2] != b"MZ" or len(data) < 64:
    raise SystemExit("Kein PE-Loader")
pe = struct.unpack_from("<I", data, 60)[0]
if pe + 24 > len(data) or data[pe:pe+4] != b"PE\0\0":
    raise SystemExit("Kein PE-Kopf")
machine, count = struct.unpack_from("<HH", data, pe+4)
optlen = struct.unpack_from("<H", data, pe+20)[0]
opt = pe + 24
if machine != 0x8664 or optlen < 152 or opt + optlen > len(data):
    raise SystemExit("Kein PE64 x86-64")
if struct.unpack_from("<H", data, opt)[0] != 0x20b or struct.unpack_from("<H", data, opt+68)[0] != 10:
    raise SystemExit("Kein EFI-Application PE32+")
if struct.unpack_from("<I", data, opt+64)[0] or any(data[opt+144:opt+152]):
    raise SystemExit("Nur eigener unsignierter Loader ohne PE-Pruefsumme")
vertraege = {b"!MOORT25": (0, 1), b"!MOORTV2": (0, 1, 2, 3)}
hits = []
for i in range(count):
    h = opt + optlen + i*40
    if h+40 > len(data):
        raise SystemExit("Abgeschnittener Abschnittskopf")
    length, start = struct.unpack_from("<II", data, h+16)
    flags = struct.unpack_from("<I", data, h+36)[0]
    if start + length > len(data):
        raise SystemExit("Abgeschnittener Abschnitt")
    if not flags & 0x80000000:
        continue
    for magic, erlaubt in vertraege.items():
        pos = data.find(magic, start, start+length)
        while pos >= 0:
            if pos+16 <= start+length:
                hits.append((pos, erlaubt))
            pos = data.find(magic, pos+1, start+length)
if len(hits) != 1:
    raise SystemExit("Rueckkehrvertrag nicht eindeutig im schreibbaren PE-Abschnitt")
pos, erlaubt = hits[0]
if struct.unpack_from("<Q", data, pos+8)[0] not in erlaubt:
    raise SystemExit("Unbekannte Rueckkehrkonfiguration")
if modus not in erlaubt:
    raise SystemExit("ESP-Profil braucht einen Loader mit Vertrag !MOORTV2")
struct.pack_into("<Q", data, pos+8, modus)
if unveraendert and data != original:
    if modus == 1:
        raise SystemExit("Testloader muss sichere Windows-BootNext-Rueckkehr aktiv haben (--loader-konfigurieren)")
    raise SystemExit("Testloader muss das kombinierte ESP-/Windows-BootNext-Profil haben")
if dst is not None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)
profil = []
if modus & 1:
    profil.append("Windows-BootNext vor Kernstart, BootOrder unveraendert")
if modus & 2:
    profil.append("ESP des gestarteten Loaders")
beschreibung = ", ".join(profil) or "Standardprofil"
if dst is None:
    print(f"Loader-Vorbereitung {src}: {beschreibung}")
else:
    print(f"Loader-Kopie {dst}: {beschreibung}")
