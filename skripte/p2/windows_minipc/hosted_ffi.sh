#!/usr/bin/env bash
# P2-T43: komplettes Moo-Programm (dynamisches main + Moo-Runtime) ruft auf dem
# echten N100 eine eigene statische Bibliothek und eine eigene DLL auf
# (compiler/tests/p2/ffi_windows_bibliothek.moos). Bisher belegt waren nur
# @einstieg-Exporte mit C-Harness (test.sh, Batches 33000/33006).
# Bestand wiederverwendet: runtime_mingw.sh + db_abwesend.c (P2-T73, Moo-Runtime
# als MinGW-Archiv), smb_upload.py, minipc_winrm.py; Laufmuster wie hosted_probe.sh
# (cmd-Umleitung, RC/stdout/stderr Base64 zurueck). GNU ld liest keine
# .drectve-Bibliotheken, daher -L/-l hier im Treiber (vgl. Thought 0ea80906).
# MOO_HOSTED_NUR_LINK=1: nur Linux-Teil, kein N100-Lauf.
set -euo pipefail
cd "$(dirname "$0")/../../.."

quelle=skripte/p2/windows_minipc
ausgabe=build/p2/windows_minipc/hosted_ffi
runtime_dir=build/p2/windows_minipc/runtime
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
winrm=build/moo-winrm-client/bin/python
tests=compiler/tests/p2
name=ffi_windows_bibliothek
remote='D:\MooTest'
mkdir -p "$ausgabe"

bash "$quelle/runtime_mingw.sh" "$runtime_dir"
archiv="$runtime_dir/libmoo_runtime_win.a"

# Eigene Bibliotheken. Die DLL-Importbibliothek heisst libp2_win_dll.a, damit
# ld sie auch unter -static (keine libwinpthread-DLL) findet; der Import bleibt dynamisch.
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -c "$quelle/bibliothek_statisch.c" -o "$ausgabe/statisch.o"
rm -f "$ausgabe/libp2_win_statisch.a"
x86_64-w64-mingw32-ar rcs "$ausgabe/libp2_win_statisch.a" "$ausgabe/statisch.o"
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -shared -static-libgcc \
    "$quelle/bibliothek_dll.c" -o "$ausgabe/p2_win_dll.dll" -Wl,--out-implib,"$ausgabe/libp2_win_dll.a"

# P2-T75: Produktweg. Der Compiler linkt die EXE selbst (MinGW, Runtime-Archiv,
# @bibliothek-Libs aus der Quelle, Suchpfad per --lib-pfad).
MOO_RUNTIME_LIB_WINDOWS_GNU="$PWD/$archiv" "$compiler" compile "$tests/$name.moos" \
    --target x86_64-pc-windows-gnu --lib-pfad "$ausgabe" -o "$ausgabe/$name.exe"
x86_64-w64-mingw32-objdump -p "$ausgabe/$name.exe" > "$ausgabe/$name.imports"
grep -qi 'DLL Name: p2_win_dll.dll' "$ausgabe/$name.imports" || { echo 'EXE importiert p2_win_dll.dll nicht' >&2; exit 1; }
if grep -qiE 'DLL Name: (p2_win_statisch|libwinpthread-1\.dll)' "$ausgabe/$name.imports"; then
    echo 'Unerwarteter DLL-Import (statische Bibliothek oder libwinpthread)' >&2; exit 1
fi
echo "LINK_OK $name: Moo-main + Runtime-Archiv + statische Bibliothek + DLL-Import"
# P2-T75 Diagnosen: fehlendes Runtime-Archiv und .exe fuer MSVC sind klare Fehler.
if MOO_RUNTIME_LIB_WINDOWS_GNU="$ausgabe/fehlt.a" "$compiler" compile "$tests/$name.moos" \
        --target x86_64-pc-windows-gnu --lib-pfad "$ausgabe" -o "$ausgabe/fehlt.exe" > "$ausgabe/fehlt.log" 2>&1; then
    echo 'Link ohne Runtime-Archiv wurde nicht abgelehnt' >&2; exit 1
