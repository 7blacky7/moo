# Geordneter Handoff: laufenden GPU-Prozess beenden lassen, dann vor dem
# nächsten GPU-Start die eigene Steuerung beenden und Sperre freigeben.
import fcntl, os, pathlib, signal, time
pid = int(os.environ["P5_EXT05_PARENT_PID"])
result = pathlib.Path(os.environ["P5_EXT05_LAST_RESULT"])
with pathlib.Path("build/p5_gpu.lock").open("a+") as lock:
    fcntl.flock(lock, fcntl.LOCK_EX)
    deadline = time.monotonic() + 30
    while not result.is_file():
        if time.monotonic() >= deadline:
            raise SystemExit("Aktueller Ergebnisbericht nach Lockfreigabe fehlt")
        time.sleep(0.1)
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        print("STEUERUNG BEREITS BEENDET", pid, flush=True)
    else:
        print("HANDOFF STOP NACH ERGEBNIS", pid, result, flush=True)
