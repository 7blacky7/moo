#!/usr/bin/env bash
# P2-T118: Closure-Captures duerfen nicht lecken (ASan/LSan).
# Vor dem Fix hielt jede erzeugte Closure ihre Captures doppelt
# (load_var +1 und moo_func_with_captures +1, freigegeben nur einmal).
# Aufruf: closure_leck_lsan.sh [compiler] [ausgabeordner]
set -u
project_root="$(cd "$(dirname "$0")/../../.." && pwd)"
compiler="${1:-$project_root/compiler/target/release/moo-compiler}"
out="${2:-$project_root/build/p2-closure-lsan}"
rm -rf "$out"; mkdir -p "$out/san"
clang_bin="$(mise exec -- which clang)"
printf '#!/bin/sh\nexec "%s" -fsanitize=address "$@"\n' "$clang_bin" > "$out/san/cc"
chmod +x "$out/san/cc"
cat > "$out/closure.moos" << 'EOF'
funktion fabrik(name):
    setze cb auf () => name
    gib_zurück cb

funktion zwei(a, b):
    setze cb auf (x) => a + b + x
    gib_zurück cb

setze i auf 0
setze r auf ""
setze s auf ""
solange i < 100:
    setze c auf fabrik("wert-" + i)
    setze r auf c()
    setze d auf zwei("a-" + i, "b")
    setze s auf d("!")
    setze i auf i + 1
zeige r
zeige s
EOF
PATH="$out/san:$PATH" "$compiler" compile "$out/closure.moos" -o "$out/closure" > "$out/compile.log" 2>&1 \
    || { cat "$out/compile.log" >&2; exit 1; }
ASAN_OPTIONS=detect_leaks=1 "$out/closure" > "$out/run.out" 2> "$out/run.err"
rc=$?
# LSan beendet per _exit, bevor die Moo-Ausgabe geflusht ist: erst Leck pruefen.
if [ "$rc" -ne 0 ] || grep -q 'LeakSanitizer' "$out/run.err"; then
    grep -E '^SUMMARY|Direct leak|Indirect leak' "$out/run.err" >&2
    echo "FEHLER: Closure-Captures lecken (rc=$rc)" >&2
    exit 1
fi
printf 'wert-99\na-99b!\n' | diff -u - "$out/run.out" || { echo 'FEHLER: Ausgabe' >&2; exit 1; }
echo 'PASS Closure-Captures ohne Leck unter ASan/LSan'