fi
grep -qF 'MOO_RUNTIME_LIB_WINDOWS_GNU zeigt auf keine Datei' "$ausgabe/fehlt.log" || { cat "$ausgabe/fehlt.log" >&2; exit 1; }
if "$compiler" compile "$tests/$name.moos" --target x86_64-pc-windows-msvc -o "$ausgabe/msvc.exe" > "$ausgabe/msvc.log" 2>&1; then
    echo 'MSVC-.exe aus Cross-Build wurde nicht abgelehnt' >&2; exit 1
fi
grep -qF 'nur fuer x86_64-pc-windows-gnu (MinGW) gelinkt' "$ausgabe/msvc.log" || { cat "$ausgabe/msvc.log" >&2; exit 1; }
echo "PASS P2-T75 Diagnosen: fehlendes Runtime-Archiv, MSVC-.exe"
# P2-T76: ohne Override nutzt der Compiler das beim eigenen Build erzeugte Archiv.
env -u MOO_RUNTIME_LIB_WINDOWS_GNU "$compiler" compile "$tests/$name.moos" \
    --target x86_64-pc-windows-gnu --lib-pfad "$ausgabe" -o "$ausgabe/${name}_eingebaut.exe" > "$ausgabe/eingebaut.log" 2>&1 \
    || { cat "$ausgabe/eingebaut.log" >&2; echo 'Link mit build.rs-Archiv fehlgeschlagen (MinGW beim Compiler-Build vorhanden?)' >&2; exit 1; }
# Ueber Datei statt Pipe: grep -q schliesst die Pipe frueh, objdump bekaeme SIGPIPE (RC141 unter pipefail).
x86_64-w64-mingw32-objdump -p "$ausgabe/${name}_eingebaut.exe" > "$ausgabe/${name}_eingebaut.imports"
grep -qi 'DLL Name: p2_win_dll.dll' "$ausgabe/${name}_eingebaut.imports" || { echo 'Eingebaut-EXE importiert p2_win_dll.dll nicht' >&2; exit 1; }
echo "PASS P2-T76: EXE mit beim Compiler-Build erzeugtem Windows-Runtime-Archiv gelinkt"
# P2-T79: DLL direkt aus Moo (native_probe.moos, 4x @einstieg); native_probe.c ist der Client.
dll_name=moo_win_probe
"$compiler" compile "$quelle/native_probe.moos" --target x86_64-pc-windows-gnu -o "$ausgabe/$dll_name.dll"
[ -f "$ausgabe/lib$dll_name.a" ] || { echo "Importbibliothek lib$dll_name.a fehlt" >&2; exit 1; }
x86_64-w64-mingw32-objdump -p "$ausgabe/$dll_name.dll" > "$ausgabe/$dll_name.exports"
# binutils >= 2.37: Eintraege '[   0] +base[   1]  0000 name' -> letztes Feld.
awk '/\[Ordinal\/Name Pointer\] Table/{t=1;next} t&&/^[[:space:]]*$/{t=0} t&&/^[[:space:]]*\[ *[0-9]+\]/{print $NF}' \
    "$ausgabe/$dll_name.exports" | sort > "$ausgabe/$dll_name.exportnamen"
printf '%s\n' moo_win_api_pid moo_win_kette moo_win_plus42 moo_win_quadrat > "$ausgabe/$dll_name.exportnamen.erwartet"
cmp "$ausgabe/$dll_name.exportnamen.erwartet" "$ausgabe/$dll_name.exportnamen" \
    || { echo 'DLL-Exporte weichen ab (nur @einstieg erwartet):' >&2; cat "$ausgabe/$dll_name.exportnamen" >&2; exit 1; }
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -O2 -static-libgcc "$quelle/native_probe.c" \
    -L "$ausgabe" -l"$dll_name" -o "$ausgabe/${dll_name}_client.exe"
