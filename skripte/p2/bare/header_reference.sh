#!/usr/bin/env bash
set -euo pipefail
bare_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$bare_dir/../../.." && pwd)"
build_dir="$project_dir/build/p2-bare-header"
mkdir -p "$build_dir"
cc -std=gnu11 -O2 -ffreestanding -fno-pie -mno-red-zone -include "$bare_dir/boot_test_decls.h" -c \
    "$project_dir/compiler/runtime/moo_bare_boot.c" -o "$build_dir/boot.o"
cc -std=c11 -O2 -Wall -Wextra -Werror \
    "$bare_dir/mb2_check.c" -o "$build_dir/mb2_check"
readelf -h "$build_dir/boot.o" | awk '/Machine:/ { if ($0 !~ /X86-64/) exit 1; ok=1 } END { if (!ok) exit 1 }'
readelf -SW "$build_dir/boot.o" | awk '$0 ~ /[[:space:]]\.multiboot2[[:space:]]/ { if ($NF != 8) exit 1; ok=1 } END { if (!ok) exit 1 }'
objcopy --dump-section ".multiboot2=$build_dir/header.bin" "$build_dir/boot.o"
"$build_dir/mb2_check" "$build_dir/header.bin"
