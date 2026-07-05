#!/usr/bin/env bash
# run_tag_sync_gate.sh — TYP-ZENTRAL Phase 3: MooTag-Drift-Gate (Task b8d73889)
# ============================================================================
# ZWECK
#   Die MooTag-Zahlen leben an DREI Orten, von denen zwei das C-enum nicht
#   einbinden koennen:
#     1. runtime/moo_runtime.h  (typedef enum MooTag)   — die WAHRHEIT
#     2. src/codegen.rs         (pub mod moo_tag)       — Rust sieht kein C-enum
#     3. runtime/moo_wasm.c     (#define MOO_*)         — freestanding, eigene
#                                                         Minimal-Structs, kein
#                                                         moo_runtime.h-Include
#   Ein enum-Reorder in moo_runtime.h wuerde ohne dieses Gate STILL falsche
#   Tags erzeugen (Codegen baut MooValues mit falschem Tag -> die Runtime
#   interpretiert Pointer/Doubles falsch = SEGV-Klasse). Dieses Gate parst
#   das enum und vergleicht jede Zweitpflege-Stelle wertgenau.
#
# EXIT-CODES: 0 synchron | 1 Drift oder Parse-Fehler
# ============================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPILER_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
HDR="$COMPILER_DIR/runtime/moo_runtime.h"
CG="$COMPILER_DIR/src/codegen.rs"
WASM="$COMPILER_DIR/runtime/moo_wasm.c"

fail=0

# enum aus moo_runtime.h parsen: "MOO_X = N" oder "MOO_X  = N," -> "MOO_X N"
enum_wert() {  # $1 = Tag-Name -> Wert auf stdout, leer wenn nicht gefunden
  awk -v n="$1" '/typedef enum/{e=1} e && $1 == n {gsub(/[=,]/,"",$0); print $2; exit} /} MooTag;/{e=0}' "$HDR"
}

pruef() {  # $1=Quelle $2=Tag-Name $3=gefundener Wert
  local soll
  soll=$(enum_wert "$2")
  if [ -z "$soll" ]; then
    echo "[tag-gate] FAIL: $2 nicht im MooTag-enum ($HDR) gefunden"; fail=1; return
  fi
  if [ "$soll" != "$3" ]; then
    echo "[tag-gate] FAIL: $1: $2 = $3, aber moo_runtime.h sagt $soll (DRIFT!)"; fail=1
  fi
}

# --- 2. codegen.rs: pub mod moo_tag { pub const NAME: u64 = N; // MOO_NAME }
CG_ZEILEN=$(awk '/pub mod moo_tag/{m=1} m && /pub const/ {print} /^}/ && m {m=0}' "$CG")
if [ -z "$CG_ZEILEN" ]; then
  echo "[tag-gate] FAIL: pub mod moo_tag in codegen.rs nicht gefunden"; fail=1
else
  while IFS= read -r z; do
    # "pub const NONE: u64 = 3;    // MOO_NONE"
    wert=$(echo "$z" | sed -n 's/.*= *\([0-9][0-9]*\);.*/\1/p')
    name=$(echo "$z" | sed -n 's/.*\/\/ *\(MOO_[A-Z_0-9]*\).*/\1/p')
    if [ -z "$wert" ] || [ -z "$name" ]; then
      echo "[tag-gate] FAIL: codegen.rs moo_tag-Zeile ohne Wert oder MOO_-Kommentar: $z"; fail=1
    else
      pruef "codegen.rs" "$name" "$wert"
    fi
  done <<< "$CG_ZEILEN"
fi

# --- 3. moo_wasm.c: #define MOO_X N
WASM_ZEILEN=$(grep -E '^#define MOO_[A-Z_0-9]+ +[0-9]+' "$WASM")
if [ -z "$WASM_ZEILEN" ]; then
  echo "[tag-gate] FAIL: keine MOO_-#defines in moo_wasm.c gefunden (Gate anpassen?)"; fail=1
else
  while IFS= read -r z; do
    name=$(echo "$z" | awk '{print $2}')
    wert=$(echo "$z" | awk '{print $3}')
    pruef "moo_wasm.c" "$name" "$wert"
  done <<< "$WASM_ZEILEN"
fi

# --- Nackte Tag-Hardcodes duerfen nicht zurueckkommen (Regressions-Schutz):
# jedes const_int(N, false) mit MOO_-Kommentar ausserhalb des moo_tag-Moduls.
RUECKFALL=$(grep -nE 'const_int\([0-9]+, (false|true)\).*MOO_[A-Z]' "$CG" | grep -v 'moo_tag' || true)
if [ -n "$RUECKFALL" ]; then
  echo "[tag-gate] FAIL: nackte Tag-Zahl(en) in codegen.rs (nutze moo_tag::*):"
  echo "$RUECKFALL" | sed 's/^/    /'
  fail=1
fi

[ "$fail" -eq 0 ] && echo "[tag-gate] PASS: MooTag synchron (moo_runtime.h == codegen.rs moo_tag == moo_wasm.c)"
exit $fail
