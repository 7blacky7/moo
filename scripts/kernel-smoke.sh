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
#   MOO_KERNEL_GRUB_DIR und MOO_KERNEL_BIOS legen die GRUB-Plattform und das BIOS fest.
#   MOO_KERNEL_OUT=<dir> behaelt ELF, ISO und Bootlogs fuer CI und Reproduktion.
#
# EXIT-CODES: 0 = alle Phasen gruen ODER QEMU sauber geskippt; 1 = Fehler.
# run_all.sh bleibt QEMU-/GPU-frei — dieses Script ist die separate Schiene.
# ============================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MOO_BIN="${MOO_BIN:-$ROOT/compiler/target/release/moo-compiler}"
KERNEL_SRC="${1:-$ROOT/beispiele/kernel/hallo_kern.moos}"

OUT="${MOO_KERNEL_OUT:-}"
if [ -n "$OUT" ]; then
  mkdir -p "$OUT" || exit 1
else
  OUT="$(mktemp -d "${TMPDIR:-/tmp}/moo_kernel_smoke.XXXXXX")" || exit 1
  trap 'rm -rf "$OUT"' EXIT
fi
ELF="$OUT/hallo_kern.elf"

fail() { echo "FAIL: $*" >&2; exit 1; }

# P7-T21: derselbe Smoke-Einstieg fuer zwei echte CPUs ohne FPU.
# Builtins-Archiv passend zum Ziel explizit angeben (libgcc oder compiler-rt).
# Beispiel: KERNEL_OHNE_FPU=arm MOO_SOFTFLOAT_BUILTINS=/.../libclang_rt.builtins-armv7m.a
if [ -n "${KERNEL_OHNE_FPU:-}" ]; then
  [ -x "$MOO_BIN" ] || fail "Compiler fehlt: $MOO_BIN"
  [ -f "${MOO_SOFTFLOAT_BUILTINS:-}" ] || fail "MOO_SOFTFLOAT_BUILTINS fehlt"
  FP_OUT="${MOO_OHNE_FPU_OUT:-$OUT}"
  mkdir -p "$FP_OUT" || fail "Ausgabeordner"
  FP_FLAGS=()
  case "$KERNEL_OHNE_FPU" in
    arm)
      FP_TARGET=thumbv7m-none-eabi
      FP_FLAGS=(--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -mfloat-abi=soft)
      FP_QEMU=("${MOO_OHNE_FPU_QEMU:-qemu-system-arm}" -M lm3s6965evb -cpu cortex-m3
               -display none -monitor none -serial none -semihosting-config enable=on,target=native)
      ;;
    rv64)
      FP_TARGET=riscv64-bare
      FP_FLAGS=(--target=riscv64-none-elf -march=rv64imac -mabi=lp64 -mcmodel=medany -msmall-data-limit=0)
      FP_QEMU=("${MOO_OHNE_FPU_QEMU:-qemu-system-riscv64}" -M virt -m 128M -smp 1
               -cpu rv64,f=false,d=false,zfa=false -bios none -display none -monitor none -serial stdio)
      ;;
    *) fail "KERNEL_OHNE_FPU erwartet arm oder rv64";;
  esac
  for tool in clang ld.lld llvm-readelf llvm-objdump timeout "${FP_QEMU[0]}"; do
    command -v "$tool" >/dev/null || fail "Werkzeug fehlt: $tool"
  done
  "$MOO_BIN" compile "$ROOT/compiler/tests/p7/bare_ohne_fpu.moos" --no-stdlib \
      --target "$FP_TARGET" -o "$FP_OUT/moo.o" || fail "Moo ohne FPU"
  clang "${FP_FLAGS[@]}" -O2 -ffreestanding -fno-builtin -ffunction-sections -fdata-sections \
      -DMOO_BARE -c "$ROOT/compiler/runtime/moo_bare.c" -o "$FP_OUT/runtime.o" || fail "Bare-Runtime"
  clang "${FP_FLAGS[@]}" -O2 -ffreestanding -fno-builtin -ffunction-sections -fdata-sections \
      -c "$ROOT/compiler/tests/p7/bare_ohne_fpu_probe.c" -o "$FP_OUT/probe.o" || fail "Ergebnisadapter"
  clang "${FP_FLAGS[@]}" -c "$ROOT/compiler/tests/p7/bare_ohne_fpu_start.S" \
      -o "$FP_OUT/start.o" || fail "Startup"
  clang "${FP_FLAGS[@]}" -E -P -x c "$ROOT/compiler/tests/p7/bare_ohne_fpu.ld" \
      -o "$FP_OUT/link.ld" || fail "Linkerscript"
  ld.lld --gc-sections -T "$FP_OUT/link.ld" "$FP_OUT/start.o" "$FP_OUT/moo.o" \
      "$FP_OUT/runtime.o" "$FP_OUT/probe.o" "$MOO_SOFTFLOAT_BUILTINS" \
      -o "$FP_OUT/test.elf" || fail "Softfloat-Link"
  llvm-readelf -h -A "$FP_OUT/test.elf" > "$FP_OUT/elf.txt" || fail "ELF"
  llvm-objdump -d "$FP_OUT/test.elf" > "$FP_OUT/disasm.txt" || fail "Disassembly"
  timeout 20s "${FP_QEMU[@]}" -kernel "$FP_OUT/test.elf" > "$FP_OUT/qemu.log" 2>&1
  FP_RC=$?
  cat "$FP_OUT/qemu.log"
  [ "$FP_RC" = 0 ] || fail "QEMU ohne FPU: RC=$FP_RC"
  grep -qx 'MOO-OHNE-FPU PASS 13' "$FP_OUT/qemu.log" || fail "13 Ergebnisse nicht bestaetigt"
  echo "PASS: $KERNEL_OHNE_FPU ohne Hardware-FPU, echte Bare-Runtime."
  exit 0
