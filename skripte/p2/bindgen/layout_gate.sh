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
         compiler/tests/p2/bindgen_fixture_packed.h)
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
for fixture in struktur_pointer_unbekannt struktur_pointer_wertzyklus; do
    rc=0
    timeout 60s "$compiler" layout "compiler/tests/p2/$fixture.moos" > "$out/invalid.out" 2> "$out/invalid.err" || rc=$?
    [[ $rc -ne 0 && $rc -ne 124 && ! -s "$out/invalid.out" && -s "$out/invalid.err" ]] || {
        echo "FAIL: layout diagnostic accepted $fixture or produced partial output (rc=$rc)" >&2
        exit 1
    }
    case "$fixture" in
        struktur_pointer_unbekannt) rg -q "Unbekannter Pointer-Zieltyp" "$out/invalid.err" ;;
        struktur_pointer_wertzyklus) rg -q "Strukturwert-Zyklus" "$out/invalid.err" ;;
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
sha256sum -- "${sources[@]}" > "$out/after.sha256"
cmp "$out/before.sha256" "$out/after.sha256"
echo 'Registered Moo/C layout comparison: PASS'
