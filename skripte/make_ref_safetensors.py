#!/usr/bin/env python3
"""Referenz-safetensors erzeugen + Forward-Referenzwerte drucken (Plan-014 F1).

Erzeugt /tmp/moo_f1_ref.safetensors NUR mit Python-Stdlib (struct+json —
bewusst OHNE das safetensors-Package: so beweist der Test die Format-
Kompatibilitaet gegen die SPEZIFIKATION, nicht gegen eine Library).

Netz: dicht(2,2, keine Aktivierung): y = x @ w + b
  w = [[1.5, -2.0], [0.25, 4.0]]   b = [0.5, -1.0]
  x = [[1.0, 2.0]]
  y = [1*1.5 + 2*0.25 + 0.5,  1*(-2) + 2*4 - 1] = [2.5, 5.0]

Das Gegenstueck (beispiele/ki_safetensors_import.moos) laedt die Datei,
baut die Matmul von Hand nach und muss exakt [2.5, 5.0] liefern.
"""
import json
import struct

w = [1.5, -2.0, 0.25, 4.0]          # [2,2] row-major
b = [0.5, -1.0]                     # [2]

header = {
    "w": {"dtype": "F32", "shape": [2, 2], "data_offsets": [0, 16]},
    "b": {"dtype": "F32", "shape": [2], "data_offsets": [16, 24]},
    "__metadata__": {"quelle": "moo-F1-Referenz (pure stdlib)"},
}
hj = json.dumps(header, separators=(",", ":")).encode("utf-8")

with open("/tmp/moo_f1_ref.safetensors", "wb") as f:
    f.write(struct.pack("<Q", len(hj)))
    f.write(hj)
    f.write(struct.pack("<4f", *w))
    f.write(struct.pack("<2f", *b))

x = [1.0, 2.0]
y = [x[0] * w[0] + x[1] * w[2] + b[0], x[0] * w[1] + x[1] * w[3] + b[1]]
print("Referenz-Datei: /tmp/moo_f1_ref.safetensors")
print(f"Referenz-Forward fuer x=[1,2]: {y}")
