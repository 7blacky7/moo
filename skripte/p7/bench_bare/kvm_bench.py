#!/usr/bin/env python3
"""P7-T20: misst echten, von Moo erzeugten Kernel-Code unter QEMU/KVM.

Aufruf: kvm_bench.py <kern.elf> <laeufe> <cpu> [<cpu2> ...]
Baut eine GRUB-ISO (wie scripts/kernel-smoke.sh), startet QEMU mit KVM,
1 vCPU, per taskset auf die angegebene Host-CPU gepinnt, und misst die
Zeit zwischen den seriellen Zeilen "111" (Start) und "999" (Ende).
Die Zeile nach 999 ist die Pruefsumme. Ausgabe: je Lauf Sekunden, dann
Minimum und Median. Regeln: Memory testumgebung-vms-linux-pc (1 VM, 256 MB).
"""
import os, shutil, statistics, subprocess, sys, tempfile, time


def iso_bauen(elf, tmp):
    iso = os.path.join(tmp, "iso")
    os.makedirs(os.path.join(iso, "boot", "grub"))
    shutil.copy(elf, os.path.join(iso, "boot", "k.elf"))
    with open(os.path.join(iso, "boot", "grub", "grub.cfg"), "w") as f:
        f.write('set timeout=0\nset default=0\nmenuentry "moo" {\n'
                '    multiboot2 /boot/k.elf\n    boot\n}\n')
    out = os.path.join(tmp, "k.iso")
    subprocess.run(["grub-mkrescue", "-o", out, iso], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out


def lauf(iso, cpu):
    cmd = ["taskset", "-c", cpu, "qemu-system-x86_64", "-enable-kvm", "-cpu", "host",
           "-smp", "1", "-m", "256M", "-cdrom", iso, "-display", "none",
           "-no-reboot", "-serial", "stdio", "-monitor", "none"]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    t0 = t1 = None
    summe = None
    frist = time.monotonic() + 300
    try:
        for roh in p.stdout:
            z = roh.decode(errors="replace").strip()
            jetzt = time.monotonic()
            if z == "111" and t0 is None:
                t0 = jetzt
            elif z == "999" and t0 is not None and t1 is None:
                t1 = jetzt
            elif t1 is not None and z:
                summe = z
                break
            if jetzt > frist:
                break
    finally:
        p.kill()
        p.wait()
    if t0 is None or t1 is None:
        raise SystemExit("FEHLER: Marker 111/999 nicht gesehen")
    return t1 - t0, summe


def main():
    elf, n, cpus = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
    tmp = tempfile.mkdtemp(prefix="moo_kvm_bench.")
    try:
        iso = iso_bauen(elf, tmp)
        zeiten = []
        summen = set()
        for i in range(n):
            dt, s = lauf(iso, cpus[i % len(cpus)])
            zeiten.append(dt)
            summen.add(s)
            print(f"lauf {i}: {dt:.4f} s summe={s}", flush=True)
        print(f"ERGEBNIS {os.path.basename(elf)}: min={min(zeiten):.4f} s "
              f"median={statistics.median(zeiten):.4f} s summen={sorted(summen)}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
