#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/../../.." && pwd)"
output_dir="$project_root/build/p2-runtime-bare-stack"
mkdir -p "$output_dir"
for triple in x86_64-unknown-none aarch64-unknown-none; do
    obj="$output_dir/$triple.o"
    clang --target="$triple" -std=c11 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -DMOO_BARE -c \
        "$project_root/compiler/runtime/moo_defer_stack.c" -o "$obj"
    llvm-nm --undefined-only "$obj" | rg 'kern_alloc_c$'
    llvm-nm --undefined-only "$obj" | rg 'kern_frei_c$'
    if llvm-nm --undefined-only "$obj" | rg '(malloc|realloc|free|memcpy|memset)$'; then
        echo "Unerwartetes hosted Speichersymbol in $triple" >&2
        exit 1
    fi
    llvm-readobj --file-headers "$obj" | rg 'EM_X86_64|EM_AARCH64'
done
