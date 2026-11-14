#!/usr/bin/env bash
# Native Moo-x64-Objekte auf Linux bauen, direkt zu Windows kopieren und ausfuehren.
set -euo pipefail
cd "$(dirname "$0")/../../.."

quelle=skripte/p2/windows_minipc
ausgabe=build/p2/windows_minipc
compiler="${MOO_COMPILER:-build/p2-t48-51-cargo/release/moo-compiler}"
winrm=build/moo-winrm-client/bin/python
mkdir -p "$ausgabe"

"$compiler" compile "$quelle/native_probe.moos" --target x86_64-pc-windows-gnu --emit-ir -o "$ausgabe/moo.ll"
opt -passes=verify -disable-output "$ausgabe/moo.ll"
llvm-extract -S --func=moo_win_plus42 --func=moo_win_quadrat --func=moo_win_kette --func=moo_win_api_pid --recursive "$ausgabe/moo.ll" -o "$ausgabe/moo.extract.ll"
llc -O2 -filetype=obj "$ausgabe/moo.extract.ll" -o "$ausgabe/moo.obj"
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -O2 "$quelle/native_probe.c" "$ausgabe/moo.obj" -o "$ausgabe/moo_win_probe.exe"

# SMB ueber das direkte Testkabel: schnelle Artefakte statt vieler WinRM-Base64-Calls.
# Das WinRM-Skript bleibt ausschliesslich fuer Kommandos und Hash-Pruefung.
"$winrm" skripte/p2/windows_minipc/smb_upload.py "$ausgabe/moo_win_probe.exe"
"$winrm" skripte/p2/minipc_winrm.py run "\$ErrorActionPreference='Stop'; & 'D:\\MooTest\\moo_win_probe.exe'; if (\$LASTEXITCODE -ne 0) { throw 'Moo-Windows-Gate fehlgeschlagen' }"

# Die P2-T56-C-ABI-Referenz aus dem Bestand direkt auf Windows ausfuehren.
# C-only: nicht als Moo-Struct-by-value-Windows-Beweis ausgeben.
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror \
    skripte/p2/abi/struct_by_value.c skripte/p2/abi/struct_by_value_test.c \
    -o "$ausgabe/moo_win_abi_reference.exe"
"$winrm" skripte/p2/windows_minipc/smb_upload.py "$ausgabe/moo_win_abi_reference.exe"
"$winrm" skripte/p2/minipc_winrm.py run "\$ErrorActionPreference='Stop'; & 'D:\\MooTest\\moo_win_abi_reference.exe'; if (\$LASTEXITCODE -ne 0) { throw 'Windows-C-ABI-Referenz fehlgeschlagen' }"
echo "PASS native Moo Windows x64 + separate Windows C-ABI-Referenz"

