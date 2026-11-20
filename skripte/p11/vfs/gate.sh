#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out=build/p11/vfs
mkdir -p "$out"
fall="${1:-ram}"
case "$fall" in
  ram) quell=vfs_ram; probe=vfs_probe ;;
  fat) quell=vfs_fat; probe=vfs_fat_probe ;;
  *) echo "unbekannte VFS-Probe: $fall" >&2; exit 2 ;;
esac
"$compiler" compile "compiler/tests/p11/$quell.moos" -o "$out/$fall"
"$out/$fall" >"$out/$fall.txt"
diff -u "compiler/tests/p11/$quell.expected" "$out/$fall.txt"
"$compiler" compile "compiler/tests/p11/$probe.moos" --no-stdlib --target x86_64-bare --emit-ir -o "$out/$fall-bare.ll"
mise exec -- opt -passes=verify -disable-output "$out/$fall-bare.ll"
echo "VFS/$fall: Funktionsprobe grün; Bare-IR verifiziert"
