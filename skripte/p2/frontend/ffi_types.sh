#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/ffi_types
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
out=build/p2-frontend/ffi_types/alias
timeout 30s "$compiler" compile compiler/tests/p2/ffi_typ_alias.moos -o "$out"
timeout 30s "$out" > "$out.out"
cmp compiler/tests/p2/ffi_typ_alias.expected "$out.out"
echo "PASS FFI-Pointer-Deklarationen angenommen"
out=build/p2-frontend/ffi_types/bool
set +e
timeout 30s "$compiler" compile compiler/tests/p2/ffi_typ_bool_zeiger.moos --emit-ir -o "$out.ll" > "$out.log" 2>&1
rc=$?
set -e
if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ] || ! rg -q -F -f compiler/tests/p2/ffi_typ_bool_zeiger.expected_error "$out.log"; then
  echo "FAIL ptr[bool]: unerwarteter Exit oder Diagnose (Exit $rc)"
  exit 1
fi
echo "PASS ptr[bool] abgelehnt"

# P2-T97 (34119, Review e6cafe73): ptr[bool] in jeder Typposition abgelehnt.
for fall in rueckruf_param rueckruf_ergebnis tief struktur indirekt; do
  out=build/p2-frontend/ffi_types/bool_$fall
  set +e
  timeout 30s "$compiler" compile "compiler/tests/p2/ffi_typ_bool_zeiger_$fall.moos" --emit-ir -o "$out.ll" > "$out.log" 2>&1
  rc=$?
  set -e
  if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ] || ! rg -q -F "ptr[bool] ist nicht freigegeben" "$out.log"; then
    echo "FAIL ptr[bool] $fall: unerwarteter Exit oder Diagnose (Exit $rc)"
    cat "$out.log"
    exit 1
  fi
done
echo "PASS ptr[bool] abgelehnt: Rueckruf-Parameter, -Ergebnis, verschachtelt, Strukturfeld, nativer Funktionszeiger"
# bool als Wert und ptr[fn(bool) -> bool] bleiben erlaubt.
out=build/p2-frontend/ffi_types/bool_wert
timeout 30s "$compiler" compile compiler/tests/p2/ffi_typ_bool_wert.moos --emit-ir -o "$out.ll" > "$out.log" 2>&1 || {
  echo "FAIL bool als Wert/Rueckruf abgelehnt"; cat "$out.log"; exit 1; }
echo "PASS bool als Wert und ptr[fn(bool) -> bool] angenommen"