# P2-T73: Zahlen-/Cast-/bool-Semantik (P2-T34) auf echtem Windows x64 sowie der
# Fehlervertrag der C-Exporte: @einstieg trappt (ADR §11), kein moo_native_fehler
# und kein offenes Symbol im Objekt (runtime-frei linkbar, ADR §13).
"$compiler" compile "$quelle/zahlen_probe.moos" --target x86_64-pc-windows-gnu --emit-ir -o "$ausgabe/zahlen.ll"
opt -passes=verify -disable-output "$ausgabe/zahlen.ll"
zahlen_funktionen="moo_win_div moo_win_rest moo_win_links moo_win_rechts moo_win_wrap8 moo_win_als_u8
moo_win_wrap_u8 moo_win_satt_i8 moo_win_satt_f moo_win_f2i moo_win_rund moo_win_kleiner moo_win_sicher moo_win_nan"
zahlen_auswahl=""
for funktion in $zahlen_funktionen; do zahlen_auswahl="$zahlen_auswahl --func=$funktion"; done
# Absichtlich unquotiert: die Optionsliste wird in einzelne --func Argumente zerlegt.
llvm-extract -S --recursive --keep-const-init $zahlen_auswahl "$ausgabe/zahlen.ll" -o "$ausgabe/zahlen.extract.ll"
if grep -q 'moo_native_fehler' "$ausgabe/zahlen.extract.ll"; then echo 'Win64-Export referenziert moo_native_fehler statt zu trappen' >&2; exit 1; fi
grep -q 'llvm.trap' "$ausgabe/zahlen.extract.ll" || { echo 'Win64-Export ohne Trap-Fehlerpfad' >&2; exit 1; }
llc -O2 -filetype=obj "$ausgabe/zahlen.extract.ll" -o "$ausgabe/zahlen.obj"
zahlen_offen="$(llvm-nm -u "$ausgabe/zahlen.obj" | awk '{print $NF}' || true)"
[ -z "$zahlen_offen" ] || { echo "Win64-Zahlenobjekt mit offenen Symbolen: $zahlen_offen" >&2; exit 1; }
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -O2 -static-libgcc "$quelle/zahlen_probe.c" "$ausgabe/zahlen.obj" -o "$ausgabe/moo_win_zahlen.exe"
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -O2 -static-libgcc "$quelle/zahlen_trap.c" "$ausgabe/zahlen.obj" -o "$ausgabe/moo_win_zahlen_trap.exe"
"$winrm" skripte/p2/windows_minipc/smb_upload.py "$ausgabe/moo_win_zahlen.exe"
"$winrm" skripte/p2/windows_minipc/smb_upload.py "$ausgabe/moo_win_zahlen_trap.exe"
"$winrm" skripte/p2/minipc_winrm.py run "\$ErrorActionPreference='Stop'; & 'D:\\MooTest\\moo_win_zahlen.exe'; if (\$LASTEXITCODE -ne 0) { throw 'Moo-Win64-Zahlen/Casts/bool fehlgeschlagen' }"
# Trap-Vertrag: Exitcode != 0 (STATUS_ILLEGAL_INSTRUCTION 0xC000001D), kein Fortlauf nach dem Fehler.
"$winrm" skripte/p2/minipc_winrm.py run "\$ErrorActionPreference='Continue'; \$ausgabe = (& 'D:\\MooTest\\moo_win_zahlen_trap.exe' 2>&1 | Out-String); \$rc = \$LASTEXITCODE; if (\$rc -eq 0) { throw 'Moo-Win64-Trap: Exitcode 0, Export hat nicht getrappt' }; if (\$ausgabe -match 'NICHT ERREICHT') { throw 'Moo-Win64-Trap: Code nach dem Fehler lief' }; Write-Output ('MOO_WIN64_TRAP_OK exit=' + \$rc)"
echo "PASS Moo Windows x64: native Zahlen/Casts/bool (20 Faelle, C-Harness) + Trap-Vertrag der C-Exporte"

# P2-T43: eigene statische Bibliothek + eigene DLL per @bibliothek aus nativem
# Moo. GNU-COFF: GNU ld ignoriert .drectve-Bibliotheken, daher -L/-l hier im
# Treiber (wie main.rs fuer native Links); MSVC-/DEFAULTLIB prueft cross-windows.
"$compiler" compile "$quelle/bibliothek_probe.moos" --target x86_64-pc-windows-gnu --emit-ir -o "$ausgabe/bib.ll"
opt -passes=verify -disable-output "$ausgabe/bib.ll"
llvm-extract -S --func=moo_win_lib_statisch --func=moo_win_lib_dll --recursive "$ausgabe/bib.ll" -o "$ausgabe/bib.extract.ll"
llc -O2 -filetype=obj "$ausgabe/bib.extract.ll" -o "$ausgabe/bib.obj"
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -O2 -c "$quelle/bibliothek_statisch.c" -o "$ausgabe/bib_statisch.o"
rm -f "$ausgabe/libp2_win_statisch.a"
x86_64-w64-mingw32-ar rcs "$ausgabe/libp2_win_statisch.a" "$ausgabe/bib_statisch.o"
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -O2 -shared -static-libgcc \
    "$quelle/bibliothek_dll.c" -o "$ausgabe/p2_win_dll.dll" -Wl,--out-implib,"$ausgabe/libp2_win_dll.dll.a"
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -O2 -static-libgcc "$quelle/bibliothek_probe.c" \
    "$ausgabe/bib.obj" -L "$ausgabe" -lp2_win_statisch -lp2_win_dll -o "$ausgabe/moo_win_bibliothek.exe"
