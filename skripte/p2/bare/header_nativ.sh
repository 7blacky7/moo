#!/usr/bin/env bash
# P2-T28 (Plan 607b20ba A1/A2, D1/D2; Bootloader-Bezug P1-T118).
#   header   Moo-Konstante MB2_KOPF: 24 Byte in .multiboot2, Sektionsausrichtung 8,
#            Symbol vorhanden, bytegleich zur C-Referenz (header_reference.sh),
#            ohne ausdrueckliche Umlaufform (als_wrap) ein Compilefehler.
#   rohdaten typisierte Konstante [u32; 4] == @rohdaten_u32 (Bytes der Sektionen).
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
out=build/p2-bare-nativ
mkdir -p "$out"
modus="${1:-alle}"

objekt() { # quelle ziel
  timeout 60s "$compiler" compile "$1" --no-stdlib --emit-ir --target x86_64-bare -o "$2.ll"
  timeout 30s mise exec -- opt -passes=verify -disable-output "$2.ll"
  timeout 30s mise exec -- llc -filetype=obj "$2.ll" -o "$2.o"
}

header() {
  cc -std=c11 -O2 -Wall -Wextra -Werror skripte/p2/bare/mb2_check.c -o "$out/mb2_check"
  objekt compiler/tests/p2/bare_mb2_kopf.moos "$out/kopf"
  readelf -SW "$out/kopf.o" | awk '$0 ~ /[[:space:]]\.multiboot2[[:space:]]/ { if ($NF != 8) exit 1; ok=1 } END { if (!ok) exit 1 }' \
    || { echo "FAIL header: .multiboot2 fehlt oder Ausrichtung != 8"; readelf -SW "$out/kopf.o"; exit 1; }
  readelf -sW "$out/kopf.o" | rg -q 'MB2_KOPF' || { echo "FAIL header: Symbol MB2_KOPF fehlt"; exit 1; }
  objcopy --dump-section ".multiboot2=$out/kopf.bin" "$out/kopf.o"
  "$out/mb2_check" "$out/kopf.bin"
  bash skripte/p2/bare/header_reference.sh > "$out/referenz.log"
  cmp build/p2-bare-header/header.bin "$out/kopf.bin"
  sha256sum "$out/kopf.bin" build/p2-bare-header/header.bin
  set +e
  timeout 60s "$compiler" compile compiler/tests/p2/bare_mb2_kopf_ohne_wrap.moos --no-stdlib --emit-ir --target x86_64-bare \
    -o "$out/ohne_wrap.ll" > "$out/ohne_wrap.log" 2>&1
  rc=$?
  set -e
  if [ "$rc" -eq 0 ] || [ "$rc" -ge 124 ]; then
    echo "FAIL header: Ueberlauf ohne als_wrap wurde nicht abgelehnt (Exit $rc)"; exit 1
  fi
  cat "$out/ohne_wrap.log"
  # B3: zweiter Kopf und Nicht-x86-Ziel sind Compilefehler.
  for fall in "doppelt|compiler/tests/p2/bare_mb2_doppelt.moos|x86_64-bare|ist bereits durch" \
              "aarch64|compiler/tests/p2/bare_mb2_kopf.moos|aarch64-bare|gibt es nur fuer x86_64" \
              "rohdaten|compiler/tests/p2/bare_mb2_rohdaten.moos|x86_64-bare|nicht fuer @rohdaten_u32" \
              "funktion|compiler/tests/p2/bare_mb2_funktion.moos|x86_64-bare|nur fuer eine @nativ konstante"; do
    IFS='|' read -r name quelle ziel text <<<"$fall"
    set +e
    timeout 60s "$compiler" compile "$quelle" --no-stdlib --emit-ir --target "$ziel" -o "$out/neg_$name.ll" > "$out/neg_$name.log" 2>&1
    rc=$?
    set -e
    if [ "$rc" -eq 0 ] || [ "$rc" -ge 124 ] || ! rg -q -F "$text" "$out/neg_$name.log"; then
      echo "FAIL header: Negativfall $name (Exit $rc)"; cat "$out/neg_$name.log"; exit 1
    fi
    echo "PASS Negativfall $name"
  done
  echo "PASS header-nativ: 24 Byte, Ausrichtung 8, bytegleich zur C-Referenz, Umlauf nur ausdruecklich"
}

rohdaten() {
  objekt compiler/tests/p2/bare_rohdaten_gleich.moos "$out/roh"
  objcopy --dump-section ".p2_typisiert=$out/typisiert.bin" --dump-section ".p2_roh=$out/roh.bin" "$out/roh.o"
  cmp "$out/typisiert.bin" "$out/roh.bin"
  [ "$(stat -c %s "$out/roh.bin")" -eq 16 ] || { echo "FAIL rohdaten: nicht 16 Byte"; exit 1; }
  echo "PASS rohdaten-gleich: [u32; 4] == @rohdaten_u32 (16 Byte)"
}

# Review 4571f610: als_wrap mit Literalen ausserhalb des Zieltyps.
wrap() {
  objekt compiler/tests/p2/bare_als_wrap_literal.moos "$out/wrap"
  objcopy --dump-section ".p2_wrap=$out/wrap.bin" "$out/wrap.o"
  printf '\x00\xff\xff\x2a' > "$out/wrap.erwartet"
  cmp "$out/wrap.erwartet" "$out/wrap.bin"
  echo "PASS als_wrap-Literale: als_wrap(u8, 256/0x1FF/-1/0x12A) = 00 ff ff 2a"
  # Review acbff6e0: Umlauf auch fuer Operator-Zwischenergebnisse (u64 MAX*MAX = 1,
  # 0 - 1 = MAX, 2^63 * 2 = 0). Fixture wird vom Gate erzeugt.
  printf '%s\n' '@nativ' '@sektion(".p2_wrap_op")' '@benutzt' \
    'konstante WRAP_OP: [u64; 3] auf [als_wrap(u64, 0xFFFFFFFFFFFFFFFF * 0xFFFFFFFFFFFFFFFF), als_wrap(u64, 0 - 1), als_wrap(u64, 0x8000000000000000 * 2)]' \
    > "$out/wrap_op.moos"
  objekt "$out/wrap_op.moos" "$out/wrap_op"
  objcopy --dump-section ".p2_wrap_op=$out/wrap_op.bin" "$out/wrap_op.o"
  printf '\x01\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00' > "$out/wrap_op.erwartet"
  cmp "$out/wrap_op.erwartet" "$out/wrap_op.bin"
  echo "PASS als_wrap-Operatoren: u64 MAX*MAX = 1, 0 - 1 = MAX, 2^63 * 2 = 0"
}

case "$modus" in
  header) header ;;
  rohdaten) rohdaten ;;
  wrap) wrap ;;
  alle) header; rohdaten; wrap ;;
  *) echo "Unbekannter Modus" >&2; exit 2 ;;
esac
