#!/usr/bin/env python3
# P7-T18 Messhilfe: erzeugt <ziel>/netz_treiber.moos mit dem Vorher-Zugriffsweg
# (speicher_lesen/-schreiben + dynamisches port_lese32/port_schreibe32, kein
# @nativ; bis P7-T22 hiessen diese kern_inl/kern_outl, gleiche Wirkung) aus dem
# aktuellen Treiber, damit Vorher/Nachher verschraenkt gemessen werden koennen.
# Aufruf: python3 -I ab_alt.py beispiele/uefi/netz_treiber.moos <zielordner>
# Danach netz_e1000.moos in den Zielordner kopieren und mit
# MOO_UEFI_APP=<zielordner>/netz_e1000.moos MOO_UEFI_PRUEFUNG=e1000 MOO_UEFI_KVM=1
# bash scripts/uefi-smoke.sh laufen lassen (Zeile MOO-E1000-MESS).
import sys, pathlib
quelle, ziel = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
t = quelle.read_text(encoding="utf-8")
a = t.index("# ---------------- Hardwarezugriff (@nativ")
b = t.index("#\n# ZUSTAND je nic")
t = t[:a] + t[b:]
ers = [
    ("gib_zurück nt_mmio_lese32(basis + (bdf << 12) + off)\n        gib_zurück nt_cfg_lese32(0x80000000 | (bdf << 8) | (off & 0xFC))",
     "gib_zurück speicher_lesen(basis + (bdf << 12) + off, 4)\n        port_schreibe32(0xCF8, 0x80000000 | (bdf << 8) | (off & 0xFC))\n        gib_zurück port_lese32(0xCFC)"),
    ("nt_mmio_schreibe32(basis + (bdf << 12) + off, wert)\n        sonst:\n            nt_cfg_schreibe32(0x80000000 | (bdf << 8) | (off & 0xFC), wert)",
     "speicher_schreiben(basis + (bdf << 12) + off, wert, 4)\n        sonst:\n            port_schreibe32(0xCF8, 0x80000000 | (bdf << 8) | (off & 0xFC))\n            port_schreibe32(0xCFC, wert)"),
    ("gib_zurück nt_mmio_lese32(speicher_lesen(nic + 8, 8) + reg)",
     "gib_zurück speicher_lesen(speicher_lesen(nic + 8, 8) + reg, 4)"),
    ("nt_mmio_schreibe32(speicher_lesen(nic + 8, 8) + reg, wert)",
     "speicher_schreiben(speicher_lesen(nic + 8, 8) + reg, wert, 4)"),
    ("nt_mmio_glocke32(speicher_lesen(nic + 8, 8) + reg, wert)",
     "speicher_schreiben(speicher_lesen(nic + 8, 8) + reg, wert, 4)"),
    ("gib_zurück nt_status_lesen(speicher_lesen(nic + 8, 8))",
     "gib_zurück speicher_lesen(speicher_lesen(nic + 8, 8) + 8, 4)"),
    ("wenn nt_desk_fertig(desk + 12) == falsch:",
     "wenn (speicher_lesen(desk + 12, 1) & 1) == 0:"),
]
for alt, neu in ers:
    assert t.count(alt) == 1, alt
    t = t.replace(alt, neu)
assert "\n@nativ\n" not in t and "nt_mmio" not in t
ziel.mkdir(parents=True, exist_ok=True)
(ziel / "netz_treiber.moos").write_text(t, encoding="utf-8")
print("ok", ziel / "netz_treiber.moos")
