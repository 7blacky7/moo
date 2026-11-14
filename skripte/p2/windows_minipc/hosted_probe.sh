#!/usr/bin/env bash
# P2-T73: Windows-hosted Moo-Programme (Moo-main + Moo-Runtime) auf Linux
# cross-bauen und auf dem echten N100 ausfuehren.
#
# Neu gegenueber test.sh: dort laufen nur extrahierte @einstieg-Exporte mit
# C-Harness (Trap-Vertrag). Hier laeuft erstmals der FANGBARE hosted Fehlerpfad
# unter Windows x64: moo_native_fehler -> versuche:/fange bzw. RC 1 ohne Handler.
# Weak-Hook-Beweis (codegen_native::native_fehler_hook): das Moo-Objekt traegt
# moo_native_fehler als SCHWACHE Trap-Definition (COFF 'W'); gewinnt beim Link
# die Runtime-Fassung (moo_error.c), faengt versuche: den Fehler und das
# Programm druckt 'gefangen ...'; gewaenne der Trap, endete die EXE mit
# 0xC000001D und RC != 0. Reihenfolge fail-closed: offene Symbole VOR dem Link,
# Link, SMB-Upload, WinRM-Lauf (Ausgabe per cmd-Umleitung und Base64 zurueck),
# cmp gegen compiler/tests/p2/*.expected (Windows-\r entfernt).
# Bestand wiederverwendet: smb_upload.py, minipc_winrm.py, Fixtures/Expected aus
# skripte/p2/zahlen/test.sh (Abschnitte 3, 4, 8, 11, 12).
# MOO_HOSTED_NUR_LINK=1: nur Linux-Teil (Objekte, Hook, Link), kein N100-Lauf.
set -euo pipefail
cd "$(dirname "$0")/../../.."

quelle=skripte/p2/windows_minipc
ausgabe=build/p2/windows_minipc/hosted
runtime_dir=build/p2/windows_minipc/runtime
compiler="${MOO_COMPILER:-build/p2-t48-51-cargo/release/moo-compiler}"
winrm=build/moo-winrm-client/bin/python
tests=compiler/tests/p2
remote='D:\MooTest'
mkdir -p "$ausgabe"

bash "$quelle/runtime_mingw.sh" "$runtime_dir"
archiv="$runtime_dir/libmoo_runtime_win.a"
# SQLite fehlt im MinGW-Sysroot (moo_db.c nicht im Archiv); der Release-Dispatch
# in moo_memory.c verlangt die moo_db_*-Symbole trotzdem: fail-closed Stub.
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -I compiler/runtime \
    -c "$quelle/db_abwesend.c" -o "$ausgabe/db_abwesend.o"

# Fixture -> COFF-Objekt (--target, main.rs: Cross = nur Objekt) -> EXE.
# hook=ja: die Fixture enthaelt fehlerfaehige native Operationen, das Objekt muss
# den schwachen moo_native_fehler-Hook tragen (Voraussetzung des Link-Beweises).
# Offene Symbole des Objekts, die das Archiv nicht liefert, stehen in
# <name>.offen_nach_runtime (erwartet: nur libc/Win32/CRT wie __main); ein
# Link-Fehler ist ein Befund mit genau dieser Liste, kein stiller Ausschluss.
baue() {  # name hook=ja|nein
    local name=$1 hook=$2 obj="$ausgabe/$1.obj" exe="$ausgabe/$1.exe"
    "$compiler" compile "$tests/$name.moos" --target x86_64-pc-windows-gnu -o "$obj"
    llvm-nm -u "$obj" | awk '{print $NF}' | sort -u > "$ausgabe/$name.offen"
    comm -23 "$ausgabe/$name.offen" "$runtime_dir/definiert.txt" > "$ausgabe/$name.offen_nach_runtime"
    llvm-nm "$obj" | grep -E ' [WwVv] .*moo_native_fehler' > "$ausgabe/$name.hook" || true
    if [ "$hook" = ja ] && [ ! -s "$ausgabe/$name.hook" ]; then
        echo "$name: Objekt ohne schwachen moo_native_fehler-Hook (erwartet COFF 'W')" >&2
        llvm-nm "$obj" | grep moo_native_fehler >&2 || true
        exit 1
    fi
    # -static: keine libwinpthread-1.dll auf dem Testrechner (vgl. Mat4 in test.sh).
    # System-Libs wie build.rs/main.rs fuer target_windows: ws2_32 bcrypt crypt32 secur32.
    if ! x86_64-w64-mingw32-gcc "$obj" "$ausgabe/db_abwesend.o" "$archiv" -static -static-libgcc \
            -lws2_32 -lbcrypt -lcrypt32 -lsecur32 -o "$exe" 2> "$ausgabe/$name.link.log"; then
        echo "LINK-FEHLER $name. Offene Moo-Symbole ohne Runtime-Definition:" >&2
        cat "$ausgabe/$name.offen_nach_runtime" >&2
        grep -E 'undefined reference' "$ausgabe/$name.link.log" | sed -E 's/.*undefined reference to//' | sort -u | head -40 >&2
        exit 1
    fi
    if x86_64-w64-mingw32-objdump -p "$exe" | grep -qi 'DLL Name: libwinpthread-1.dll'; then
        echo "$name: libwinpthread darf nicht dynamisch benoetigt werden" >&2; exit 1
    fi
    echo "LINK_OK $name hook=$(test -s "$ausgabe/$name.hook" && echo W || echo -) offen_ohne_runtime=$(tr '\n' ' ' < "$ausgabe/$name.offen_nach_runtime")"
}

