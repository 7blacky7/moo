#!/usr/bin/env bash
# kernel-smoke.sh — Plan-010 T1: Toolchain-Smoke fuer den moo-Bare-Metal-Kernel.
# ============================================================================
# PHASEN
#   [1/4] Build: One-Shot-Kernel-Pipeline (P010-C2)
#         moo-compiler compile <kernel.moos> --no-stdlib --target x86_64-bare
#                              --kernel -o hallo_kern.elf
#   [2/4] nm-Gate: KEINE undefined-Symbole, KEINE hosted-Symbole
#         (malloc/free/printf/puts/calloc/realloc/moo_string_new/moo_alloc,
#          pthread_*, @GLIBC, starkes 'T moo_throw' — weak 'W' ist der
#          erlaubte bare-Stub aus moo_bare.c)
#   [3/4] readelf: ELF64 x86-64 EXEC + Multiboot2-Magic in den ersten 32K
#   [4/4] QEMU-Boot-Smoke: GRUB-ISO bauen, 25s booten, beide seriellen Marker
#         asserten: MOO-KERN-OK und MOO-KERN-TICKS-OK.
#         Fehlt Tooling (qemu-system-x86_64 / grub-mkrescue / xorriso):
#         TRANSPARENTER SKIP mit Diagnose, Exit 0 (Projekt-Konvention wie
#         run_sanitize.sh — kein stiller Skip, kein hartes CI-Versagen).
#
# EINORDNUNG (User-Feedback 2026-06-11):
#   * GRUB/Multiboot2 ist hier bewusst nur die pragmatische TESTBRUECKE —
#     KEINE langfristige Architekturentscheidung, kein Lock-in. Eigene
#     moo-Bootloader (Stage1/Stage2/UEFI) sind Roadmap: Plan-Task P011-EPIC.
#   * Fehlendes Tooling ist KEIN fachlicher Blocker — qemu/grub/xorriso sind
#     per Synapse-shell installierbare Test-Infrastruktur (pacman).
#   * Der transparente SKIP ist reine Entwicklerfreundlichkeit, KEIN finaler
#     Beweis: das Final-Gate verlangt den ECHTEN QEMU-Boot mit beiden Markern.
#
# NUTZUNG
#   scripts/kernel-smoke.sh [pfad/zur/kernel.moos]
#   (Default: beispiele/kernel/hallo_kern.moos)
#   MOO_BIN=... ueberschreibt den Compiler-Pfad.
#
# EXIT-CODES: 0 = alle Phasen gruen ODER QEMU sauber geskippt; 1 = Fehler.
# run_all.sh bleibt QEMU-/GPU-frei — dieses Script ist die separate Schiene.
# ============================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MOO_BIN="${MOO_BIN:-$ROOT/compiler/target/release/moo-compiler}"
KERNEL_SRC="${1:-$ROOT/beispiele/kernel/hallo_kern.moos}"

OUT="$(mktemp -d "${TMPDIR:-/tmp}/moo_kernel_smoke.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
ELF="$OUT/hallo_kern.elf"

fail() { echo "FAIL: $*" >&2; exit 1; }

echo "############################################################"
echo "# moo Kernel-Smoke (Plan-010 T1)"
echo "# Kernel : $KERNEL_SRC"
echo "# Compiler: $MOO_BIN"
echo "############################################################"

# --- [1/4] Build ------------------------------------------------------------
echo "[1/4] One-Shot-Build (--kernel) ..."
if [ ! -x "$MOO_BIN" ]; then
  echo "      moo-compiler fehlt — baue via cargo build --release ..."
  (cd "$ROOT/compiler" && cargo build --release) \
    || fail "cargo build --release fehlgeschlagen"
fi
[ -f "$KERNEL_SRC" ] || fail "Kernel-Quelle nicht gefunden: $KERNEL_SRC"

"$MOO_BIN" compile "$KERNEL_SRC" --no-stdlib --target x86_64-bare --kernel \
    -o "$ELF" || fail "Kernel-Pipeline fehlgeschlagen"
[ -f "$ELF" ] || fail "kein ELF erzeugt: $ELF"
echo "      OK: $ELF"

# --- [2/4] nm-Gate ------------------------------------------------------------
echo "[2/4] nm-Gate (hosted-Verbotsliste + keine undefined) ..."
UNDEF="$(nm -u "$ELF" 2>/dev/null || true)"
if [ -n "$UNDEF" ]; then
  echo "$UNDEF" | sed 's/^/      | /'
  fail "undefined-Symbole im Kernel-ELF (siehe oben)"
fi

SYMS="$(nm "$ELF" 2>/dev/null | awk '{print $NF}')"
VERBOTEN="$(printf '%s\n' "$SYMS" \
  | grep -xE 'malloc|free|printf|puts|calloc|realloc|moo_string_new|moo_alloc' || true)"
