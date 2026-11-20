#!/usr/bin/env bash
# P11-T2: zwei Implementierungen, konkreter/virtueller Aufruf und strenge Typfehler.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
out=${MOO_P11_OUT:-$root/build/p11/schnittstellen}
mkdir -p "$out"
src=compiler/tests/p11/schnittstelle_dispatch.moos
"$compiler" compile "$src" -o "$out/dispatch"
"$out/dispatch" >"$out/dispatch.out"
diff -u compiler/tests/p11/schnittstelle_dispatch.expected "$out/dispatch.out"
echo "PASS zwei Implementierungen, lesender/schreibender Dispatch und implizite Argumentkonvertierung"
"$compiler" compile compiler/tests/p11/schnittstelle_abi.moos -o "$out/abi"
"$out/abi" >"$out/abi.out"
diff -u compiler/tests/p11/schnittstelle_abi.expected "$out/abi.out"
for abi_target in x86_64-bare x86_64-unknown-uefi; do
  "$compiler" compile compiler/tests/p11/schnittstelle_abi.moos --no-stdlib --target "$abi_target" --emit-ir -o "$out/abi-$abi_target.ll"
  mise exec -- opt -passes=verify -disable-output "$out/abi-$abi_target.ll"
done
echo "PASS Vtable-ABI: u8/i8/bool und Strukturparameter/-rückgabe"

# P11-T12: fallible interne Vtables müssen Fehler bis zum nativen Aufrufer
# propagieren; Register-/SRET-Strukturen und Result dürfen nicht weiterlaufen.
"$compiler" compile compiler/tests/p11/schnittstelle_fehler.moos -o "$out/fehler"
"$out/fehler" >"$out/fehler.out"
diff -u compiler/tests/p11/schnittstelle_fehler.expected "$out/fehler.out"
"$compiler" compile compiler/tests/p11/schnittstelle_puffer.moos -o "$out/puffer"
"$out/puffer" >"$out/puffer.out"
diff -u compiler/tests/p11/schnittstelle_puffer.expected "$out/puffer.out"
for fehler_ziel in x86_64-unknown-linux-gnu x86_64-pc-windows-msvc x86_64-bare x86_64-unknown-uefi; do
  "$compiler" compile compiler/tests/p11/schnittstelle_fehler_probe.moos --no-stdlib --target "$fehler_ziel" --emit-ir -o "$out/fehler-$fehler_ziel.ll"
  mise exec -- opt -passes=verify -disable-output "$out/fehler-$fehler_ziel.ll"
  "$compiler" compile compiler/tests/p11/schnittstelle_puffer_probe.moos --no-stdlib --target "$fehler_ziel" --emit-ir -o "$out/puffer-$fehler_ziel.ll"
  mise exec -- opt -passes=verify -disable-output "$out/puffer-$fehler_ziel.ll"
done
if "$compiler" compile compiler/tests/p2/rueckruf_neg_struktur_fehlbar.moos --emit-ir -o "$out/extern-struktur.ll" >"$out/extern-struktur.log" 2>&1; then
  echo "FAIL externer fehlbarer Struktur-Rückruf akzeptiert" >&2
  exit 1
fi
grep -F "kann fehlschlagen und liefert eine Struktur" "$out/extern-struktur.log"
echo "PASS interne Vtable-Fehlerpfade und Bytepuffer; externer C-Vertrag bleibt streng"
for puffer_neg in typ rueckgabe; do
  if "$compiler" compile "compiler/tests/p11/schnittstelle_puffer_$puffer_neg.moos" --emit-ir -o "$out/puffer-neg-$puffer_neg.ll" >"$out/puffer-neg-$puffer_neg.log" 2>&1; then
    echo "FAIL unerlaubter Puffervertrag $puffer_neg" >&2; exit 1
  fi
  grep -F "Vtable benötigt" "$out/puffer-neg-$puffer_neg.log"
done
if "$compiler" compile compiler/tests/p2/rueckruf_neg_fehlerwert_fehlt.moos --emit-ir -o "$out/extern-skalar.ll" >"$out/extern-skalar.log" 2>&1; then
  echo "FAIL externer fehlbarer Skalar-Rückruf ohne Fehlerwert akzeptiert" >&2; exit 1
fi
grep -F "@rückruf(fehlerwert) angeben" "$out/extern-skalar.log"

for target in x86_64-unknown-linux-gnu x86_64-bare x86_64-unknown-uefi; do
  "$compiler" compile "$src" --no-stdlib --target "$target" --emit-ir -o "$out/$target.ll"
  mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
done
uv run --no-project --offline python -B "$root/skripte/p11/sprache/schnittstellen_ir.py" "$out/x86_64-bare.ll"
declare -A erwartet=(
  [fehlt]="Methode 'schreiben' fehlt"
  [parameter]="erwartet u64, erhalten i64"
  [receiver]="erwartet ptr[Fehler], erhalten ptr[PlusSpeicher]"
  [ergebnis]="Rückgabe: erwartet u64, erhalten u32"
  [kein_impl]="implementiert 'ptr[dyn Speicher]' nicht"
  [kein_trait]="keine eindeutig deklarierte Schnittstelle"
  [zeiger]="erwartet ptr[u8], erhalten ptr[i8]"
  [doppelt]="Feld/Methode mehrfach deklariert"
  [flucht]="geliehener Zeiger entkommt"
  [structzeiger_falsch]="SignalPunkt"
)
for fall in fehlt parameter receiver ergebnis kein_impl kein_trait zeiger doppelt flucht structzeiger_falsch; do
  if "$compiler" compile "compiler/tests/p11/schnittstelle_$fall.moos" --emit-ir -o "$out/$fall.ll" >"$out/$fall.log" 2>&1; then
    echo "FAIL ungültige Schnittstelle akzeptiert: $fall" >&2
    exit 1
  fi
  grep -F "${erwartet[$fall]}" "$out/$fall.log"
  if [[ "$fall" == flucht ]]; then
    grep -F "M-M002" "$out/$fall.log"
  else
    grep -F "M-C000" "$out/$fall.log"
  fi
  echo "PASS negativer Vertrag $fall"
done
"$compiler" compile compiler/tests/test_interface.moos -o "$out/legacy-interface"
"$out/legacy-interface" >"$out/legacy-interface.out"
diff -u compiler/tests/test_interface.expected "$out/legacy-interface.out"
echo "PASS vorhandene dynamische Klassen/Interfaces"