# EXE hochladen, auf Windows per cmd mit Datei-Umleitung starten (CRT-Textmodus
# schreibt \r\n), RC sowie stdout/stderr Base64-kodiert zurueckholen. Ergebnis:
# <name>.win.rc, <name>.win.out, <name>.win.err (\r entfernt).
laufe() {
    local name=$1
    "$winrm" "$quelle/smb_upload.py" "$ausgabe/$name.exe"
    local exe="$remote\\$name.exe" out="$remote\\$name.out" err="$remote\\$name.err"
    local ps="\$ErrorActionPreference='Continue'; cmd /c \"$exe > $out 2> $err\"; \$rc = \$LASTEXITCODE; Write-Output ('MOO_HOSTED_RC=' + \$rc); Write-Output ('MOO_HOSTED_OUT=' + [Convert]::ToBase64String([IO.File]::ReadAllBytes('$out'))); Write-Output ('MOO_HOSTED_ERR=' + [Convert]::ToBase64String([IO.File]::ReadAllBytes('$err')))"
    "$winrm" skripte/p2/minipc_winrm.py run "$ps" > "$ausgabe/$name.winrm"
    sed -n 's/^MOO_HOSTED_RC=//p' "$ausgabe/$name.winrm" | tr -d '\r' > "$ausgabe/$name.win.rc"
    sed -n 's/^MOO_HOSTED_OUT=//p' "$ausgabe/$name.winrm" | tr -d '\r' | base64 -d | tr -d '\r' > "$ausgabe/$name.win.out"
    sed -n 's/^MOO_HOSTED_ERR=//p' "$ausgabe/$name.winrm" | tr -d '\r' | base64 -d | tr -d '\r' > "$ausgabe/$name.win.err"
    [ -s "$ausgabe/$name.win.rc" ] || { echo "$name: kein MOO_HOSTED_RC vom N100" >&2; cat "$ausgabe/$name.winrm" >&2; exit 1; }
}

rc_pruefen() {  # name erwartet
    local rc; rc="$(cat "$ausgabe/$1.win.rc")"
    [ "$rc" = "$2" ] || { echo "$1: Windows-RC $rc statt $2" >&2; cat "$ausgabe/$1.win.err" >&2; exit 1; }
    if grep -q 'NACH_FALSCH' "$ausgabe/$1.win.out"; then echo "$1: Code nach dem Fehler lief auf Windows" >&2; exit 1; fi
}

# Linux-Teil: alle Objekte und EXEs zuerst, damit ein Link-Befund vor dem
# ersten N100-Zugriff sichtbar wird.
baue native_zahlen_fehler ja
baue native_cast_fehler ja
baue native_bool_arg nein   # bool-Argumentpruefung laeuft im dyn-Adapter ueber moo_throw, keine native Fehleroperation
baue native_zahlen_unbehandelt ja
baue native_system_fehler ja
if [ "${MOO_HOSTED_NUR_LINK:-0}" = 1 ]; then
    echo "PASS Linux-Teil P2-T73 hosted: 5 Windows-EXEs gelinkt (Runtime-Archiv + db_abwesend), kein N100-Lauf (MOO_HOSTED_NUR_LINK=1)"
    exit 0
fi

# 1. Fangbare Fehler in versuche: (Division/MIN/Rest/Shift, Casts, bool-Argument):
#    Ausgabe byteidentisch zu Linux (.expected), RC 0. zahlen/cast belegen
#    zugleich, dass die Runtime-Fassung von moo_native_fehler den schwachen
#    Trap-Hook im COFF-Link geschlagen hat.
for name in native_zahlen_fehler native_cast_fehler native_bool_arg; do
    laufe "$name"
    rc_pruefen "$name" 0
    cmp "$tests/$name.expected" "$ausgabe/$name.win.out"
done
echo "PASS Windows hosted: versuche: faengt native Zahlen-, Cast- und bool-Fehler (3 Fixtures byteidentisch zu Linux)"

# 2. Unbehandelt: moo_throw beendet den Prozess mit RC 1 und Diagnose, nichts danach.
laufe native_zahlen_unbehandelt
rc_pruefen native_zahlen_unbehandelt 1
grep -qx 'vorher' "$ausgabe/native_zahlen_unbehandelt.win.out"
grep -qF "Native Division durch Null oder MIN/-1 in 'z_div'" \
    "$ausgabe/native_zahlen_unbehandelt.win.err" "$ausgabe/native_zahlen_unbehandelt.win.out"
echo "PASS Windows hosted: unbehandelter Zahlenfehler RC 1, Diagnose, kein Fortlauf"

# 3. Hosted Systemkoerper (unsicher:): sys.fehler ruft moo_native_fehler statt zu
#    trappen; ohne Handler RC 1 mit Diagnose.
laufe native_system_fehler
rc_pruefen native_system_fehler 1
grep -qx '4' "$ausgabe/native_system_fehler.win.out"
grep -qF "Nativer Laufzeitfehler (Division, Shift, Index oder Konversion) in 's_div'" \
    "$ausgabe/native_system_fehler.win.err" "$ausgabe/native_system_fehler.win.out"
echo "PASS Windows hosted: Systemkoerper-Fehler ueber moo_native_fehler, RC 1, kein Fortlauf"

echo "PASS P2-T73 hosted Windows x64 (N100): fangbarer Fehlerpfad + RC-1-Vertrag; Runtime-Fassung von moo_native_fehler gewinnt ueber den schwachen COFF-Trap-Hook"