VERBOTEN2="$(printf '%s\n' "$SYMS" | grep -E '^pthread_|@GLIBC' || true)"
STRONG_THROW="$(nm "$ELF" | grep -E ' T moo_throw$' || true)"   # weak 'W' = bare-Stub, ok
if [ -n "$VERBOTEN$VERBOTEN2$STRONG_THROW" ]; then
  printf '%s\n%s\n%s\n' "$VERBOTEN" "$VERBOTEN2" "$STRONG_THROW" | sed '/^$/d;s/^/      | /'
  fail "hosted-Symbole im Kernel-ELF (siehe oben)"
fi
echo "      OK: keine undefined-, keine hosted-Symbole"

# --- [3/4] readelf + Multiboot2-Magic ----------------------------------------
echo "[3/4] ELF-Header + Multiboot2-Magic ..."
readelf -h "$ELF" | grep -q 'Class:.*ELF64'            || fail "kein ELF64"
readelf -h "$ELF" | grep -q 'Machine:.*X86-64'         || fail "keine X86-64-Maschine"
readelf -h "$ELF" | grep -qE 'Type:.*EXEC'             || fail "kein EXEC-Type"
# MB2-Magic 0xE85250D6 little-endian = d6 50 52 e8, muss in den ersten 32K liegen.
head -c 32768 "$ELF" | od -A d -t x1 | grep -q 'd6 50 52 e8' \
  || fail "Multiboot2-Magic nicht in den ersten 32K"
echo "      OK: ELF64/X86-64/EXEC, MB2-Magic < 32K"

# --- [4/4] QEMU-Boot-Smoke (transparent skippbar) ----------------------------
echo "[4/4] QEMU-Boot-Smoke ..."
MISSING=()
for t in qemu-system-x86_64 grub-mkrescue xorriso; do
  command -v "$t" >/dev/null 2>&1 || MISSING+=("$t")
done
if [ "${#MISSING[@]}" -gt 0 ]; then
  echo "      SKIP: fehlendes Tooling: ${MISSING[*]}"
  echo "      SKIP: Boot-Test uebersprungen (transparent, Exit 0)."
  echo "      SKIP: Dies ist KEIN finaler Beweis — das Final-Gate verlangt den echten Boot."
  echo "      Hinweis (Arch/CachyOS): sudo pacman -S qemu-system-x86 grub xorriso"
  echo "############################################################"
  echo "# ERGEBNIS: Build/nm/MB2 gruen — QEMU-Boot GESKIPPT (Tooling)."
  echo "############################################################"
  exit 0
fi

ISO_DIR="$OUT/iso"
mkdir -p "$ISO_DIR/boot/grub"
cp "$ELF" "$ISO_DIR/boot/hallo_kern.elf"
cat > "$ISO_DIR/boot/grub/grub.cfg" <<'EOF'
set timeout=0
set default=0
menuentry "moo-kernel" {
    multiboot2 /boot/hallo_kern.elf
    boot
}
EOF

grub-mkrescue -o "$OUT/moo.iso" "$ISO_DIR" >"$OUT/grub.log" 2>&1 \
  || { sed 's/^/      | /' "$OUT/grub.log"; fail "grub-mkrescue fehlgeschlagen"; }

SERIAL_LOG="$OUT/serial.log"
# Der Kernel endet in cli/hlt — QEMU laeuft weiter, timeout (rc 124) ist erwartet.
timeout --foreground 25s qemu-system-x86_64 \
    -cdrom "$OUT/moo.iso" -m 128M \
    -display none -no-reboot -serial stdio \
    > "$SERIAL_LOG" 2>&1
QEMU_RC=$?
if [ "$QEMU_RC" -ne 0 ] && [ "$QEMU_RC" -ne 124 ]; then
  sed 's/^/      | /' "$SERIAL_LOG" | tail -30
  fail "qemu-system-x86_64 rc=$QEMU_RC (weder 0 noch timeout)"
fi

grep -q 'MOO-KERN-OK'       "$SERIAL_LOG" || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "Marker MOO-KERN-OK fehlt auf seriell"; }
grep -q 'MOO-KERN-TICKS-OK' "$SERIAL_LOG" || { sed 's/^/      | /' "$SERIAL_LOG" | tail -30; fail "Marker MOO-KERN-TICKS-OK fehlt (PIT-Timer zaehlt nicht?)"; }

echo "      OK: beide Marker auf seriell — Kernel bootet, Timer zaehlt."
echo "      Serielle Ausgabe (Auszug):"
grep -E 'MOO-KERN' "$SERIAL_LOG" | sed 's/^/      | /'
echo "############################################################"
echo "# ERGEBNIS: ALLE 4 Phasen gruen."
echo "############################################################"
exit 0