fi

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

# KERNEL_ARGS: zusaetzliche Compiler-Argumente (z. B. --lib-pfad DIR), P2-T43.
read -r -a KERNEL_EXTRA <<<"${KERNEL_ARGS:-}"
"$MOO_BIN" compile "$KERNEL_SRC" --no-stdlib --target x86_64-bare --kernel \
    "${KERNEL_EXTRA[@]}" -o "$ELF" || fail "Kernel-Pipeline fehlgeschlagen"
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
mkdir -p "$ISO_DIR/boot/grub" || fail "ISO-Verzeichnis"
cp -- "$ELF" "$ISO_DIR/boot/hallo_kern.elf" || fail "Kern ins ISO-Verzeichnis kopieren"
cat > "$ISO_DIR/boot/grub/grub.cfg" <<'EOF'
insmod serial
serial --unit=0 --speed=115200
terminal_output serial
set timeout=0
set default=0
menuentry "moo-kernel" {
    echo MOO-GRUB-MULTIBOOT2
    multiboot2 /boot/hallo_kern.elf
    boot
}
EOF

GRUB_PLATFORM=()
if [ -n "${MOO_KERNEL_GRUB_DIR:-}" ]; then
  [ -r "$MOO_KERNEL_GRUB_DIR/modinfo.sh" ] || fail "GRUB-Plattform fehlt: $MOO_KERNEL_GRUB_DIR"
  GRUB_PLATFORM=(-d "$MOO_KERNEL_GRUB_DIR")
fi
grub-mkrescue "${GRUB_PLATFORM[@]}" -o "$OUT/moo.iso" "$ISO_DIR" >"$OUT/grub.log" 2>&1 \
  || { sed 's/^/      | /' "$OUT/grub.log"; fail "grub-mkrescue fehlgeschlagen"; }

