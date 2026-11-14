#!/usr/bin/env bash
# P2-T73: Bare-Laufzeitnachweis der nativen Zahlen-/Cast-/bool-Semantik (P2-T34).
# Multiboot-Kernel aus compiler/tests/p2/native_zahlen_kern*.moos, Boot in QEMU
# ueber scripts/kernel-smoke.sh (Bestand: Build, nm-Gate, MB2, serielle Marker).
# Voraussetzung: Option A (P2-T43, Batch 33002): main ruft unsicher:-Systemkoerper
# mit Literalen. Fehlt QEMU/GRUB, skippt kernel-smoke transparent (kein Beweis).
set -euo pipefail

zahlen_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$zahlen_dir/../../.." && pwd)"
out="$project_dir/build/p2-zahlen"
compiler="${MOO_COMPILER:-$project_dir/compiler/target/release/moo-compiler}"
tests="$project_dir/compiler/tests/p2"
mkdir -p -- "$out"
cd "$project_dir"

# 1. Bare-IR: Fehlerpfade trappen (sys.trap), kein moo_native_fehler und kein
#    weak-Hook; saettigende Float->Int-Konversion als Intrinsic ohne Bibliothek.
"$compiler" compile "$tests/native_zahlen_kern.moos" --no-stdlib --target x86_64-bare --emit-ir -o "$out/kern.ll"
mise exec -- opt -passes=verify -disable-output "$out/kern.ll"
if grep -q 'moo_native_fehler' "$out/kern.ll"; then echo "FEHLER: Bare-IR referenziert moo_native_fehler" >&2; exit 1; fi
grep -q 'sys.trap' "$out/kern.ll" || { echo "FEHLER: Bare-Systemkoerper ohne sys.trap" >&2; exit 1; }
grep -q 'llvm.trap' "$out/kern.ll" || { echo "FEHLER: Bare-IR ohne llvm.trap" >&2; exit 1; }
grep -Eq 'llvm\.fpto[su]i\.sat' "$out/kern.ll" || { echo "FEHLER: saettigende Konversion nicht als Intrinsic" >&2; exit 1; }
echo "PASS Bare-IR: sys.trap/llvm.trap statt moo_native_fehler, fptosi.sat als Intrinsic"

# 2. QEMU: Werte seriell (Division/Rest/Shift/Wrap, Casts geprueft/wrap/saettigend,
#    Float-Rundung, Vergleich, Kurzschluss, NaN). nm-Gate von kernel-smoke:
#    keine undefined-/hosted-Symbole, also auch keine Float-Bibliotheksaufrufe.
MOO_BIN="$compiler" KERNEL_MARKER='-2 -1 2 -128 200 255 127 0 7 16777216 1' \
    bash scripts/kernel-smoke.sh "$tests/native_zahlen_kern.moos"
echo "PASS Bare QEMU: 14 Werte der Zahlen-/Cast-/bool-Semantik seriell"

# 3. QEMU: Trap-Vertrag. 41 erscheint, nach der Division durch 0 nie 42.
MOO_BIN="$compiler" KERNEL_MARKER='41' KERNEL_MARKER_FEHLT='42' \
    bash scripts/kernel-smoke.sh "$tests/native_zahlen_kern_trap.moos"
echo "PASS Bare QEMU: Division durch 0 trappt, kein Fortlauf"
echo "PASS P2-T73 Bare: IR-Vertrag, QEMU-Werte, QEMU-Trap"
