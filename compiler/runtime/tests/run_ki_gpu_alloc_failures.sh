#!/usr/bin/env bash
# Fehlerpfade ohne echte GPU testen; keine Installation oder Aenderung der Runtime.
# Benoetigt GCC (oder CC), Vulkan-Header/-Loader und ASan/UBSan im Compiler.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/moo-ki-alloc-test.XXXXXX")"
trap 'rm -rf -- "$OUT"' EXIT
CC="${CC:-gcc}"
RT="$ROOT/compiler/runtime"
# Pfade sind unabhaengig vom Aufrufverzeichnis.
"$CC" -std=gnu11 -O1 -g -Wall -Wextra -ftrivial-auto-var-init=pattern \
    -fsanitize=address,undefined -fno-omit-frame-pointer -I"$RT" \
    "$RT/tests/test_ki_gpu_alloc_failures.c" -lvulkan -lm -o "$OUT/test"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$OUT/test"
# Ohne Vulkan-Feature bleibt der Stub-Pfad baubar.
"$CC" -std=gnu11 -O2 -Wall -Wextra -I"$RT" -c "$RT/moo_ki_gpu.c" -o "$OUT/stub.o"
echo 'PASS: Build ohne Vulkan.'
