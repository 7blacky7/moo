#!/usr/bin/env bash
# P2-T28 (Plan 607b20ba D3/D4/D5; Bootloader-Bezug P1-T118).
#   laufzeitfrei  in jeder __moo_native.*-Funktion (nativ_kern.moos und P7-Fixture
#                 system_koerper.moos) kein Aufruf eines moo_*-Symbols ausser der
#                 freistehenden Mathematik (moo_native_fmod/fmodf/pow/powf,
#                 P2-T89 moo_native_sin/cos/tan[f]); das isolierte Objekt hat
#                 ausser dieser Positivliste KEIN offenes Symbol (Review acbff6e0)
#   kernel        Kernel-ELF aus nativ_kern.moos: .multiboot2 genau 24 Byte und
#                 bytegleich zur C-Referenz, g_mb2_header fehlt, MB2_KOPF da;
#                 GRUB-ISO + QEMU ohne Fenster mit Marker MOO-NATIV-KERN-OK
#   referenz      hallo_kern.moos (C-Kopf) unveraendert: .multiboot2 bytegleich
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
out=build/p2-bare-nativ
mkdir -p "$out" build/p2-bare-tmp
modus="${1:-alle}"
# Freistehende Mathematik aus compiler/runtime/moo_native_math.c (Kernel linkt sie).
liste='moo_native_fmod|moo_native_fmodf|moo_native_pow|moo_native_powf|moo_native_sin|moo_native_sinf|moo_native_cos|moo_native_cosf|moo_native_tan|moo_native_tanf'
erlaubt="^($liste)$"

