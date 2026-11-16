#!/usr/bin/env bash
# P2-T120: Das Ergebnis eines Threads darf nicht lecken, auch wenn ein anderer
# Thread als der erste Wartende den Thread-Wert zuletzt freigibt
# (warten_mehrfach Fall 1). Laeuft unter ASan/LSan.
# Aufruf: warten_leck_lsan.sh [compiler] [ausgabeordner]
set -u
project_root="$(cd "$(dirname "$0")/../../.." && pwd)"
compiler="${1:-$project_root/compiler/target/release/moo-compiler}"
out="${2:-$project_root/build/p2-warten-lsan}"
rm -rf "$out"; mkdir -p "$out/san"
clang_bin="$(mise exec -- which clang)"
printf '#!/bin/sh\nexec "%s" -fsanitize=address "$@"\n' "$clang_bin" > "$out/san/cc"
chmod +x "$out/san/cc"
fehler=0
for p in warten_freigabe warten_mehrfach starte_mehrere starte_lambda_mehrere; do
    quelle="$project_root/compiler/tests/p2/rc_geteilt/$p.moos"
    [ -f "$quelle" ] || continue
    PATH="$out/san:$PATH" "$compiler" compile "$quelle" -o "$out/$p" > "$out/$p.clog" 2>&1 \
        || { echo "$p KOMPILIERFEHLER" >&2; cat "$out/$p.clog" >&2; fehler=$((fehler+1)); continue; }
    ASAN_OPTIONS=detect_leaks=1 "$out/$p" > "$out/$p.out" 2> "$out/$p.err"
    rc=$?
    # LSan beendet per _exit, bevor die Moo-Ausgabe geflusht ist: erst Leck pruefen.
    if [ "$rc" -ne 0 ] || grep -q 'LeakSanitizer' "$out/$p.err"; then
        grep -E '^SUMMARY|Direct leak|Indirect leak' "$out/$p.err" >&2
        echo "$p FEHLER: Leck oder rc=$rc" >&2
        fehler=$((fehler+1))
        continue
    fi
    erwartet="$project_root/compiler/tests/p2/rc_geteilt/$p.expected"
    if [ -f "$erwartet" ] && ! diff -u "$erwartet" "$out/$p.out" > "$out/$p.diff"; then
        echo "$p FEHLER: Ausgabe weicht ab" >&2; cat "$out/$p.diff" >&2
        fehler=$((fehler+1)); continue
    fi
    echo "$p PASS ohne Leck unter ASan/LSan"
done
exit $fehler
