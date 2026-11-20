#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out=build/p11/vfs/asm
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/syscall_asm.moos -o "$out/host"
"$out/host" >"$out/host.txt"
diff -u compiler/tests/p11/syscall_asm.expected "$out/host.txt"
"$compiler" compile compiler/tests/p11/syscall_asm_nackt.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/bare.ll"
mise exec -- opt -passes=verify -disable-output "$out/bare.ll"
mise exec -- llc -filetype=obj "$out/bare.ll" -o "$out/bare.o"
mise exec -- llvm-objdump -d "$out/bare.o" >"$out/bare.asm"
grep -Eq '%r10, %gs:0x10|%r10, %gs:16' "$out/bare.asm"
grep -Eq 'movq.*0x7|movq.*\$7' "$out/bare.asm"
negativ() {
  local name="$1" meldung="$2"
  if "$compiler" compile "compiler/tests/p11/syscall_asm_$name.moos" --no-stdlib --target x86_64-bare --emit-ir -o "$out/negativ.ll" >"$out/$name.log" 2>&1; then
    echo "FEHLER: $name wurde angenommen" >&2; exit 1
  fi
  grep -Fq "$meldung" "$out/$name.log" || { cat "$out/$name.log"; exit 1; }
}
negativ global 'kein @pro_kern statisch-Objekt'
negativ lokal 'pro_kern_versatz erwartet'
negativ index 'Index muss eine nichtnegative Ganzzahlkonstante'
negativ grenze 'Arrayindex außerhalb'
negativ laufzeit 'i-Eingang muss eine Ganzzahlkonstante'
negativ nackt_laufzeit '@nackt: assembler ohne eingaenge=/ausgang='
echo 'PASS CPU-lokale Offsetkonstanten/Immediate-ASM: Host, Naked-GS-ASM, 6 Negativfälle'
