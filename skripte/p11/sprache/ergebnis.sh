#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
out=${MOO_P11_RESULT_OUT:-$root/build/p11/ergebnis}
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/ergebnis.moos -o "$out/probe"
"$out/probe" >"$out/ausgabe"
diff -u compiler/tests/p11/ergebnis.expected "$out/ausgabe"
echo "PASS Erfolg/Fehler, Struktur-/void-Nutzwert, Weitergabe, Cleanup, Vtable und SysV-Rueckruf"
for target in x86_64-unknown-linux-gnu x86_64-bare x86_64-unknown-uefi; do
  "$compiler" compile compiler/tests/p11/ergebnis_probe.moos --no-stdlib --target "$target" --emit-ir -o "$out/$target.ll"
  mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
done
# ABI-Evidenz: kleines Resultat ergebnis[u32,u32] via echtem SysV-Thunk.
grep -E '^define .*\{ i64, i64 \}.*@__moo_rueckruf.ergebnis_klein\(' "$out/x86_64-bare.ll"
awk '/^define .*@__moo_native.ergebnis_klein\(/,/^}/' "$out/x86_64-bare.ll" >"$out/heapfrei.ll"
test -s "$out/heapfrei.ll"
if grep -E 'call .*@moo_(alloc|malloc|struct_box|struct_erstellen)' "$out/heapfrei.ll"; then
  echo "FAIL Ergebniswert allokiert Heap" >&2
  exit 1
fi
mise exec -- llc -O2 -filetype=asm "$out/x86_64-bare.ll" -o "$out/bare.s"
echo "PASS IR-ABI und nativer Ergebnispfad ohne Heap-Boxing"
declare -A erwartet=(
  [verworfen]="Unbenutztes ergebnis"
  [bindung]="Unbenutztes ergebnis"
  [ueberschrieben]="Unbenutztes ergebnis"
  [zweig]="Unbenutztes ergebnis"
  [kurzschluss]="Unbenutztes ergebnis"
  [schleife]="Unbenutztes ergebnis"
  [defer]="Unbenutztes ergebnis"
  [opak]="ergebnis ist opak"
  [weiter_ohne]="weitergeben braucht eine Funktion"
  [fehlertyp]="Fehlertyp stimmt nicht"
  [nutzwert]="erwartet u64, erhalten i64"
  [flucht]="M-M002"
  [weiter_flucht]="M-M002"
  [indirekt]="Unbenutztes ergebnis"
  [dynamisch]="nur im typisierten @nativ"
)
for fall in verworfen bindung ueberschrieben zweig kurzschluss schleife defer opak weiter_ohne fehlertyp nutzwert flucht weiter_flucht indirekt dynamisch; do
  if "$compiler" compile "compiler/tests/p11/ergebnis_$fall.moos" --emit-ir -o "$out/$fall.ll" >"$out/$fall.log" 2>&1; then
    echo "FAIL ungueltiges Ergebnis akzeptiert: $fall" >&2
    exit 1
  fi
  grep -F "${erwartet[$fall]}" "$out/$fall.log"
  echo "PASS negativer Vertrag $fall"
done
