#!/usr/bin/env bash
set -u
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/flow
compiler=compiler/target/release/moo-compiler
failed=0
for name in nested_unhandled nested_caller defer_unhandled break_outside continue_outside break_inside continue_inside break_outer continue_outer defer_heap main_erfolg main_fehler main_defer_erfolg main_defer_fehler main_defer_ende return_tief main_return_tief throw_tief; do
  out="build/p2-frontend/flow/$name"
  timeout 30s "$compiler" compile "compiler/tests/p2/try_flow_${name}.moos" -o "$out" > "$out.compile.log" 2>&1
  rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "FAIL $name: Compile-Exit $rc"
    failed=1
    continue
  fi
  # P2-T95: Abbruchvertrag nur noch mit MOO_STRENG=1 (Standard: Hinweis + Weiterlauf).
  case "$name" in nested_unhandled|defer_unhandled|break_outside|continue_outside) streng=1 ;; *) streng=0 ;; esac
  MOO_STRENG=$streng timeout 30s "$out" > "$out.out" 2> "$out.err"
  rc=$?
  case "$name" in
    nested_unhandled|defer_unhandled|break_outside|continue_outside)
      if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ] || ! rg -q -F 'Unbehandelter Fehler' "$out.err"; then
        echo "FAIL $name: unbehandelter Fehler fehlt (Exit $rc)"
        failed=1
        continue
      fi ;;
    *)
      if [ "$rc" -ne 0 ]; then
        echo "FAIL $name: Laufzeit-Exit $rc"
        failed=1
        continue
      fi ;;
  esac
  if ! cmp -s "compiler/tests/p2/try_flow_${name}.expected" "$out.out"; then
    echo "FAIL $name: Ausgabe stimmt nicht"
    failed=1
    continue
  fi
  echo "PASS $name"
done
exit "$failed"
