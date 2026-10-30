"""Rohbyte-Sampling in maschinenlesbares JSON mit echtem UTF-8-Status wandeln."""
import json
import subprocess
import sys

result = subprocess.run([sys.argv[1]], check=True, stdout=subprocess.PIPE)
report = json.loads(result.stdout)
if len(report["ausgaben"]) != 24:
    raise ValueError("Sampling-Anzahl")
for item in report["ausgaben"]:
    raw = bytes(item["rohbytes"])
    if len(raw) > 120:
        raise ValueError("Bytegrenze")
    try:
        item["text"] = raw.decode("utf-8", "strict")
        item["utf8_gueltig"] = True
    except UnicodeDecodeError:
        item["text"] = None
        item["utf8_gueltig"] = False
print(json.dumps(report, ensure_ascii=True, separators=(",", ":")))
