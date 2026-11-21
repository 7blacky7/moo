#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out="$PWD/build/p11/array-q/t27/cpu"
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/cpu_bereich.moos --no-stdlib --target x86_64-unknown-linux-gnu --emit-ir -o "$out/host.ll"
# Nur Beobachtung im Testartefakt: echten CPUID-Befehl und Operanden beibehalten.
uv run python - "$out/host.ll" "$out/zaehlen.ll" <<'PY'
from pathlib import Path
import sys
lines=Path(sys.argv[1]).read_text().splitlines()
result=[]
count=0
for line in lines:
    if 'asm sideeffect "cpuid"' in line or 'asm "cpuid"' in line:
        result.append('  call void @cpu_cpuid_zaehlen()')
        count+=1
    result.append(line)
assert count>=4, count
result.append('declare void @cpu_cpuid_zaehlen()')
Path(sys.argv[2]).write_text('\n'.join(result)+'\n')
print(f'CPU-Orakel beobachtet {count} unveraenderte CPUID-Stellen')
PY
mise exec -- opt -passes='default<O2>,verify' -S "$out/zaehlen.ll" -o "$out/host.opt.ll"
mise exec -- llvm-extract --delete --func=main "$out/host.opt.ll" -o "$out/host.bc"
mise exec -- llc -O2 -relocation-model=pic -filetype=obj "$out/host.bc" -o "$out/host.o"
gcc -std=c11 -O2 -Wall -Wextra -Werror -pthread compiler/tests/p11/cpu_bereich.c "$out/host.o" -Wl,--wrap=sched_setaffinity -o "$out/cpu-bereich"
"$out/cpu-bereich"
for target in x86_64-bare x86_64-unknown-uefi x86_64-windows; do
  "$compiler" compile compiler/tests/p11/cpu_bereich.moos --no-stdlib --target "$target" --emit-ir -o "$out/$target.ll"
  mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
  if rg -q '@__moo_native.cpu_scope.features' "$out/$target.ll"; then
    echo "FAIL unsupported CPU scope cached: $target" >&2; exit 1
  fi
done
for target in x86_64-bare x86_64-unknown-uefi; do
  rg -q '__moo_simd_zustand' "$out/$target.ll"
done
for fall in argument simd abi; do
  if "$compiler" compile "compiler/tests/p11/cpu_bereich_$fall.moos" --no-stdlib --target x86_64-unknown-linux-gnu --emit-ir -o "$out/neg.ll" > "$out/$fall.log" 2>&1; then
    echo "FAIL invalid cpu_bereich accepted: $fall" >&2; exit 1
  fi
  rg -q 'M-C000' "$out/$fall.log"
  rg -q '@cpu_bereich' "$out/$fall.log"
done
"$compiler" compile compiler/tests/p11/cpu_bereich_fangen.moos -o "$out/fangen"
test "$("$out/fangen")" = "$(printf 'CPU-Bereich gefangen\nCPU-Affinität restauriert')"
echo 'CPU-Bereich: Linux-Guards/Affinitaet/Cleanup und sicherer Bare-/UEFI-/Windows-Fallback OK'
