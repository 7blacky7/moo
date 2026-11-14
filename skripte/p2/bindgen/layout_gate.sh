#!/usr/bin/env bash
# T52: compare real C layout with the compiler's registered TargetData.
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-bindgen/layout-gate
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
timeout 30s "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror skripte/p2/bindgen/layout_ref.c -o "$out/c-reference"
timeout 30s "${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror skripte/p2/bindgen/layout_compare.cpp -o "$out/compare"
timeout 30s "$out/c-reference" > "$out/c.rows"
timeout 30s "$out/compare" "$out/c.rows" "$out/c.rows"
sed '1s/^8 /9 /' "$out/c.rows" > "$out/altered.rows"
if timeout 30s "$out/compare" "$out/c.rows" "$out/altered.rows"; then
    echo 'FAIL: comparator did not detect modified layout' >&2
    exit 1
fi
if [[ ${1:-layout} == reference ]]; then
    echo 'C reference and comparator control: PASS'
    exit 0
fi
sources=("$compiler" skripte/p2/bindgen/layout_gate.sh skripte/p2/bindgen/layout_probe.h
         skripte/p2/bindgen/layout_ref.c skripte/p2/bindgen/layout_compare.cpp
         skripte/p2/bindgen/layout_pointer_ref.cpp skripte/p2/abi/pointer_layout.c
         compiler/tests/p2/struktur_pointer_felder.moos compiler/tests/p2/struktur_pointer_unbekannt.moos
         compiler/tests/p2/struktur_pointer_wertzyklus.moos compiler/tests/p2/ast_struktur_import.moos
         compiler/tests/p2/import_struktur_modul.moos compiler/tests/p2/struktur_layout.moos
         skripte/p2/bindgen/layout_packed_ref.c skripte/p2/bindgen/layout_generated_packed_ref.c
         compiler/tests/p2/bindgen_fixture_packed.h compiler/tests/p2/struktur_layout_matrix.moos
         compiler/tests/p2/struktur_ausrichten_klein.moos)
sha256sum -- "${sources[@]}" > "$out/before.sha256"
timeout 30s "$compiler" bindgen skripte/p2/bindgen/layout_probe.h --prefix P2Layout > "$out/generated.moos"
timeout 60s "$compiler" layout "$out/generated.moos" > "$out/layout.json"
jq -er '
  if .schema != 1 or (.target_triple | type != "string" or (startswith("x86_64-") and contains("linux") | not))
  then error("unexpected schema or target") else . end
  | .structs as $s
  | def row($name; $fields):
      [$s[] | select(.name == $name)] as $matches
      | if ($matches|length) != 1 then error("missing or duplicate struct: " + $name)
        elif ([$matches[0].fields[].name] != $fields) then error("field mismatch: " + $name)
        else [$matches[0].size, $matches[0].alignment] + [$matches[0].fields[].offset] end;
  [row("P2LayoutProbe"; ["first","second"]),
   row("P2LayoutPadded"; ["tag","address","flags"]),
   row("P2LayoutNested"; ["head","entry","values","tail"])]
  | .[] | join(" ")
' "$out/layout.json" > "$out/moo.rows"
timeout 30s "$out/compare" "$out/c.rows" "$out/moo.rows"

# The same registered diagnostic must be stable and reject invalid structures.
timeout 60s "$compiler" layout "$out/generated.moos" > "$out/layout-repeat.json"
cmp "$out/layout.json" "$out/layout-repeat.json"
for fixture in struktur_pointer_unbekannt struktur_pointer_wertzyklus struktur_ausrichten_klein; do
    rc=0
    timeout 60s "$compiler" layout "compiler/tests/p2/$fixture.moos" > "$out/invalid.out" 2> "$out/invalid.err" || rc=$?
    [[ $rc -ne 0 && $rc -ne 124 && ! -s "$out/invalid.out" && -s "$out/invalid.err" ]] || {
        echo "FAIL: layout diagnostic accepted $fixture or produced partial output (rc=$rc)" >&2
        exit 1
    }
    case "$fixture" in
        struktur_pointer_unbekannt) rg -q "Unbekannter Pointer-Zieltyp" "$out/invalid.err" ;;
        struktur_pointer_wertzyklus) rg -q "Strukturwert-Zyklus" "$out/invalid.err" ;;
        struktur_ausrichten_klein) rg -q "unter der natürlichen Ausrichtung" "$out/invalid.err" ;;
    esac