x86_64-w64-mingw32-objdump -p "$ausgabe/moo_win_bibliothek.exe" > "$ausgabe/bib.imports"
grep -qi 'DLL Name: p2_win_dll.dll' "$ausgabe/bib.imports" || { echo 'EXE importiert p2_win_dll.dll nicht' >&2; exit 1; }
if grep -qi 'p2_win_statisch' "$ausgabe/bib.imports"; then echo 'Statische Bibliothek wurde als DLL importiert' >&2; exit 1; fi
"$winrm" skripte/p2/windows_minipc/smb_upload.py "$ausgabe/p2_win_dll.dll"
"$winrm" skripte/p2/windows_minipc/smb_upload.py "$ausgabe/moo_win_bibliothek.exe"
"$winrm" skripte/p2/minipc_winrm.py run "\$ErrorActionPreference='Stop'; & 'D:\\MooTest\\moo_win_bibliothek.exe'; if (\$LASTEXITCODE -ne 0) { throw 'Moo-Windows-Bibliotheken fehlgeschlagen' }"
echo "PASS Moo Windows x64: eigene statische Bibliothek + eigene DLL aus nativem Moo"

# P2-T43: dieselbe Quelle als MSVC-COFF. Die Bibliotheken findet lld-link NUR
# ueber die eingebettete .drectve /DEFAULTLIB des Moo-Objekts (kein -l); der
# CRT-freie Einstieg linkt nur kernel32 (Importlib aus kernel32.def).
msvc=x86_64-pc-windows-msvc
"$compiler" compile "$quelle/bibliothek_probe.moos" --target "$msvc" --emit-ir -o "$ausgabe/bib-msvc.ll"
opt -passes=verify -disable-output "$ausgabe/bib-msvc.ll"
llvm-extract -S --func=moo_win_lib_statisch --func=moo_win_lib_dll --recursive "$ausgabe/bib-msvc.ll" -o "$ausgabe/bib-msvc.extract.ll"
llc -O2 -filetype=obj "$ausgabe/bib-msvc.extract.ll" -o "$ausgabe/bib-msvc.obj"
llvm-readobj --coff-directives "$ausgabe/bib-msvc.obj" > "$ausgabe/bib-msvc.directives"
grep -Fq '/DEFAULTLIB:p2_win_statisch.lib' "$ausgabe/bib-msvc.directives"
grep -Fq '/DEFAULTLIB:p2_win_dll.lib' "$ausgabe/bib-msvc.directives"
msvc_dir="$ausgabe/msvc"
rm -rf "$msvc_dir"; mkdir -p "$msvc_dir"
llvm-dlltool -m i386:x86-64 -d "$quelle/kernel32.def" -l "$msvc_dir/kernel32.lib"
clang --target="$msvc" -O2 -Wall -Wextra -Werror -c "$quelle/bibliothek_statisch.c" -o "$msvc_dir/statisch.obj"
llvm-lib /out:"$msvc_dir/p2_win_statisch.lib" "$msvc_dir/statisch.obj"
clang --target="$msvc" -O2 -Wall -Wextra -Werror -c "$quelle/bibliothek_dll.c" -o "$msvc_dir/dll.obj"
lld-link /dll /noentry /nodefaultlib /out:"$msvc_dir/p2_win_dll.dll" /implib:"$msvc_dir/p2_win_dll.lib" "$msvc_dir/dll.obj"
clang --target="$msvc" -O2 -ffreestanding -fno-builtin -fno-stack-protector -Wall -Wextra -Werror \
    -c "$quelle/bibliothek_probe_msvc.c" -o "$msvc_dir/harness.obj"
lld-link /entry:moo_msvc_start /subsystem:console /nodefaultlib:libcmt /nodefaultlib:oldnames \
    /libpath:"$msvc_dir" "$ausgabe/bib-msvc.obj" "$msvc_dir/harness.obj" "$msvc_dir/kernel32.lib" \
    /out:"$ausgabe/moo_win_bib_msvc.exe"
llvm-readobj --coff-imports "$ausgabe/moo_win_bib_msvc.exe" > "$ausgabe/bib-msvc.imports"
grep -Fq 'Name: p2_win_dll.dll' "$ausgabe/bib-msvc.imports" || { echo 'MSVC-EXE importiert p2_win_dll.dll nicht' >&2; exit 1; }
# Gleicher DLL-Name wie im GNU-Abschnitt: die MSVC-DLL ersetzt sie vor diesem Lauf.
"$winrm" skripte/p2/windows_minipc/smb_upload.py "$msvc_dir/p2_win_dll.dll"
"$winrm" skripte/p2/windows_minipc/smb_upload.py "$ausgabe/moo_win_bib_msvc.exe"
"$winrm" skripte/p2/minipc_winrm.py run "\$ErrorActionPreference='Stop'; & 'D:\\MooTest\\moo_win_bib_msvc.exe'; if (\$LASTEXITCODE -ne 0) { throw 'Moo-MSVC-Bibliotheken fehlgeschlagen' }"
echo "PASS Moo Windows x64 MSVC-ABI: Bibliotheken allein ueber eingebettete /DEFAULTLIB-Direktive"