x86_64-w64-mingw32-objdump -p "$ausgabe/${dll_name}_client.exe" > "$ausgabe/${dll_name}_client.imports"
grep -qi "DLL Name: $dll_name.dll" "$ausgabe/${dll_name}_client.imports" || { echo "Client importiert $dll_name.dll nicht" >&2; exit 1; }
if "$compiler" compile "$tests/$name.moos" --target x86_64-pc-windows-gnu --lib-pfad "$ausgabe" \
        -o "$ausgabe/ohne_export.dll" > "$ausgabe/ohne_export.log" 2>&1; then
    echo 'DLL ohne @einstieg-Export wurde nicht abgelehnt' >&2; exit 1
fi
grep -qF 'mindestens eine exportierte Funktion' "$ausgabe/ohne_export.log" || { cat "$ausgabe/ohne_export.log" >&2; exit 1; }
echo "PASS P2-T79 Linux-Teil: Moo-DLL mit genau 4 @einstieg-Exporten, Importbibliothek, C-Client gelinkt, DLL ohne Export abgelehnt"
# P2-T81: zweites Moo-Programm importiert die erzeugte Schnittstelle (--lib-pfad) und linkt gegen die Moo-DLL.
verbraucher=ffi_moo_dll_verbraucher
schnittstelle="$ausgabe/${dll_name}_schnittstelle.moos"
head -1 "$schnittstelle" | grep -qF 'moo-schnittstelle' || { echo "Schnittstelle $schnittstelle fehlt oder ist nicht erzeugt" >&2; exit 1; }
[ "$(grep -c '^extern "C" funktion ' "$schnittstelle")" = 4 ] || { cat "$schnittstelle" >&2; echo 'Schnittstelle ohne genau 4 extern-Deklarationen' >&2; exit 1; }
"$compiler" compile "$tests/$verbraucher.moos" --target x86_64-pc-windows-gnu --lib-pfad "$ausgabe" -o "$ausgabe/$verbraucher.exe"
x86_64-w64-mingw32-objdump -p "$ausgabe/$verbraucher.exe" > "$ausgabe/$verbraucher.imports"
grep -qi "DLL Name: $dll_name.dll" "$ausgabe/$verbraucher.imports" || { echo "Moo-Verbraucher importiert $dll_name.dll nicht" >&2; exit 1; }
echo "PASS P2-T81 Linux-Teil: Moo-Verbraucher importiert erzeugte Schnittstelle und linkt gegen die Moo-DLL"
if [ "${MOO_HOSTED_NUR_LINK:-0}" = 1 ]; then
    echo "PASS Linux-Teil P2-T43 hosted FFI (kein N100-Lauf, MOO_HOSTED_NUR_LINK=1)"
    exit 0
fi

"$winrm" "$quelle/smb_upload.py" "$ausgabe/p2_win_dll.dll"
"$winrm" "$quelle/smb_upload.py" "$ausgabe/$name.exe"
exe="$remote\\$name.exe" out="$remote\\$name.out" err="$remote\\$name.err"
ps="\$ErrorActionPreference='Continue'; cmd /c \"$exe > $out 2> $err\"; \$rc = \$LASTEXITCODE; Write-Output ('MOO_HOSTED_RC=' + \$rc); Write-Output ('MOO_HOSTED_OUT=' + [Convert]::ToBase64String([IO.File]::ReadAllBytes('$out'))); Write-Output ('MOO_HOSTED_ERR=' + [Convert]::ToBase64String([IO.File]::ReadAllBytes('$err')))"
"$winrm" skripte/p2/minipc_winrm.py run "$ps" > "$ausgabe/$name.winrm"
rc="$(sed -n 's/^MOO_HOSTED_RC=//p' "$ausgabe/$name.winrm" | tr -d '\r')"
sed -n 's/^MOO_HOSTED_OUT=//p' "$ausgabe/$name.winrm" | tr -d '\r' | base64 -d | tr -d '\r' > "$ausgabe/$name.win.out"
sed -n 's/^MOO_HOSTED_ERR=//p' "$ausgabe/$name.winrm" | tr -d '\r' | base64 -d | tr -d '\r' > "$ausgabe/$name.win.err"
[ "$rc" = 0 ] || { echo "$name: Windows-RC '$rc' statt 0" >&2; cat "$ausgabe/$name.win.err" "$ausgabe/$name.winrm" >&2; exit 1; }
cmp "$tests/$name.expected" "$ausgabe/$name.win.out"
echo "PASS P2-T43 hosted Windows x64 (N100): Moo-main ruft eigene statische Bibliothek und eigene DLL (Ausgabe byteidentisch zu .expected)"

