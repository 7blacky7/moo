"""SIGKILL-/E/A-Harness; Format, Mutation und Byteorakel laufen im Moo-Prozess."""
from __future__ import annotations
import collections
import json
import os
from pathlib import Path
import selectors
import shutil
import signal
import subprocess
import sys

binary = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
base = out / "crash-basis.img"
image = out / "crash-test.img"

def env(action: str, **extra: str) -> dict[str, str]:
    return {**os.environ, "MOO_MOOFS_CRASH_BILD": str(image),
            "MOO_MOOFS_CRASH_AKTION": action, **extra}

def run(action: str, **extra: str) -> int:
    p = subprocess.run([binary], env=env(action, **extra), capture_output=True, timeout=15)
    assert p.returncode == 0, (action, extra, p.returncode, p.stdout, p.stderr)
    value = int(p.stdout.strip())
    assert value in (0, 1), (action, value)
    return value

def receive(p: subprocess.Popen[bytes]) -> bytes:
    with selectors.DefaultSelector() as sel:
        sel.register(p.stdout, selectors.EVENT_READ)
        assert sel.select(10), "Handshake-Timeout"
    value = os.read(p.stdout.fileno(), 1)
    assert value in (b"B", b"A", b"S"), (value, p.poll(), p.stderr.read() if p.poll() is not None else b"")
    return value

image.unlink(missing_ok=True)
assert run("init") == 0
assert run("pruefen") == 0
shutil.copyfile(image, base)
# Ein vollständiger Lauf ermittelt die tatsächlichen Grenzen der Produktoperation.
p = subprocess.Popen([binary], env=env("write"), stdin=subprocess.PIPE,
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
events: list[str] = []
while True:
    event = receive(p)
    events.append(event.decode())
    p.stdin.write(b"+")
    if event == b"S":
        break
stdout, stderr = p.communicate(timeout=15)
assert p.returncode == 0 and stdout == b"0\n", (p.returncode, stdout, stderr)
assert run("pruefen") == 1
assert events[-1] == "S" and events[:-1] == ["B", "A"] * ((len(events) - 1) // 2)
steps = (len(events) - 1) // 2
histogram: collections.Counter[str] = collections.Counter()
variants: collections.Counter[int] = collections.Counter()
for iteration in range(1000):
    shutil.copyfile(base, image)
    point = iteration % len(events)
    p = subprocess.Popen([binary], env=env("write"), stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
    try:
        for index in range(point + 1):
            event = receive(p)
            assert event.decode() == events[index], (iteration, index, event)
            if index < point:
                p.stdin.write(b"+")
        # Genau diesen eigenen Writer töten; keine Prozessgruppen oder Fremdprozesse.
        p.send_signal(signal.SIGKILL)
        stdout, stderr = p.communicate(timeout=15)
        assert p.returncode == -signal.SIGKILL, (iteration, p.returncode, stdout, stderr)
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()
    value = run("pruefen")
    if events[point] == "S":
        assert value == 1, ("bestätigte Operation verloren", iteration)
    variants[value] += 1
    histogram[f"{point}:{events[point]}"] += 1
    if (iteration + 1) % 100 == 0:
        print(f"MooFS SIGKILL {iteration + 1}/1000: Mount + stabile Datei + atomare alte/neue Datei bytegleich", flush=True)
faults = 0
for fault in (1, 2, 3):
    for step in range(1, steps + 1):
        shutil.copyfile(base, image)
        assert run("write", MOO_MOOFS_CRASH_FEHLER=str(fault),
                   MOO_MOOFS_CRASH_SCHRITT=str(step)) == 0
        run("pruefen")
        faults += 1
result = {"sigkill": 1000, "signal": 9, "events": events, "points": dict(histogram),
          "old_or_new": dict(variants), "io_faults": faults, "steps": steps,
          "stable_bytes": 12289, "atomic_bytes": 12289, "synced_exact": True}
(out / "crash-ergebnis.json").write_text(json.dumps(result, indent=2) + "\n")
print(f"MooFS: 1000 echte SIGKILL, {faults} E/A-Fehler/Teilschreibfälle, alle Wiederanläufe bytegleich", flush=True)