pruefe_ir() { # ll-datei
  awk -v erlaubt="$erlaubt" '
    /^define .*@"?__moo_native[.]/ { innen=1; name=$0; next }
    innen && /^}/ { innen=0; next }
    innen {
      while (match($0, /@"?moo_[A-Za-z0-9_]+/)) {
        sym = substr($0, RSTART + 1, RLENGTH - 1); gsub(/"/, "", sym)
        if ($0 ~ /call / && sym !~ erlaubt) { print "VERBOTEN " sym " in " name; fehler=1 }
        $0 = substr($0, RSTART + RLENGTH)
      }
    }
    END { exit fehler }
  ' "$1"
}

# D3 (Review 4571f610/acbff6e0, Plan 607b20ba E5): isoliertes Objekt aus allen
# __moo_native.*-Funktionen; llvm-nm -u darf ausser der Positivliste KEIN
# offenes Symbol enthalten (auch kein memcpy, puts, ...).
# Rueckgabe: 0 sauber, 1 verbotenes offenes Symbol, 2 Werkzeugfehler.
pruefe_objekt() { # ll name
  rm -f "$out/$2.extract.ll" "$out/$2.extract.o" "$out/$2.undef"
  timeout 60s mise exec -- llvm-extract -S --rfunc='__moo_native\..*' "$1" -o "$out/$2.extract.ll" \
    || { echo "WERKZEUGFEHLER llvm-extract ($2)"; return 2; }
  timeout 60s mise exec -- llc -filetype=obj "$out/$2.extract.ll" -o "$out/$2.extract.o" \
    || { echo "WERKZEUGFEHLER llc ($2)"; return 2; }
  timeout 30s mise exec -- llvm-nm -u "$out/$2.extract.o" > "$out/$2.undef" \
    || { echo "WERKZEUGFEHLER llvm-nm ($2)"; return 2; }
  rg -v -e " ($liste)\$" "$out/$2.undef" > "$out/$2.undef.rest.roh" || true
  # llvm-extract nimmt nur die Funktionen mit; eigene Datenobjekte des Programms
  # (im Gesamtmodul definierte globale Variablen/Konstanten, z. B. NATIV_MARKER)
  # werden dabei zu Deklarationen. Sie sind keine Laufzeitabhaengigkeit und
  # werden gegen die Definitionen des Gesamtmoduls abgeglichen; Funktionen
  # (auch eigene dynamische) bleiben verboten.
  # Nur echte Definitionen: `external`/`extern_weak` sind Deklarationen eines
  # fremden Datensymbols und bleiben offen (Review 072a9575).
  rg -N '^@"?[A-Za-z0-9_.$]+"? = [^(]*\b(global|constant)\b' "$1" \
    | rg -v '^@"?[A-Za-z0-9_.$]+"? = ([^(]*\s)?(external|extern_weak)\s' \
    | rg -o -N '^@"?([A-Za-z0-9_.$]+)"?' -r '$1' > "$out/$2.daten" || true
  awk 'NR == FNR { daten[$1] = 1; next } !(($NF) in daten)' "$out/$2.daten" "$out/$2.undef.rest.roh" > "$out/$2.undef.rest"
  if [ -s "$out/$2.undef.rest" ]; then
    echo "VERBOTEN im Objekt $2 (offen, nicht in der Positivliste):"; cat "$out/$2.undef.rest"; return 1
  fi
  return 0
}

laufzeitfrei() {
  timeout 60s "$compiler" compile beispiele/kernel/nativ_kern.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/nativ_kern.ll"
  pruefe_ir "$out/nativ_kern.ll"
  timeout 60s "$compiler" compile skripte/p7/lader/system_koerper.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/system_koerper.ll"
  pruefe_ir "$out/system_koerper.ll"
  # Kontrollprobe: eine eingeschleuste moo_*-Aufrufzeile muss erkannt werden.
  # Kontrollprobe als gueltiges IR: angehaengte __moo_native-Funktion ruft moo_print.
  cp "$out/nativ_kern.ll" "$out/kontrolle.ll"
  # (eigenes moo_-Symbol: moo_print u. a. sind im Bare-IR bereits deklariert)
  printf '\ndefine void @"__moo_native.kontrolle"() {\n  call void @moo_kontrollsymbol()\n  ret void\n}\n\ndeclare void @moo_kontrollsymbol()\n' >> "$out/kontrolle.ll"
  if pruefe_ir "$out/kontrolle.ll" > /dev/null; then echo "FAIL laufzeitfrei: Kontrollprobe nicht erkannt"; exit 1; fi
  echo "PASS laufzeitfrei IR: keine moo_*-Aufrufe in __moo_native.* (nativ_kern, system_koerper; Kontrollprobe erkannt)"
  pruefe_objekt "$out/nativ_kern.ll" nativ_kern
  pruefe_objekt "$out/system_koerper.ll" system_koerper
  # Objekt-Kontrollprobe: der eingeschleuste moo_print muss in llvm-nm -u auftauchen (Rueckgabe 1).
  set +e
  pruefe_objekt "$out/kontrolle.ll" kontrolle > "$out/kontrolle_objekt.log"
  rc=$?
  set -e
  if [ "$rc" -ne 1 ]; then echo "FAIL laufzeitfrei: Objekt-Kontrollprobe nicht erkannt (rc=$rc)"; cat "$out/kontrolle_objekt.log"; exit 1; fi
  # Zweite Kontrollprobe (Review acbff6e0): beliebiges Nicht-moo-Symbol.
  cp "$out/nativ_kern.ll" "$out/kontrolle_fremd.ll"
  printf '\ndefine void @"__moo_native.kontrolle_fremd"() {\n  call void @fremd_kontrollsymbol()\n  ret void\n}\n\ndeclare void @fremd_kontrollsymbol()\n' >> "$out/kontrolle_fremd.ll"
  set +e
  pruefe_objekt "$out/kontrolle_fremd.ll" kontrolle_fremd > "$out/kontrolle_fremd.log"
  rc=$?
  set -e
  if [ "$rc" -ne 1 ]; then echo "FAIL laufzeitfrei: Fremdsymbol-Kontrollprobe nicht erkannt (rc=$rc)"; cat "$out/kontrolle_fremd.log"; exit 1; fi
  # Dritte Kontrollprobe (Review 072a9575): externes Nicht-moo-Datensymbol.
  cp "$out/nativ_kern.ll" "$out/kontrolle_daten.ll"
  printf '\n@fremd_daten = external global i8\n\ndefine i8 @"__moo_native.kontrolle_daten"() {\n  %%v = load volatile i8, ptr @fremd_daten\n  ret i8 %%v\n}\n' >> "$out/kontrolle_daten.ll"
  set +e
  pruefe_objekt "$out/kontrolle_daten.ll" kontrolle_daten > "$out/kontrolle_daten.log"
  rc=$?
  set -e
  if [ "$rc" -ne 1 ] || ! rg -q 'fremd_daten$' "$out/kontrolle_daten.log"; then
    echo "FAIL laufzeitfrei: Datensymbol-Kontrollprobe nicht erkannt (rc=$rc)"; cat "$out/kontrolle_daten.log"; exit 1
  fi
  echo "PASS laufzeitfrei Objekt: llvm-nm -u des isolierten __moo_native.*-Objekts ohne offenes Symbol ausser der moo_native_math-Positivliste (Kontrollproben moo_*, Fremdfunktion und externes Datensymbol erkannt)"
}

mb2_bytes() { # elf ziel
  # Im gelinkten ELF legt das Linkerscript .multiboot2 in eine Ausgabesektion;
  # daher die 24 Byte ab dem (einzigen) Magic in den ersten 32 KiB lesen.
  head -c 32768 "$1" > "$out/kopf32k.bin"
  local treffer
  treffer=$(LC_ALL=C grep -obUaP '\xd6\x50\x52\xe8' "$out/kopf32k.bin" | cut -d: -f1)
  [ "$(printf '%s\n' "$treffer" | sed '/^$/d' | wc -l)" -eq 1 ] \
    || { echo "FAIL: nicht genau ein Multiboot2-Magic in den ersten 32 KiB von $1"; exit 1; }
  dd if="$1" of="$2" bs=1 skip="$treffer" count=24 status=none
}

kernel() {
  timeout 120s "$compiler" compile beispiele/kernel/nativ_kern.moos --no-stdlib --target x86_64-bare --kernel -o "$out/nativ_kern.elf"
  mb2_bytes "$out/nativ_kern.elf" "$out/nativ_kern_mb2.bin"
  [ "$(stat -c %s "$out/nativ_kern_mb2.bin")" -eq 24 ] || { echo "FAIL kernel: .multiboot2 nicht 24 Byte"; exit 1; }
  bash skripte/p2/bare/header_reference.sh > "$out/referenz.log"
  cmp build/p2-bare-header/header.bin "$out/nativ_kern_mb2.bin"
  nm "$out/nativ_kern.elf" > "$out/nativ_kern.nm"
  if rg -q 'g_mb2_header' "$out/nativ_kern.nm"; then echo "FAIL kernel: C-Kopf g_mb2_header noch vorhanden"; exit 1; fi
  rg -q ' MB2_KOPF$' "$out/nativ_kern.nm" || { echo "FAIL kernel: MB2_KOPF fehlt"; exit 1; }
  echo "PASS kernel-ELF: .multiboot2 = 24 Byte aus Moo, bytegleich zur C-Referenz, g_mb2_header entfaellt"
  TMPDIR="$PWD/build/p2-bare-tmp" MOO_BIN="$PWD/$compiler" KERNEL_MARKER="MOO-NATIV-KERN-OK" \
    bash scripts/kernel-smoke.sh "$PWD/beispiele/kernel/nativ_kern.moos" | tee "$out/kernel_smoke.log"
  rg -q 'OK: Marker MOO-NATIV-KERN-OK' "$out/kernel_smoke.log" || { echo "FAIL kernel: QEMU-Marker nicht belegt (SKIP?)"; exit 1; }
  echo "PASS kernel-nativ: GRUB/QEMU bootet den Moo-Kopf, nativer Treiber sendet MOO-NATIV-KERN-OK"
}

referenz() {
  timeout 120s "$compiler" compile beispiele/kernel/hallo_kern.moos --no-stdlib --target x86_64-bare --kernel -o "$out/hallo_kern.elf"
  mb2_bytes "$out/hallo_kern.elf" "$out/hallo_kern_mb2.bin"
  bash skripte/p2/bare/header_reference.sh > "$out/referenz.log"
  cmp build/p2-bare-header/header.bin "$out/hallo_kern_mb2.bin"
  nm "$out/hallo_kern.elf" > "$out/hallo_kern.nm"
  rg -q 'g_mb2_header' "$out/hallo_kern.nm" || { echo "FAIL referenz: C-Kopf fehlt"; exit 1; }
  echo "PASS referenz: hallo_kern mit C-Kopf, .multiboot2 bytegleich"
}

case "$modus" in
  laufzeitfrei) laufzeitfrei ;;
  kernel) kernel ;;
  referenz) referenz ;;
  alle) laufzeitfrei; referenz; kernel ;;
  *) echo "Unbekannter Modus" >&2; exit 2 ;;
esac