# P2-T79: C-Client laedt die aus Moo gebaute DLL auf dem N100.
"$winrm" "$quelle/smb_upload.py" "$ausgabe/$dll_name.dll"
"$winrm" "$quelle/smb_upload.py" "$ausgabe/${dll_name}_client.exe"
"$winrm" skripte/p2/minipc_winrm.py run "\$ErrorActionPreference='Stop'; \$a = (& '$remote\\${dll_name}_client.exe' | Out-String); if (\$LASTEXITCODE -ne 0) { throw 'Moo-DLL-Client fehlgeschlagen' }; if (\$a -notmatch 'MOO_WIN64_NATIVE_OK: 7/7') { throw 'Moo-DLL-Client ohne OK-Marke' }; Write-Output \$a" > "$ausgabe/${dll_name}_client.winrm"
grep -q 'MOO_WIN64_NATIVE_OK: 7/7' "$ausgabe/${dll_name}_client.winrm" || { cat "$ausgabe/${dll_name}_client.winrm" >&2; exit 1; }
echo "PASS P2-T79 hosted Windows x64 (N100): C-Client ruft 4 Exporte der aus Moo gebauten DLL (7/7)"

# P2-T81: Moo-EXE ruft Moo-DLL (DLL liegt seit P2-T79 in D:\MooTest).
"$winrm" "$quelle/smb_upload.py" "$ausgabe/$verbraucher.exe"
exe="$remote\\$verbraucher.exe" out="$remote\\$verbraucher.out" err="$remote\\$verbraucher.err"
ps="\$ErrorActionPreference='Continue'; cmd /c \"$exe > $out 2> $err\"; \$rc = \$LASTEXITCODE; Write-Output ('MOO_HOSTED_RC=' + \$rc); Write-Output ('MOO_HOSTED_OUT=' + [Convert]::ToBase64String([IO.File]::ReadAllBytes('$out'))); Write-Output ('MOO_HOSTED_ERR=' + [Convert]::ToBase64String([IO.File]::ReadAllBytes('$err')))"
"$winrm" skripte/p2/minipc_winrm.py run "$ps" > "$ausgabe/$verbraucher.winrm"
rc="$(sed -n 's/^MOO_HOSTED_RC=//p' "$ausgabe/$verbraucher.winrm" | tr -d '\r')"
sed -n 's/^MOO_HOSTED_OUT=//p' "$ausgabe/$verbraucher.winrm" | tr -d '\r' | base64 -d | tr -d '\r' > "$ausgabe/$verbraucher.win.out"
[ "$rc" = 0 ] || { echo "$verbraucher: Windows-RC '$rc' statt 0" >&2; cat "$ausgabe/$verbraucher.winrm" >&2; exit 1; }
cmp "$tests/$verbraucher.expected" "$ausgabe/$verbraucher.win.out"
echo "PASS P2-T81 hosted Windows x64 (N100): Moo-EXE ruft Moo-DLL ueber erzeugte Schnittstelle (Ausgabe byteidentisch zu .expected)"
