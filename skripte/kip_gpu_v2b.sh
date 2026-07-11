#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../compiler/runtime"
if ! ldconfig -p 2>/dev/null | grep libvulkan >/dev/null; then
  echo "SKIP: libvulkan fehlt — V2b nicht bewiesen"; exit 77
fi
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -DMOO_HAS_VULKAN -I. \
  -o /tmp/ki_gpu_v2b tests/test_ki_gpu_conv_pool.c moo_ki_gpu.c -lvulkan -lm
MOO_KI_GPU_ERZWINGEN=1 MOO_KI_GPU_STRIKT=1 /tmp/ki_gpu_v2b