# P2-T56: Dieselbe Moo-Struct-Quelle, die unter Linux-SysV bereits
# C-By-value geprüft wurde, jetzt als Win64-COFF + getrennte C-Callee.
"$compiler" compile skripte/p7/lader/system_ffi_struct.moos \
    --target x86_64-pc-windows-gnu --emit-ir -o "$ausgabe/moo-struct-gnu.ll"
opt -passes=verify -disable-output "$ausgabe/moo-struct-gnu.ll"
llvm-extract -S \
    --func=sys_c_abi4 --func=sys_c_abi8 --func=sys_c_abi12 \
    --func=sys_c_abi16 --func=sys_c_abi24 --recursive \
    "$ausgabe/moo-struct-gnu.ll" -o "$ausgabe/moo-struct-extract.ll"
opt -passes=verify -disable-output "$ausgabe/moo-struct-extract.ll"
rg -q 'declare i32 @moo_p2_struct1' "$ausgabe/moo-struct-extract.ll"
rg -q 'declare i64 @moo_p2_struct2' "$ausgabe/moo-struct-extract.ll"
for n in 3 4 6; do
    rg -q "declare void @moo_p2_struct$n" "$ausgabe/moo-struct-extract.ll"
    rg -q "call void @moo_p2_struct$n\\(ptr sret" "$ausgabe/moo-struct-extract.ll"
done
if rg -q 'byval\(' "$ausgabe/moo-struct-extract.ll"; then
    echo "Win64-ABI darf an dieser C-Grenze kein SysV-byval verwenden" >&2
    exit 1
fi
llc -O2 -filetype=obj "$ausgabe/moo-struct-extract.ll" -o "$ausgabe/moo-struct.obj"
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror \
    -I skripte/p2/abi \
    skripte/p2/windows_minipc/struct_value_probe.c \
    skripte/p2/abi/struct_by_value.c \
    "$ausgabe/moo-struct.obj" -o "$ausgabe/moo_win_struct.exe"
"$winrm" skripte/p2/windows_minipc/smb_upload.py "$ausgabe/moo_win_struct.exe"
"$winrm" skripte/p2/minipc_winrm.py run "\$ErrorActionPreference='Stop'; & 'D:\\MooTest\\moo_win_struct.exe'; if (\$LASTEXITCODE -ne 0) { throw 'Moo-Win64-Struct-ABI fehlgeschlagen' }"

# MSVC-Ziel: LLVM-Verify + COFF-Objekt, kein MSVC-Runtime-Nachweis.
"$compiler" compile skripte/p7/lader/system_ffi_struct.moos \
    --target x86_64-pc-windows-msvc --emit-ir -o "$ausgabe/moo-struct-msvc.ll"
opt -passes=verify -disable-output "$ausgabe/moo-struct-msvc.ll"
llvm-extract -S \
    --func=sys_c_abi4 --func=sys_c_abi8 --func=sys_c_abi12 \
    --func=sys_c_abi16 --func=sys_c_abi24 --recursive \
    "$ausgabe/moo-struct-msvc.ll" -o "$ausgabe/moo-struct-msvc-extract.ll"
llc -O2 -filetype=obj "$ausgabe/moo-struct-msvc-extract.ll" -o "$ausgabe/moo-struct-msvc.obj"
if rg -q 'byval\(' "$ausgabe/moo-struct-msvc-extract.ll"; then
    echo "MSVC ABI darf an dieser C-Grenze kein SysV-byval verwenden" >&2
    exit 1
fi

# Nicht klassifizierte C-Aggregate bleiben unter Windows weiterhin gesperrt.
for negativ in system_abi_20_verbot.moos system_abi_mixed_verbot.moos; do
    if "$compiler" compile "skripte/p7/lader/$negativ" \
        --target x86_64-pc-windows-gnu --emit-ir -o "$ausgabe/negativ.ll" \
        >"$ausgabe/negativ.log" 2>&1; then
        echo "Unerwartete Windows-C-ABI-Freigabe: $negativ" >&2
        exit 1
    fi
    rg -q 'noch nicht klassifiziert' "$ausgabe/negativ.log" || {
        echo "Unerwartete Windows-C-ABI-Diagnose: $negativ" >&2
        cat "$ausgabe/negativ.log" >&2
        exit 1
    }