done
timeout 60s "$compiler" layout "$out/generated.moos" --target x86_64-uefi > "$out/uefi.json"
jq -e '.schema == 1 and (.target_triple | contains("uefi"))' "$out/uefi.json" >/dev/null

# Pointer table has a separately compiled C oracle and a C++ reference adapter.
timeout 30s "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -c skripte/p2/abi/pointer_layout.c -o "$out/pointer-c.o"
timeout 30s "${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror skripte/p2/bindgen/layout_pointer_ref.cpp "$out/pointer-c.o" -o "$out/pointer-reference"
timeout 30s "$out/pointer-reference" > "$out/pointer-c.rows"
timeout 60s "$compiler" layout compiler/tests/p2/struktur_pointer_felder.moos > "$out/pointer.json"
jq -er '
  [.structs[] | select(.name == "P2PointerTabelle")] as $x
  | if ($x|length) != 1 or [$x[0].fields[].name] != ["status","daten","ausgabe"]
    then error("pointer struct or fields mismatch")
    else [$x[0].size,$x[0].alignment,$x[0].fields[].offset] | join(" ") end
' "$out/pointer.json" > "$out/pointer-moo.rows"
timeout 30s "$out/compare" "$out/pointer-c.rows" "$out/pointer-moo.rows"
timeout 60s "$compiler" layout compiler/tests/p2/ast_struktur_import.moos > "$out/import.json"
jq -e '.structs == [{"name":"ImportPaket","size":4,"alignment":4,"fields":[{"name":"wert","offset":0}]}]' "$out/import.json" >/dev/null
timeout 60s "$compiler" layout compiler/tests/p2/struktur_layout.moos > "$out/packed.json"
timeout 30s "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror skripte/p2/bindgen/layout_packed_ref.c -o "$out/packed-reference"
timeout 30s "$out/packed-reference" > "$out/packed-c.rows"
jq -er '
  .structs as $s
  | def row($name; $fields):
      [$s[] | select(.name == $name)] as $x
      | if ($x|length) != 1 or [$x[0].fields[].name] != $fields
        then error("packed struct or fields mismatch: " + $name)
        else [$x[0].size,$x[0].alignment,$x[0].fields[].offset] | join(" ") end;
  row("Innen"; ["klein","flagge"]), row("Paket"; ["marke","kern","werte"])
' "$out/packed.json" > "$out/packed-moo.rows"
timeout 30s "$out/compare" "$out/packed-c.rows" "$out/packed-moo.rows"
# Bindgen-generated @gepackt record, compared to its actual C header.
timeout 30s "$compiler" bindgen compiler/tests/p2/bindgen_fixture_packed.h --prefix P2BindgenPacked > "$out/generated-packed.moos"
timeout 60s "$compiler" layout "$out/generated-packed.moos" > "$out/generated-packed.json"
timeout 30s "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -Icompiler/tests/p2 skripte/p2/bindgen/layout_generated_packed_ref.c -o "$out/generated-packed-reference"
timeout 30s "$out/generated-packed-reference" > "$out/generated-packed-c.rows"
jq -er '
  [.structs[] | select(.name == "P2BindgenPacked")] as $x
  | if ($x|length) != 1 or [$x[0].fields[].name] != ["tag","value","array"]
    then error("generated packed struct or fields mismatch")
    else [$x[0].size,$x[0].alignment,$x[0].fields[].offset] | join(" ") end
' "$out/generated-packed.json" > "$out/generated-packed-moo.rows"
timeout 30s "$out/compare" "$out/generated-packed-c.rows" "$out/generated-packed-moo.rows"

