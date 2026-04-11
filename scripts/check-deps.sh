#!/usr/bin/env bash
# moo Dependency-Check — Bericht ueber veraltete/unsichere Dependencies.
# Kein Auto-Update. Nur Output.
#
# Nutzung: scripts/check-deps.sh [> /tmp/moo-deps.txt]

set -uo pipefail
cd "$(dirname "$0")/.."

echo "=== moo Dependency-Check $(date +%F) ==="
echo

# --- Rust ---
echo "## Rust Toolchain"
rustc --version 2>&1 || echo "rustc nicht gefunden"
cargo --version 2>&1 || echo "cargo nicht gefunden"
echo

cd compiler 2>/dev/null || { echo "FEHLER: compiler/ nicht gefunden"; exit 1; }

echo "## Cargo Outdated (Veraltete Crates)"
if command -v cargo-outdated &>/dev/null; then
  cargo outdated 2>&1 | head -30
else
  echo "  cargo-outdated nicht installiert (cargo install cargo-outdated)"
fi
echo

echo "## Cargo Audit (Sicherheits-CVEs)"
if command -v cargo-audit &>/dev/null; then
  cargo audit 2>&1 | head -40
else
  echo "  cargo-audit nicht installiert (cargo install cargo-audit)"
fi
echo

echo "## Aktuelle Crate-Versionen (Cargo.lock)"
grep -A1 '^name = "inkwell"\|^name = "clap"\|^name = "cc"' Cargo.lock 2>/dev/null | head -20 || echo "  Cargo.lock nicht lesbar"
echo

# --- C System-Libraries ---
echo "## C System-Libraries (pacman)"
for pkg in sqlite sdl2 glfw mesa curl; do
  ver=$(pacman -Q "$pkg" 2>/dev/null | awk '{print $2}')
  if [[ -n $ver ]]; then
    printf "  %-12s %s\n" "$pkg" "$ver"
  else
    printf "  %-12s NICHT INSTALLIERT\n" "$pkg"
  fi
done
echo

# --- LLVM ---
echo "## LLVM"
llvm-config --version 2>&1 || echo "  llvm-config nicht im PATH"
echo

# --- Build-Sanity ---
echo "## Build-Status (release)"
if [[ -x target/release/moo-compiler ]]; then
  size=$(stat -c%s target/release/moo-compiler 2>/dev/null || echo "?")
  mtime=$(stat -c%y target/release/moo-compiler 2>/dev/null | cut -d. -f1 || echo "?")
  echo "  moo-compiler vorhanden ($size bytes, modified $mtime)"
else
  echo "  KEIN release build — 'cargo build --release' ausfuehren"
fi

echo
echo "=== Ende Bericht ==="
