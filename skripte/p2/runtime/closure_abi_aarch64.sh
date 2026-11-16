#!/usr/bin/env bash
# P2-T119 (aus P2-T118, Thought f147f27b): Closure-/Lambda-ABI auf AArch64 zur
# Laufzeit pruefen (qemu-aarch64 user). Deckt den c_aufteilungs_wrapper
# (<name>.__moo_cabi mit [2 x i64]) ab, den moo_func_call_N fuer >=3 Parameter
# aus C ruft (P2-T117).
# Aufruf: closure_abi_aarch64.sh [compiler] [ausgabeordner]
# Umgebung: MOO_AARCH64_ROOT (Standard build/leck-nacht/arm) mit
#   qemu/usr/bin/qemu-aarch64-static   (Debian qemu-user-static, nur entpackt)
#   sysroot/                           (Debian libc6, libc6-dev, libgcc-12-dev,
#                                       linux-libc-dev arm64, nur entpackt)
# Fehlt eins davon: Exit 2 (nicht gruen), mit Hinweis. Keine Installation.
# Runtime: alle compiler/runtime/*.c mit clang --target=aarch64-linux-gnu; was
# fuer Linux/AArch64 nicht baut (bare/wasm/win32/GUI), bleibt draussen und wird
# protokolliert. Link statisch mit ld.lld (--gc-sections, unaufgeloeste Symbole
# der nicht genutzten Teile ignoriert).
set -u
project_root="$(cd "$(dirname "$0")/../../.." && pwd)"
compiler="$(realpath "${1:-$project_root/compiler/target/release/moo-compiler}")"
out="$(realpath -m "${2:-$project_root/build/p2-closure-abi-aarch64}")"
arm="$(realpath -m "${MOO_AARCH64_ROOT:-$project_root/build/leck-nacht/arm}")"
qemu="$arm/qemu/usr/bin/qemu-aarch64-static"
sysroot="$arm/sysroot"
libdir="$sysroot/usr/lib/aarch64-linux-gnu"
gccdir="$(ls -d "$sysroot"/usr/lib/gcc/aarch64-linux-gnu/* 2>/dev/null | head -1)"
for p in "$qemu" "$libdir/crt1.o" "$libdir/libc.a" "$gccdir/libgcc.a"; do
  [[ -e "$p" ]] || { echo "FEHLT: $p (MOO_AARCH64_ROOT pruefen, siehe Kopf dieses Skripts)"; exit 2; }
done
[[ -x "$compiler" ]] || { echo "FEHLT: Compiler $compiler"; exit 2; }
clang_bin="$(mise exec -- which clang 2>/dev/null || command -v clang)"
lld_bin="$(mise exec -- which ld.lld 2>/dev/null || command -v ld.lld)"
[[ -n "$clang_bin" && -n "$lld_bin" ]] || { echo "FEHLT: clang/ld.lld"; exit 2; }
rm -rf "$out"; mkdir -p "$out/rt"

# Runtime fuer aarch64-linux-gnu
rt="$project_root/compiler/runtime"; gebaut=0; aus=0
for c in "$rt"/*.c; do
  # Bare-Metal/UEFI/Win32/WASM definieren dieselben Symbole anders (moo_retain,
  # moo_print, ...) und gehoeren nicht in eine hosted Linux-Runtime.
  case "$(basename "$c")" in
    moo_bare*|*uefi*|*win32*|*wasm*) echo "$(basename "$c") (Zielplattform)" >>"$out/rt_aus.txt"; continue;;
  esac
  o="$out/rt/$(basename "${c%.c}").o"
  if "$clang_bin" --target=aarch64-linux-gnu --sysroot="$sysroot" -O2 -std=gnu11 -fPIC \
       -ffunction-sections -fdata-sections -w -I"$rt" -c "$c" -o "$o" 2>>"$out/rt_aus.log"; then
    gebaut=$((gebaut+1))
  else
    echo "$(basename "$c")" >>"$out/rt_aus.txt"; aus=$((aus+1))
  fi
done
rm -f "$out/librt.a"; ar rcs "$out/librt.a" "$out"/rt/*.o
echo "Runtime aarch64: $gebaut Dateien gebaut, $aus ausgelassen ($out/rt_aus.txt)"

fehler=0
for t in test_closure_abi_viele_param test_closure_multi_call test_lambda_callable_capture; do
  src="$project_root/compiler/tests/regression/$t.moos"
  exp="$project_root/compiler/tests/regression/$t.expected"
  [[ -f "$src" && -f "$exp" ]] || { echo "$t FEHLT"; fehler=$((fehler+1)); continue; }
  if ! "$compiler" compile "$src" --target aarch64 -o "$out/$t.o" >"$out/$t.clog" 2>&1; then
    echo "$t KOMPILIERFEHLER"; fehler=$((fehler+1)); continue
  fi
  if ! "$lld_bin" -static -o "$out/$t.elf" --gc-sections --unresolved-symbols=ignore-all \
       "$libdir/crt1.o" "$libdir/crti.o" "$gccdir/crtbeginT.o" "$out/$t.o" "$out/librt.a" \
       --start-group "$libdir/libm.a" "$libdir/libc.a" "$gccdir/libgcc.a" "$gccdir/libgcc_eh.a" --end-group \
       "$gccdir/crtend.o" "$libdir/crtn.o" >"$out/$t.llog" 2>&1; then
    echo "$t LINKFEHLER ($out/$t.llog)"; fehler=$((fehler+1)); continue
  fi
  timeout 120 "$qemu" "$out/$t.elf" >"$out/$t.out" 2>"$out/$t.err"; rc=$?
  if [[ $rc -eq 0 ]] && diff -q "$out/$t.out" "$exp" >/dev/null; then
    echo "$t PASS"
  else
    echo "$t FAIL rc=$rc (diff $out/$t.out $exp)"; fehler=$((fehler+1))
  fi
done
# Beleg, dass der Wrapper im AArch64-IR steht (sonst prueft der Lauf nichts).
if "$compiler" compile "$project_root/compiler/tests/regression/test_closure_abi_viele_param.moos" \
     --target aarch64 --emit-ir -o "$out/abi.ll" >/dev/null 2>&1 \
   && grep -q '__moo_cabi' "$out/abi.ll" && grep -q '\[2 x i64\]' "$out/abi.ll"; then
  echo "IR: __moo_cabi mit [2 x i64] vorhanden"
else
  echo "IR: __moo_cabi/[2 x i64] FEHLT"; fehler=$((fehler+1))
fi
exit $fehler