done
echo "PASS Moo Win64-Struct-by-value auf N100; MSVC LLVM/COFF und Negativ-Grenzen geprueft"

# P2-T56: neue Typklassen mit derselben Moo-Quelle auf Linux UND Windows.
bash skripte/p2/abi/complex_e2e.sh "$compiler"

# Derselbe Source-Bestand zusaetzlich auf AArch64 AAPCS64-C-ABI
# querpruefen; das ist ein Cross-/Link-Beleg, kein AArch64-Hardwarelauf.
bash skripte/p2/abi/aarch64_struct_slice.sh "$compiler"

# P2-T62: unveraenderter Moo-Mat4-Pilot auf echtem Windows-11-N100 pruefen.
# Der vorhandene Grafik-Differenztest (inkl. Alias/NaN) wird wiederverwendet.
# Windows-COFF hat ein eigenes main-Symbol; nur die exportierten Funktionen
# werden aus dem LLVM-IR extrahiert und mit derselben O2-Pipeline gebaut.
mat4=skripte/p2/grafik
"$compiler" compile "$mat4/mat4_nativ.moos" --no-stdlib \
    --target x86_64-pc-windows-gnu --emit-ir -o "$ausgabe/mat4-win.ll"
opt -passes=verify -disable-output "$ausgabe/mat4-win.ll"
mat4_funktionen="moo_n_mat4_identity moo_n_mat4_multiply moo_n_mat4_multiply_skalar
moo_n_mat4_translate moo_n_mat4_translate_skalar moo_n_mat4_scale moo_n_mat4_scale_skalar
moo_n_mat4_ortho moo_n_mat4_perspective moo_n_mat4_lookat moo_n_mat4_rotate moo_n_mat4_rotate_skalar
moo_n_matrix_stack_init moo_n_matrix_stack_push moo_n_matrix_stack_pop"
mat4_auswahl=""
for funktion in $mat4_funktionen; do mat4_auswahl="$mat4_auswahl --func=$funktion"; done
# Absichtlich unquotiert: die Optionsliste wird in einzelne --func Argumente zerlegt.
llvm-extract -S --recursive $mat4_auswahl "$ausgabe/mat4-win.ll" -o "$ausgabe/mat4-only.ll"
opt -passes='default<O2>' -S "$ausgabe/mat4-only.ll" -o "$ausgabe/mat4-opt.ll"
opt -passes=verify -disable-output "$ausgabe/mat4-opt.ll"
llc -mtriple=x86_64-pc-windows-gnu -O2 -filetype=obj \
    "$ausgabe/mat4-opt.ll" -o "$ausgabe/mat4-win.obj"
# -static ist notwendig: ohne das Flag fehlte dem Testrechner libwinpthread-1.dll
# (Windows-Exitcode 0xC0000135). Keine DLLs auf dem Testrechner installieren.
x86_64-w64-mingw32-gcc -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror \
    -static -static-libgcc -I compiler/runtime "$mat4/test_mat4_nativ.c" compiler/runtime/moo_3d_math.c \
    "$ausgabe/mat4-win.obj" -lm -o "$ausgabe/moo_win_mat4.exe"
if x86_64-w64-mingw32-objdump -p "$ausgabe/moo_win_mat4.exe" | grep -qi 'DLL Name: libwinpthread-1.dll'; then
    echo 'Win64-Mat4: libwinpthread darf nicht dynamisch benoetigt werden' >&2
    exit 1
fi
"$winrm" skripte/p2/windows_minipc/smb_upload.py "$ausgabe/moo_win_mat4.exe"
"$winrm" skripte/p2/minipc_winrm.py run "\$ErrorActionPreference='Stop'; & 'D:\\MooTest\\moo_win_mat4.exe' 200000 300000 5; if (\$LASTEXITCODE -ne 0) { throw 'Moo-Win64-Mat4-Differenztest fehlgeschlagen' }"
echo 'PASS Windows-N100 Mat4 + Matrix-Stack: 200000 Runden, 57.6 Mio Matrixwerte + 200000 Stack-Operationen (vollstaendiger C-Moo-Memcmp, Ueber-/Unterlauf); Benchmark nur Diagnose'