# P2-T13/T48 (Plan a71947f7 E2): je Ziel erzeugt Moo einen C11-Header mit
# _Static_assert fuer sizeof/_Alignof/offsetof/Feldgroesse/Arraystride, den der
# Ziel-C-Compiler prueft (-fsyntax-only, freestanding, auch cross). Dieselbe
# Matrix nutzt groesse_von/ausrichtung_von/versatz_von; deren gefaltete
# IR-Konstanten muessen zum Layout-JSON desselben Ziels passen.
matrix=compiler/tests/p2/struktur_layout_matrix.moos
clang_bin="${CLANG:-clang}"
host_ziel=x86_64-unknown-linux-gnu
for ziel in "$host_ziel" i686-unknown-linux-gnu aarch64-unknown-linux-gnu x86_64-pc-windows-msvc; do
    timeout 60s "$compiler" layout "$matrix" --target "$ziel" > "$out/matrix-$ziel.json"
    jq -e --arg z "$ziel" '.target_triple == $z and (.structs | length) >= 10' "$out/matrix-$ziel.json" >/dev/null
    timeout 60s "$compiler" layout "$matrix" --target "$ziel" --c-header > "$out/matrix-$ziel.h"
    [[ $(grep -c '^struct ' "$out/matrix-$ziel.h") -eq $(jq '.structs | length' "$out/matrix-$ziel.json") ]] || {
        echo "FAIL: Matrix-Header $ziel enthaelt nicht jede Struktur" >&2; exit 1; }
    timeout 30s "$clang_bin" --target="$ziel" -std=c11 -ffreestanding -fsyntax-only -Wall -Wextra -Werror -x c "$out/matrix-$ziel.h"
    timeout 60s "$compiler" compile "$matrix" --emit-ir --target "$ziel" -o "$out/matrix-$ziel.ll" > /dev/null
    soll=$(jq -r '.structs as $s
      | def st($n): [$s[] | select(.name == $n)][0];
        def off($n; $f): [st($n).fields[] | select(.name == $f)][0].offset;
      "AUSRICHTUNG_KNOTEN=\(st("Knoten").alignment)", "GROESSE_VERTEX=\(st("Vertex").size)",
      "VERSATZ_ANZAHL=\(off("Netz"; "anzahl"))", "VERSATZ_KOPF=\(off("KopfVerschachtelt"; "kopf"))"' \
      "$out/matrix-$ziel.json" | sort | tr '\n' ' ')
    ist=$(sed -nE 's/^@(GROESSE_VERTEX|AUSRICHTUNG_KNOTEN|VERSATZ_KOPF|VERSATZ_ANZAHL) = constant i64 ([0-9]+)$/\1=\2/p' \
      "$out/matrix-$ziel.ll" | sort | tr '\n' ' ')
    [[ -n "$ist" && "$ist" == "$soll" ]] || {
        echo "FAIL: Layout-Builtins $ziel: IR '$ist' != JSON '$soll'" >&2; exit 1; }
done
timeout 30s "${CC:-cc}" -std=c11 -ffreestanding -fsyntax-only -Wall -Wextra -Werror -x c "$out/matrix-$host_ziel.h"
# Kontrollproben: jede Verfaelschung muss der C-Compiler ablehnen.
kontrolle() {
    cmp -s "$out/matrix-$host_ziel.h" "$2" && { echo "FAIL: Kontrollprobe $1 hat nichts veraendert" >&2; exit 1; }
    if timeout 30s "$clang_bin" --target="$3" -std=c11 -ffreestanding -fsyntax-only -x c "$2" 2>/dev/null; then
        echo "FAIL: Kontrollprobe $1 wurde nicht erkannt" >&2; exit 1
    fi
}
sed -E '0,/offsetof\(Vertex, uv\) == [0-9]+/s//offsetof(Vertex, uv) == 999/' "$out/matrix-$host_ziel.h" > "$out/kontrolle-versatz.h"
kontrolle versatz "$out/kontrolle-versatz.h" "$host_ziel"
sed '/#pragma pack/d' "$out/matrix-$host_ziel.h" > "$out/kontrolle-pack.h"
kontrolle pack "$out/kontrolle-pack.h" "$host_ziel"
# Batch C: ohne _Alignas stimmen Ausrichtung, Einbettung und Stride nicht mehr.
sed 's/_Alignas([0-9]*) //' "$out/matrix-$host_ziel.h" > "$out/kontrolle-alignas.h"
kontrolle alignas "$out/kontrolle-alignas.h" "$host_ziel"
jq -e '[.structs[] | select(.name == "MitAusgerichtet")][0] | .alignment == 64 and .size == 192
  and ([.fields[].offset] == [0, 16, 32, 64, 96, 128])' "$out/matrix-$host_ziel.json" >/dev/null
# Zielspezifisch: das i686-Layout (4-Byte-Zeiger, i64/f64 4-Byte-ausgerichtet) passt nicht zu x86_64.
kontrolle fremdziel "$out/matrix-i686-unknown-linux-gnu.h" "$host_ziel"

sha256sum -- "${sources[@]}" > "$out/after.sha256"
cmp "$out/before.sha256" "$out/after.sha256"
echo 'Registered Moo/C layout comparison: PASS'
