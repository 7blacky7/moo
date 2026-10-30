"""Bounded POSIX worker control for future P4 experiments.

Wall-clock deadlines and process-group termination are NOT GPU-time telemetry.
This module must only control a trusted, foreground, non-detaching worker on
an exclusively allocated GPU. SIL-10, rights, snapshot sealing, process/GPU
isolation and persistent cross-restart accounting remain external prerequisites.
Never treats a wall-clock pass as proof of GPU-time compliance.
"""
import math
import os
import signal
import subprocess
import threading
import time


class WallBudget:
    def __init__(self, *, max_runs=9, per_run=600.0, total=7200.0):
        if (type(max_runs) is not int or not 1 <= max_runs <= 9
                or type(per_run) not in (int, float) or not math.isfinite(per_run)
                or type(total) not in (int, float) or not math.isfinite(total)
                or not 0 < per_run <= 600 or not 0 < total <= 7200):
            raise ValueError("budget exceeds approved ceilings")
        if os.name != "posix" or not all(hasattr(os, n) for n in ("waitid", "WNOWAIT", "P_PID")):
            raise RuntimeError("safe unreaped process-group tracking unavailable on this platform")
        self.max_runs = max_runs
        self.per_run = float(per_run)
        self.total = float(total)
        self.started = time.monotonic()
        self.runs = 0
        self._lock = threading.Lock()

    def run(self, argv, *, cwd, env):
        """Run one trusted worker, retain bounded stderr and kill its process group.

        Counts time since object creation, including gaps/evaluation. A failed
        launch counts as a run. No shell. Requires exactly one caller at a time.
        Outside processes, detached children and GPU activity that outlives
        worker exit are not bounded by this process group.
        """
        if (not isinstance(argv, (list, tuple)) or not argv
                or any(not isinstance(x, str) or not x for x in argv)):
            raise ValueError("argv must be a nonempty sequence of strings")
        if not self._lock.acquire(blocking=False):
            raise RuntimeError("parallel workers are prohibited")
        proc = None
        try:
            remaining = self.total - (time.monotonic() - self.started)
            if self.runs >= self.max_runs or remaining <= 0:
                raise RuntimeError("wall budget exhausted")
            limit = min(self.per_run, remaining)
            self.runs += 1
            start = time.monotonic()
            proc = subprocess.Popen(list(argv), cwd=cwd, env=env, shell=False,
                                    start_new_session=True, stdin=subprocess.DEVNULL,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            # Drain continuously to avoid pipe deadlock; retain bounded stderr.
            error_bytes = bytearray()
            def drain_error():
                while True:
                    block = proc.stderr.read(4096)
                    if not block:
                        return
                    room = max(0, 8192 - len(error_bytes))
                    error_bytes.extend(block[:room])
            reader = threading.Thread(target=drain_error, daemon=True)
            reader.start()
            # Do not reap the group leader before signalling its group:
            # WNOWAIT keeps its PID reserved until after killpg.
            deadline = start + limit
            timed_out = False
            try:
                while True:
                    event = os.waitid(os.P_PID, proc.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
                    if event is not None:
                        # Conservative: a completion first observed after the
                        # deadline is not accepted as an in-budget success.
                        timed_out = time.monotonic() >= deadline
                        break
                    if time.monotonic() >= deadline:
                        timed_out = True
                        break
                    time.sleep(min(0.02, max(0.0, deadline - time.monotonic())))
            finally:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                rc = proc.wait()
                # Ensure no member of the worker process group remains before
                # accepting output. Detached children are outside this guarantee.
                group_deadline = time.monotonic() + 2.0
                while True:
                    try:
                        os.killpg(proc.pid, 0)
                    except ProcessLookupError:
                        break
                    if time.monotonic() >= group_deadline:
                        raise RuntimeError("worker process group did not terminate")
                    time.sleep(0.02)
            reader.join(timeout=2.0)
            if reader.is_alive():
                raise RuntimeError("worker stderr pipe did not close")
            proc.stderr.close()
            if timed_out:
                rc = None
            elapsed = time.monotonic() - start
            return {"returncode": rc, "timed_out": timed_out,
                    "stderr": bytes(error_bytes).decode("utf-8", errors="replace"),
                    "wall_seconds": elapsed, "run": self.runs,
                    "wall_total_seconds": time.monotonic() - self.started}
        finally:
            self._lock.release()
