#!/usr/bin/env bash
set -euo pipefail
bare_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$bare_dir/../../.." && pwd)"
moo_bin="$project_dir/compiler/target/release/moo-compiler"
if [ ! -x "$moo_bin" ]; then
    echo "Kernel-Referenz: Compiler fehlt; kein automatischer Compiler-Build." >&2
    exit 1
fi
for tool in qemu-system-x86_64 grub-mkrescue xorriso; do
    command -v "$tool" >/dev/null || { echo "Kernel-Referenz: $tool fehlt." >&2; exit 1; }
done
mkdir -p "$project_dir/build/p2-bare-tmp"
export TMPDIR="$project_dir/build/p2-bare-tmp"
export MOO_BIN="$moo_bin"
bash "$project_dir/scripts/kernel-smoke.sh"
