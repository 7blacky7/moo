#!/usr/bin/env bash
# Native Funktionswerte: @nativ-Funktion als Wert (Thunk an der Grenze
# nativ -> dynamisch), Laufzeitfehler fangbar, Diagnosen M-N001/M-N002/M-N003.
set -eu
cd "$(dirname "$0")/../../.."
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
out=build/p2-frontend/native_wert
mkdir -p "$out"
failed=0

# Positiv: Ausgabe gegen .expected (Wert, Rückruf, map/filter, Import, Fehler).
for name in positiv import fehler thread leck; do
  src="compiler/tests/p2/native_wert_${name}.moos"
  if timeout 60s "$compiler" compile "$src" -o "$out/$name" > "$out/$name.log" 2>&1 \
     && timeout 30s "$out/$name" > "$out/$name.out" 2>&1 \
     && cmp -s "compiler/tests/p2/native_wert_${name}.expected" "$out/$name.out"; then
    echo "PASS $name"
  else
    echo "FAIL $name"; failed=1
  fi
done

# IR: der Thunk entsteht nur bei Wertnutzung; der Direktaufruf bleibt ohne Box.
timeout 60s "$compiler" compile compiler/tests/p2/native_wert_positiv.moos --emit-ir -o "$out/positiv.ll" > /dev/null 2>&1
timeout 60s "$compiler" compile compiler/tests/p2/native_call_positiv.moos --emit-ir -o "$out/ohne_wert.ll" > /dev/null 2>&1
if rg -q 'call void @moo_func_strikt' "$out/positiv.ll" \
   && ! rg -q 'moo_func_strikt|__nwname_' "$out/ohne_wert.ll"; then
  echo "PASS ir: Thunk nur bei Wertnutzung"
else
  echo "FAIL ir: Thunk nur bei Wertnutzung"; failed=1
fi

# Negativ: Diagnosecode im Log, Exit != 0.
pruefe_negativ() {
  local name=$1; shift
  local src="compiler/tests/p2/native_wert_${name}.moos"
  local expected="compiler/tests/p2/native_wert_${name}.expected_error"
  set +e
  timeout 60s "$compiler" compile "$src" "$@" --emit-ir -o "$out/$name.ll" > "$out/$name.log" 2>&1
  local rc=$?
  set -e
  if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ] || ! rg -q -F -f "$expected" "$out/$name.log"; then
    echo "FAIL $name: unerwarteter Exit oder Diagnose (Exit $rc)"; failed=1
  else
    echo "PASS $name"
  fi
}
# Ohne versuche: Arityfehler beim Thread-Start gemeldet, kein Absturz.
if timeout 60s "$compiler" compile compiler/tests/p2/native_wert_thread_ungefangen.moos -o "$out/thread_ungefangen" > "$out/thread_ungefangen.log" 2>&1 \
   && timeout 30s "$out/thread_ungefangen" > "$out/thread_ungefangen.out" 2>&1 \
   && rg -q -F "erwartet 2 Argumente, bekommen 1" "$out/thread_ungefangen.out" \
   && rg -q -x -F "ENDE" "$out/thread_ungefangen.out"; then
  echo "PASS thread_ungefangen"
else
  echo "FAIL thread_ungefangen"; failed=1
fi

# Leckprobe: map/filter mit nativem Funktionswert unter LSan (nur wenn libasan
# vorhanden; der Runtime-Bestand allein ist in dieser Probe leckfrei).
asan="$(gcc -print-file-name=libasan.so 2>/dev/null || true)"
if [ -n "$asan" ] && [ -f "$asan" ]; then
  if LD_PRELOAD="$asan" ASAN_OPTIONS=detect_leaks=1 timeout 60s "$out/leck" > "$out/leck.lsan.log" 2>&1; then
    echo "PASS leck: LSan ohne Befund"
  else
    echo "FAIL leck: LSan meldet Lecks (siehe $out/leck.lsan.log)"; failed=1
  fi
  # Thread-Start mit Arityfehler: Fehlerpfad gibt Funktion und Argument frei.
  if timeout 60s "$compiler" compile compiler/tests/p2/native_wert_thread_leck.moos -o "$out/thread_leck" > "$out/thread_leck.log" 2>&1 \
     && LD_PRELOAD="$asan" ASAN_OPTIONS=detect_leaks=1 timeout 60s "$out/thread_leck" > "$out/thread_leck.lsan.log" 2>&1 \
     && rg -q -F "erwartet 2 Argumente, bekommen 1" "$out/thread_leck.lsan.log"; then
    echo "PASS thread_leck: LSan ohne Befund"
  else
    echo "FAIL thread_leck (siehe $out/thread_leck.lsan.log)"; failed=1
  fi
else
  echo "SKIP leck: libasan nicht gefunden"
fi

pruefe_negativ struktur
pruefe_negativ system
pruefe_negativ bare --no-stdlib
pruefe_negativ extern
exit "$failed"
