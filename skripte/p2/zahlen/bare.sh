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
# 4. P2-T58 B8: Batch A im Bare-Profil (Locals anderer Typen, Pure->Pure mit
#    i64/u8/bool-Rueckgabe): Fehlerpfad trappt, QEMU liefert 44 1 0.
"$compiler" compile "$tests/native_zahlen_kern_a.moos" --no-stdlib --target x86_64-bare --emit-ir -o "$out/kern_a.ll"
mise exec -- opt -passes=verify -disable-output "$out/kern_a.ll"
if grep -q 'moo_native_fehler' "$out/kern_a.ll"; then echo "FEHLER: Bare-IR (Batch A) referenziert moo_native_fehler" >&2; exit 1; fi
grep -q 'llvm.trap' "$out/kern_a.ll" || { echo "FEHLER: Bare-IR (Batch A) ohne llvm.trap" >&2; exit 1; }
MOO_BIN="$compiler" KERNEL_MARKER='44 1 0' \
    bash scripts/kernel-smoke.sh "$tests/native_zahlen_kern_a.moos"
echo "PASS Bare Batch A: Locals anderer Typen und Pure->Pure (i64/u8/bool) in QEMU"
# 5. P2-T58 D2c (Plan f40b5980 G4): Float-%/** im Bare-Profil rufen moo_native_fmod/
#    pow/powf (kein frem, kein fmod/pow aus libm); der Kernel linkt moo_native_math.c
#    (main.rs KERNEL_SOURCES), das nm-Gate von kernel-smoke laesst kein offenes
#    Symbol zu. QEMU liefert 5 1024 3 -6.
"$compiler" compile "$tests/native_zahlen_kern_float.moos" --no-stdlib --target x86_64-bare --emit-ir -o "$out/kern_float.ll"
mise exec -- opt -passes=verify -disable-output "$out/kern_float.ll"
grep -q 'call double @moo_native_fmod(' "$out/kern_float.ll" || { echo "FEHLER: Bare-% ohne moo_native_fmod" >&2; exit 1; }
grep -q 'call float @moo_native_powf(' "$out/kern_float.ll" || { echo "FEHLER: Bare-** (f32) ohne moo_native_powf" >&2; exit 1; }
if grep -qE ' frem |call .*@(fmod|fmodf|pow|powf|llvm\.pow[^(]*)\(' "$out/kern_float.ll"; then echo "FEHLER: Bare-Float-%/** ueber frem oder libm" >&2; exit 1; fi
MOO_BIN="$compiler" KERNEL_MARKER='5 1024 3 -6' \
    bash scripts/kernel-smoke.sh "$tests/native_zahlen_kern_float.moos"
echo "PASS Bare Float-%/**: moo_native_math.c im Kernel, QEMU 5 1024 3 -6, keine libm"
# 6. P2-T58 D2c (Plan f40b5980 §3.2, Review a7d2cf59 #8): UEFI-Link. Den C-Export
#    mit Float-%/** aus dem UEFI-IR extrahieren und als COFF uebersetzen: offen sind
#    nur moo_native_fmod/moo_native_powf (ggf. _fltused der MS-Float-ABI, sonst
#    von boot/uefi_rt.c). Mit moo_native_math.c (clang, Flags wie
#    scripts/uefi-smoke.sh, dort in RT_SOURCES) per lld-link zur EFI-Application.
"$compiler" compile "$tests/native_uefi_float.moos" --no-stdlib --target x86_64-unknown-uefi --emit-ir -o "$out/uefi_float.ll"
mise exec -- opt -passes=verify -disable-output "$out/uefi_float.ll"
mise exec -- llvm-extract -S --recursive --func=moo_uefi_start "$out/uefi_float.ll" -o "$out/uefi_float.extract.ll"
mise exec -- llc -O2 -filetype=obj "$out/uefi_float.extract.ll" -o "$out/uefi_float.obj"
uefi_offen="$(mise exec -- llvm-nm -u "$out/uefi_float.obj" | awk '{print $NF}' | awk '$0 != "_fltused"' | sort | tr '\n' ' ')"
[ "$uefi_offen" = "moo_native_fmod moo_native_powf " ] || { echo "FEHLER: UEFI-Objekt offen: '$uefi_offen'" >&2; exit 1; }
uefi_cflags=(--target=x86_64-w64-windows-gnu -std=c11 -O2 -ffreestanding -fno-stack-protector
             -mno-stack-arg-probe -ffunction-sections -fdata-sections -DMOO_BARE)
clang "${uefi_cflags[@]}" -c compiler/runtime/moo_native_math.c -o "$out/uefi_native_math.obj"
printf 'int _fltused = 0;\n' > "$out/uefi_fltused.c"
clang "${uefi_cflags[@]}" -c "$out/uefi_fltused.c" -o "$out/uefi_fltused.obj"
rm -f "$out/uefi_float.efi"
lld-link /subsystem:efi_application /entry:moo_uefi_start /nodefaultlib /opt:ref \
    /out:"$out/uefi_float.efi" "$out/uefi_float.obj" "$out/uefi_native_math.obj" "$out/uefi_fltused.obj"
file "$out/uefi_float.efi" | grep -qiE 'EFI.*application|PE32\+' || { echo "FEHLER: kein PE32+-EFI-Binary" >&2; exit 1; }
echo "PASS UEFI-Link Float-%/**: Export aus x86_64-unknown-uefi + moo_native_math.c -> EFI-Application"
# 7. P2-T89: sin/cos/tan (f64/f32, auch |x| = 1e6) und stdlib/vektor-Drehung
#    (dmat4/mat4, Komponentenindex v[i]; auch in sicheren Koerpern mit
#    Zwischenwert vec4(...)[i] und dvec4-Local) im Bare-Profil ueber moo_native_sin/
#    cos/tan[f]; kein sinf/cosf/tanf/sin/cos/tan aus libm, nm-Gate ohne offenes
#    Symbol. QEMU liefert die gekappten Werte (gleich hosted C mit libm).
"$compiler" compile "$tests/native_zahlen_kern_trig.moos" --no-stdlib --target x86_64-bare --emit-ir -o "$out/kern_trig.ll"
mise exec -- opt -passes=verify -disable-output "$out/kern_trig.ll"
for f in moo_native_sin moo_native_cos moo_native_tan moo_native_sinf moo_native_cosf moo_native_tanf; do
    grep -qE "call (double|float) @$f\(" "$out/kern_trig.ll" || { echo "FEHLER: Bare-Trigonometrie ohne $f" >&2; exit 1; }
done
if grep -qE 'call .*@(sin|cos|tan|sinf|cosf|tanf|llvm\.(sin|cos|tan)[^(]*)\(' "$out/kern_trig.ll"; then
    echo "FEHLER: Bare-Trigonometrie ueber libm/llvm-Intrinsic" >&2; exit 1
fi
MOO_BIN="$compiler" KERNEL_MARKER='479425 877582 546302 84147 936752 218503 1000 8660 32 40' \
    bash scripts/kernel-smoke.sh "$tests/native_zahlen_kern_trig.moos"
echo "PASS Bare Trigonometrie: moo_native_sin/cos/tan[f] im Kernel, vektor-Drehung dmat4/mat4, keine libm"
echo "PASS P2-T73 Bare: IR-Vertrag, QEMU-Werte, QEMU-Trap"
