#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../compiler/runtime"
gcc -std=gnu11 -O3 -DMOO_HAS_VULKAN -I. -o /tmp/v2b_bench tests/ki_gpu_v2b_bench.c moo_ki_gpu.c -lvulkan -lm
MOO_KI_GPU_ERZWINGEN=1 /tmp/v2b_bench
