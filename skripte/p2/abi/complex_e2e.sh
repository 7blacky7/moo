#!/usr/bin/env bash
# Gemeinsam genutzte Moo->C->Moo-ABI-Proben: SysV-Linux und echtes Win64.
# Dieser Teil ist ein Baustein des vorhandenen Windows-N100-Testworkflows.
set -euo pipefail
cd "$(dirname "$0")/../../.."

compiler="$1"
out=build/p2/windows_minipc
quelle=skripte/p7/lader/system_ffi_struct_complex.moos
referenz=skripte/p2/abi/struct_by_value.c
oracle=skripte/p2/abi/struct_complex_probe.c
winrm=build/moo-winrm-client/bin/python
mkdir -p "$out"

# EIN Moos-Quellprogramm fuer beide x86_64 ABIs und MSVC-Crossbeleg.
for target in x86_64-bare x86_64-pc-windows-gnu x86_64-pc-windows-msvc; do
    "$compiler" compile "$quelle" --target "$target" --emit-ir -o "$out/$target-complex.ll"
    opt -passes=verify -disable-output "$out/$target-complex.ll"
    llvm-extract -S --func=sys_c_complex_mixed \
        --func=sys_c_complex_float4 --func=sys_c_complex_packed \
        --func=sys_c_pair8 --func=sys_c_pair12 \
        --func=sys_c_pair_mixed --func=sys_c_pair_float4 \
        --func=sys_c_pair_packed --func=sys_c_pressure \
        --func=sys_c_pressure12_5 --func=sys_c_pressure12_6 \
        --func=sys_c_pressure16_5 --func=sys_c_pressure_mixed6 \
        --func=sys_c_pair12_pressure3 --func=sys_c_pressure_float4_6 \
        --recursive "$out/$target-complex.ll" -o "$out/$target-complex-call.ll"
    opt -passes=verify -disable-output "$out/$target-complex-call.ll"
    llc -O2 -filetype=obj "$out/$target-complex-call.ll" -o "$out/$target-complex.o"
done

# Clang ist das unabhaengige C-Frontend-Referenzmodell.
# Bei SysV werden mehrteilige Aggregate erst bei fehlenden GP-/SSE-
# Registern als GANZES byval uebergeben; die Rueckgabe bleibt Register-ABI.
clang --target=x86_64-unknown-linux-gnu -std=c11 -O0 -S -emit-llvm \
    -I skripte/p2/abi "$referenz" -o "$out/clang-pressure-sysv.ll"
clang --target=x86_64-pc-windows-gnu -std=c11 -O0 -S -emit-llvm \
    -I skripte/p2/abi "$referenz" -o "$out/clang-pressure-win64.ll"
sysv_ir="$out/x86_64-bare-complex-call.ll"
for fn in moo_p2_pressure12_5 moo_p2_pressure12_6 \
          moo_p2_pressure16_5 moo_p2_pressure_mixed6 \
          moo_p2_pair12_pressure3; do
    rg -q "declare .*@$fn\\(.*ptr byval\\(" "$sysv_ir"
    rg -q "define .*@$fn\\(.*ptr .*byval\\(" "$out/clang-pressure-sysv.ll"
done
# ZWEI 12B-Aggregate nach drei u64: erste Register, zweite byval.
rg -q 'declare \{ i64, i32 \} @moo_p2_pair12_pressure3\(i64, i64, i64, i64, i32, ptr byval' "$sysv_ir"
# Unabhaengige GP-/SSE-Budgets: f32x4 bleibt in 2 SSE-Registerteilen
# nach sechs u64, waehrend Mixed (f64+u32) komplett byval wird.
rg -q 'declare \{ <2 x float>, <2 x float> \} @moo_p2_pressure_float4_6\(i64, i64, i64, i64, i64, i64, <2 x float>, <2 x float>\)' "$sysv_ir"

# GNU/Windows- und MSVC-ABI duerfen f64+u32/f32x4/packed nicht
# wie SysV in SSE-Registern bzw. als byval-Parameter uebergeben.
for target in x86_64-pc-windows-gnu x86_64-pc-windows-msvc; do
    datei="$out/$target-complex-call.ll"
    for fn in moo_p2_struct_mixed moo_p2_float4 moo_p2_packed \
              moo_p2_pair12 moo_p2_pair_mixed moo_p2_pair_float4 moo_p2_pair_packed \
              moo_p2_pressure12_5 moo_p2_pressure12_6 \
              moo_p2_pressure16_5 moo_p2_pressure_mixed6 \
              moo_p2_pair12_pressure3 moo_p2_pressure_float4_6; do
        rg -q "declare void @$fn\\(ptr sret" "$datei"
    done
    rg -q 'declare i64 @moo_p2_pair8\(i64, i64\)' "$datei"
    rg -q 'declare i64 @moo_p2_pressure\(i64, i64, i64, i64, i64, i64, i64\)' "$datei"
    if rg -q 'byval\(' "$datei"; then
        echo "Unerlaubte SysV-byval-ABI im $target" >&2
        exit 1
    fi
done

# Negative ABI-Scope-Grenzen bleiben erhalten.
if "$compiler" compile skripte/p7/lader/system_abi_pressure_multireg_verbot.moos \
    --target x86_64-bare --emit-ir -o "$out/negativ-pressure.ll" \
    >"$out/negativ-pressure.log" 2>&1; then
    echo "UNZULAESSIG: mehr als sechs u64-Praefixe freigegeben" >&2
    exit 1
fi
rg -q 'bis zu 6 u64-Praefixe' "$out/negativ-pressure.log"
if "$compiler" compile skripte/p7/lader/system_abi_fremde_struktur_verbot.moos \
    --target x86_64-pc-windows-gnu --emit-ir -o "$out/negativ-fremd.ll" \
    >"$out/negativ-fremd.log" 2>&1; then
    echo "UNZULAESSIG: zwei verschieden nominalisierte Strukturen als eine ABI freigegeben" >&2
    exit 1
fi
rg -q 'gleichartige Strukturargumente' "$out/negativ-fremd.log"
if "$compiler" compile skripte/p7/lader/system_abi_drei_strukturen_verbot.moos \
    --target x86_64-pc-windows-gnu --emit-ir -o "$out/negativ-drei.ll" \
    >"$out/negativ-drei.log" 2>&1; then
    echo "UNZULAESSIG: drei Struct-Parameter freigegeben" >&2
    exit 1
fi
rg -q '1..2 Strukturargumente' "$out/negativ-drei.log"

# Linux: bestaetigt echte SSE-/INTEGER- und Packed-byval-Uebergabe.
gcc -std=c11 -O2 -Wall -Wextra -Werror -no-pie -I skripte/p2/abi \
    "$oracle" "$referenz" "$out/x86_64-bare-complex.o" \
    -o "$out/moo_linux_complex"
"$out/moo_linux_complex"

# Windows N100: gleicher Moo-Code, separat kompilierte C-Callees.
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror \
    -I skripte/p2/abi "$oracle" "$referenz" \
    "$out/x86_64-pc-windows-gnu-complex.o" -o "$out/moo_win_complex.exe"
"$winrm" skripte/p2/windows_minipc/smb_upload.py "$out/moo_win_complex.exe"
"$winrm" skripte/p2/minipc_winrm.py run "\$ErrorActionPreference='Stop'; & 'D:\\MooTest\\moo_win_complex.exe'; if (\$LASTEXITCODE -ne 0) { throw 'Moo Complex Struct ABI Windows FAIL' }"
echo "PASS komplexe Moo-Struct-C-ABI: Linux SysV + echtes Windows N100; MSVC LLVM/COFF"
