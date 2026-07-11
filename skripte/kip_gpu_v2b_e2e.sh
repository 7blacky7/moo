#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../compiler/runtime"
gcc -std=gnu11 -O1 -g -Wall -Wextra -DMOO_HAS_VULKAN -I. -o /tmp/v2b_tensor \
 tests/test_v2b_tensor_e2e.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c \
 moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c -lvulkan -lm
MOO_KI_GPU_ERZWINGEN=1 MOO_KI_GPU_STRIKT=1 /tmp/v2b_tensor
