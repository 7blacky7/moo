#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/ffi_types
compiler=compiler/target/release/moo-compiler
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
