#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out=build/p11/buffer-feld
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/buffer_feld_host.moos -o "$out/host"
"$out/host" > "$out/host.txt"
printf '0\n' | diff -u - "$out/host.txt"
"$compiler" compile compiler/tests/regression/p11_buffer_feld.moos -o "$out/core"
"$out/core" > "$out/core.txt"
diff -u compiler/tests/regression/p11_buffer_feld.expected "$out/core.txt"
"$compiler" compile compiler/tests/p11/buffer_feld_grenzen.moos -o "$out/grenzen"
"$out/grenzen" > "$out/grenzen.txt"
printf '0\n' | diff -u - "$out/grenzen.txt"
for target in x86_64-bare x86_64-unknown-uefi; do
    "$compiler" compile compiler/tests/p11/buffer_feld.moos --no-stdlib --target "$target" --emit-ir -o "$out/$target.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
done
for name in rueckgabe kopie speichern array typ ffi zeiger ansicht konstant box; do
    for target in x86_64-linux x86_64-bare; do
        args=()
        [ "$target" != x86_64-linux ] || args=()
        [ "$target" != x86_64-bare ] || args=(--no-stdlib --target x86_64-bare)
        if "$compiler" compile "compiler/tests/p11/buffer_feld_$name.moos" "${args[@]}" --emit-ir -o "$out/$name-$target.ll" > "$out/$name-$target.log" 2>&1; then
            echo "FAIL Bufferfeld: $name/$target akzeptiert" >&2
            exit 1
        fi
        case "$name" in
            box)
                if [ "$target" = x86_64-bare ]; then
                    rg -q -F 'Strukturwerte brauchen die hosted Runtime' "$out/$name-$target.log"
                else
                    rg -q -F 'Laufzeitbeschreibung' "$out/$name-$target.log"
                fi
                ;;
            konstant) rg -q 'Konstante' "$out/$name-$target.log" ;;
            typ) rg -q 'Zahlen- oder Strukturtyp' "$out/$name-$target.log" ;;
            zeiger) rg -q 'unsicher' "$out/$name-$target.log" ;;
            ffi) rg -q 'C-ABI-Klasse' "$out/$name-$target.log" ;;
            *) rg -q 'M-M002' "$out/$name-$target.log" ;;
        esac
    done
done
# Der bestehende enge Bare-Pointer-Emitter bleibt einschließlich unaligniertem
# Zugriff erhalten; derselbe unabhängige Bestandsadapter führt das Objekt aus.
"$compiler" compile compiler/tests/p2/zeiger_lesen.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/zeiger-bestand.ll"
mise exec -- llvm-extract -S --func=__moo_native.p2_zeiger_lesen --func=__moo_native.p2_zeiger_lesen_zwei "$out/zeiger-bestand.ll" -o "$out/zeiger-bestand.extract.ll"
mise exec -- llc -filetype=obj "$out/zeiger-bestand.extract.ll" -o "$out/zeiger-bestand.o"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror compiler/tests/p2/zeiger_lesen_adapter.cpp "$out/zeiger-bestand.o" -o "$out/zeiger-bestand"
"$out/zeiger-bestand"
echo 'PASS Bufferfelder: geliehene Deskriptorkopie, Datenalias, Felder/Strukturpuffer, 20 negative Zielproben, Arrayleihe und vier Bereichsfehler'
