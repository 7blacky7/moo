#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/native_calls
compiler=compiler/target/release/moo-compiler
src=compiler/tests/p2/native_call_positiv.moos
out=build/p2-frontend/native_calls/positiv
timeout 30s "$compiler" compile "$src" --emit-ir -o "$out.ll"
timeout 30s "$compiler" compile "$src" -o "$out"
timeout 30s "$out" > "$out.out"
cmp compiler/tests/p2/native_call_positiv.expected "$out.out"
awk '
/^define .*@("?__moo_native[.]p2_call_frueh"?)/ {inside=1; next}
/^define .*@("?__moo_native[.]p2_call_spaet"?)/ {inside=2; next}
inside && /^}/ {inside=0}
inside == 1 && /call i32 @("?__moo_native[.]p2_call_spaet"?)/ {forward=1}
inside == 2 && /call i32 @("?__moo_native[.]p2_call_doppelt"?)/ {backward=1}
inside && /call .*@moo_(number|to_number|new_number)/ {boxing=1}
END {if (!forward || !backward || boxing) exit 1}
' "$out.ll"
echo "PASS direkte i32-Vorwärts-/Rückwärtsaufrufe und Ausgabe"
# Rekursion: Pure-Form mit Selbst- bzw. Wechselaufruf laeuft im Systemkoerper
# (kompiliert, IR gueltig; ohne Abbruchbedingung endet sie erst zur Laufzeit).
for name in rekursiv zyklus; do
  timeout 30s "$compiler" compile "compiler/tests/p2/native_call_${name}.moos" --emit-ir -o "build/p2-frontend/native_calls/$name.ll"
  mise exec -- opt -passes=verify -disable-output "build/p2-frontend/native_calls/$name.ll"
done
src=compiler/tests/p2/native_call_rekursion_lauf.moos
out=build/p2-frontend/native_calls/rekursion_lauf
timeout 30s "$compiler" compile "$src" -o "$out"
timeout 30s "$out" > "$out.out"
cmp compiler/tests/p2/native_call_rekursion_lauf.expected "$out.out"
src=compiler/tests/p2/native_call_rekursion_formen.moos
out=build/p2-frontend/native_calls/rekursion_formen
timeout 30s "$compiler" compile "$src" -o "$out"
timeout 30s "$out" > "$out.out"
cmp compiler/tests/p2/native_call_rekursion_formen.expected "$out.out"
echo "PASS Rekursion (Pure-Form umgeleitet, wenn-/Wechselrekursion, reiner Aufrufer)"
failed=0
for name in arity arity_dyn arity_system typ ffi_transitiv doppelt; do
  src="compiler/tests/p2/native_call_${name}.moos"
  expected="compiler/tests/p2/native_call_${name}.expected_error"
  out="build/p2-frontend/native_calls/$name"
  set +e
  timeout 30s "$compiler" compile "$src" --emit-ir -o "$out.ll" > "$out.log" 2>&1
  rc=$?
  set -e
  if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ] || ! rg -q -F -f "$expected" "$out.log"; then
    echo "FAIL $name: unerwarteter Exit oder Diagnose (Exit $rc)"
    failed=1
  else
    echo "PASS $name"
  fi
done
exit "$failed"
