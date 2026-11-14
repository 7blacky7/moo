#!/usr/bin/env bash
# P2-T73: Windows-Staticlib der Moo-Runtime auf Linux per MinGW bauen.
#
# Die Dateiliste kommt aus compiler/build.rs (alle .file("runtime/...c")), nicht
# aus einer zweiten Hand-Liste. Die Windows-Auswahl spiegelt build.rs fuer
# target_windows ohne UI/3D-Features: vendored win_regex, SChannel-TLS,
# Capture-Stubs statt Media Foundation (keine MF/COM-Importe in der Probe).
# Jede Quelle wird einzeln uebersetzt; was unter MinGW nicht uebersetzt, steht
# mit Grund in runtime_mingw.ausgelassen und fehlt im Archiv. Offene Symbole
# meldet erst der Link in hosted_probe.sh. Dieses Skript ist kein Windows-
# Beweis, nur die Grundlage fuer die hosted Probe.
set -euo pipefail
cd "$(dirname "$0")/../../.."

ausgabe="${1:-build/p2/windows_minipc/runtime}"
build_rs=compiler/build.rs
runtime=compiler/runtime
cc=x86_64-w64-mingw32-gcc
ar=x86_64-w64-mingw32-ar
nm=x86_64-w64-mingw32-nm
mkdir -p "$ausgabe/obj"

# Zweige, die build.rs nur per Feature/cfg baut und die eine fensterlose Probe
# nicht braucht (UI/Tray, Wayland/X11, 3D/GL/Vulkan/SDL, Linux-Capture,
# Media-Foundation-Capture, alternative TLS-/AES-Backends).
ausschluss='^(moo_ui_gtk|moo_tray_linux|moo_ui_win32|moo_tray_win32|moo_ui_wayland|wayland/.*|moo_ui_x11|moo_graphics|moo_test_api|moo_3d.*|moo_world|moo_voxel|moo_sprite|moo_hybrid|glad/.*|moo_capture_v4l2|moo_capture_alsa|moo_capture_pull|moo_capture_windows_system|moo_tls_mbedtls|moo_tls_openssl|moo_aes_native)\.c$'

# Defines wie build.rs (MOO_HAS_SURFACE immer; _CRT_* fuer target_windows);
# __USE_MINGW_ANSI_STDIO fuer C99-%lld/%zu-Formatierung unter MinGW.
flags=(-std=c11 -O2 -I"$runtime" -I"$runtime/win_regex"
       -DMOO_HAS_SURFACE -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE
       -D__USE_MINGW_ANSI_STDIO=1)

quellen="$(grep -oE '\.file\("runtime/[A-Za-z0-9_./-]+\.c"\)' "$build_rs" \
    | sed -E 's/\.file\("runtime\/(.*)"\)/\1/' | sort -u)"
[ -n "$quellen" ] || { echo "runtime_mingw: keine .file(\"runtime/*.c\") in $build_rs gefunden" >&2; exit 1; }

: > "$ausgabe/runtime_mingw.quellen"
: > "$ausgabe/runtime_mingw.ausgelassen"
objekte=()
while IFS= read -r q; do
    [ -n "$q" ] || continue
    if grep -qE "$ausschluss" <<<"$q"; then
        echo "$q  (Feature-/cfg-Zweig, nicht im Probe-Umfang)" >> "$ausgabe/runtime_mingw.ausgelassen"
        continue
    fi
    if [ ! -f "$runtime/$q" ]; then
        echo "$q  (Datei fehlt im Projekt)" >> "$ausgabe/runtime_mingw.ausgelassen"
        continue
    fi
    obj="$ausgabe/obj/$(tr '/' '_' <<<"${q%.c}").o"
    if "$cc" "${flags[@]}" -c "$runtime/$q" -o "$obj" 2> "$obj.log"; then
        objekte+=("$obj")
        echo "$q" >> "$ausgabe/runtime_mingw.quellen"
    else
        rm -f "$obj"
        echo "$q  (MinGW-Compilefehler: $(grep -m1 -E 'error' "$obj.log" | cut -c1-160))" >> "$ausgabe/runtime_mingw.ausgelassen"
    fi
done <<<"$quellen"

# Ohne Fehler-/Druckkern ist die Probe sinnlos: fail-closed.
for pflicht in moo_error.c moo_print.c moo_value.c moo_memory.c moo_string.c moo_stdlib.c; do
    grep -qx "$pflicht" "$ausgabe/runtime_mingw.quellen" \
        || { echo "runtime_mingw: Pflichtquelle $pflicht nicht uebersetzt" >&2; cat "$ausgabe/runtime_mingw.ausgelassen" >&2; exit 1; }
done

# P2-T75: fail-closed-Stub fuer moo_db_* (SQLite fehlt im MinGW-Sysroot) ins Archiv,
# damit `moo compile --target x86_64-pc-windows-gnu -o x.exe` ohne Zusatzobjekt
# linkt. Nur wenn moo_db.c nicht uebersetzt wurde (sonst doppelte Definitionen).
if ! grep -qx 'moo_db.c' "$ausgabe/runtime_mingw.quellen"; then
    "$cc" "${flags[@]}" -Wall -Wextra -Werror -c skripte/p2/windows_minipc/db_abwesend.c -o "$ausgabe/obj/db_abwesend.o"
    objekte+=("$ausgabe/obj/db_abwesend.o")
fi

archiv="$ausgabe/libmoo_runtime_win.a"
rm -f "$archiv"
"$ar" rcs "$archiv" "${objekte[@]}"
# Exportierte Definitionen des Archivs: hosted_probe.sh zieht damit die Liste
# der offenen Moo-Symbole VOR dem Link.
"$nm" --defined-only -g "$archiv" | awk 'NF >= 3 {print $3}' | sort -u > "$ausgabe/definiert.txt"

uebersetzt=$(wc -l < "$ausgabe/runtime_mingw.quellen")
ausgelassen=$(wc -l < "$ausgabe/runtime_mingw.ausgelassen")
echo "RUNTIME_MINGW_OK uebersetzt=$uebersetzt ausgelassen=$ausgelassen archiv=$(sha256sum "$archiv" | cut -c1-16)"
echo "  Auslassungen (ehrlich, siehe $ausgabe/runtime_mingw.ausgelassen):"
sed 's/^/    /' "$ausgabe/runtime_mingw.ausgelassen"
