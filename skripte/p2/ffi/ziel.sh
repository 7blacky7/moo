#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-ffi-ziel
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
timeout 30s "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -c compiler/tests/p2/ffi_schmal.c -o "$out/ffi_schmal.o"
ar rcs "$out/libp2_ffi_schmal.a" "$out/ffi_schmal.o"
timeout 120s "$compiler" compile compiler/tests/p2/bibliothek_ziel_alle.moos --lib-pfad "$out" -o "$out/probe"
[[ $(timeout 30s "$out/probe") == "-745" ]] || { echo "Zielauswahl: falsche Ausgabe" >&2; exit 1; }
failure() {
    local name="$1" target="$2" expected="$3" rc
    set +e
    timeout 30s "$compiler" compile "compiler/tests/p2/bibliothek_ziel_${name}.moos" --target "$target" --emit-ir -o "$out/${name}.ll" > "$out/${name}.log" 2>&1
    rc=$?
    set -e
    if (( rc == 0 || rc == 124 || rc >= 128 )) || ! rg -F -q "$expected" "$out/${name}.log"; then
        printf '%s / %s: Exit %s oder Diagnose unpassend\n' "$name" "$target" "$rc" >&2
        exit 1
    fi
}
failure doppelt native "Ziel 'linux' doppelt"
failure unbekannt native "unbekanntes Ziel 'freebsd'"
failure gemischt native "Argumente nicht mischen"
failure linux windows "kein Name fuer Ziel 'windows'"
failure linux x86_64-apple-darwin23 "kein Name fuer Ziel 'macos'"
set +e
timeout 120s "$compiler" compile compiler/tests/p2/bibliothek_fehlt.moos --lib-pfad "$out" -o "$out/fehlt" >"$out/fehlt.log" 2>&1
rc=$?
set -e
if (( rc == 0 || rc == 124 || rc >= 128 )) || ! rg -F -q 'p2_bibliothek_absichtlich_fehlt_7f3e' "$out/fehlt.log"; then
    printf 'Fehlende Bibliothek: Exit %s oder Diagnose unpassend\n' "$rc" >&2
    exit 1
fi
printf 'Bibliotheks-Zielauswahl und Fehlersuche: PASS\n'