SERIAL_LOG="$OUT/serial.log"
QEMU_LOG="$OUT/qemu.log"
RESET_LOG="$OUT/qemu-reset.log"
: >"$SERIAL_LOG" || fail "Serielles Log kann nicht angelegt werden"
: >"$RESET_LOG" || fail "Reset-Log kann nicht angelegt werden"
QEMU_BIOS=()
if [ -n "${MOO_KERNEL_BIOS:-}" ]; then
  [ -r "$MOO_KERNEL_BIOS" ] || fail "BIOS-Image fehlt: $MOO_KERNEL_BIOS"
  QEMU_BIOS=(-bios "$MOO_KERNEL_BIOS")
fi
# Bei einem Kernel in cli/hlt ist der Timeout (rc 124) erwartet.
QEMU_ACCEL=(-accel tcg)
[ "${MOO_KERNEL_KVM:-0}" != 1 ] || QEMU_ACCEL=(-enable-kvm -cpu host)
timeout --foreground 25s qemu-system-x86_64 "${QEMU_ACCEL[@]}" "${QEMU_BIOS[@]}" \
    -boot order=d -nic none -cdrom "$OUT/moo.iso" -m 128M \
    -display none -no-reboot -monitor none -serial "file:$SERIAL_LOG" \
    -d cpu_reset -D "$RESET_LOG" >"$QEMU_LOG" 2>&1
QEMU_RC=$?

boot_fehler() {
  printf '      QEMU rc=%s\n' "$QEMU_RC"
  for log in "$OUT/grub.log" "$QEMU_LOG" "$RESET_LOG" "$SERIAL_LOG"; do
    printf '      %s\n' "$log"
    tail -30 "$log" | sed 's/^/      | /'
  done
  fail "$1"
}

if [ "$QEMU_RC" -ne 0 ] && [ "$QEMU_RC" -ne 124 ]; then
  boot_fehler "qemu-system-x86_64 beendet"
fi

if [ -n "${KERNEL_MARKER:-}" ]; then
  # P2-T43: eigene Marker, je Eintrag ein ERE fuer eine ganze serielle Zeile.
  read -r -a MARKER <<<"$KERNEL_MARKER"
  for m in "${MARKER[@]}"; do
    tr -d '\r' <"$SERIAL_LOG" | grep -Eq "^${m}\$" \
      || boot_fehler "Marker $m fehlt auf seriell"
    echo "      OK: Marker $m"
  done
else
grep -q 'MOO-KERN-OK'       "$SERIAL_LOG" || boot_fehler "Marker MOO-KERN-OK fehlt auf seriell"
grep -q 'MOO-KERN-TICKS-OK' "$SERIAL_LOG" || boot_fehler "Marker MOO-KERN-TICKS-OK fehlt (PIT-Timer zaehlt nicht?)"

echo "      OK: beide Marker auf seriell — Kernel bootet, Timer zaehlt."
echo "      Serielle Ausgabe (Auszug):"
grep -E 'MOO-KERN' "$SERIAL_LOG" | sed 's/^/      | /'
fi
if [ -n "${KERNEL_MARKER_FEHLT:-}" ]; then
  # P2-T73: Marker, die NICHT erscheinen duerfen (Trap-Vertrag: nach einem
  # Laufzeitfehler darf der Kernel nicht weiterlaufen); je Eintrag ein ERE.
  read -r -a FEHLT <<<"$KERNEL_MARKER_FEHLT"
  for m in "${FEHLT[@]}"; do
    if tr -d '\r' <"$SERIAL_LOG" | grep -Eq "^${m}\$"; then
      sed 's/^/      | /' "$SERIAL_LOG" | tail -30
      fail "Marker $m erschien auf seriell, obwohl der Kernel vorher anhalten muss"
    fi
    echo "      OK: Marker $m fehlt wie erwartet"
  done
fi
echo "############################################################"
echo "# ERGEBNIS: ALLE 4 Phasen gruen."
echo "############################################################"
exit 0
