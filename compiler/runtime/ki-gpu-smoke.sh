#!/usr/bin/env bash
# ============================================================
# GPU2-Smoke (Plan-014): Vulkan-Compute-Ops gegen CPU-Referenzen.
# Transparent skippbar (kein glslc noetig — SPIR-V ist eingebettet;
# kein libvulkan/keine GPU => SKIP). Ein SKIP ist KEIN Beweis —
# run_all.sh bleibt bewusst GPU-frei, dieses Script ist das Gate.
# OHNE ASan: NVIDIA-Treiber-Allokationen erzeugen Leak-Noise.
# ============================================================
set -euo pipefail
cd "$(dirname "$0")"

if [ -z "$(ldconfig -p 2>/dev/null | grep libvulkan || true)" ]; then
    echo "SKIP: libvulkan nicht installiert"
    exit 0
fi

gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/test_ki_gpu tests/test_ki_gpu.c moo_ki_gpu.c -lvulkan -lm

/tmp/test_ki_gpu

# KIP-G1: residente Buffers (Differential + Submit-Zaehler-Beweis + Slot-Reuse)
gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/test_ki_gpu_resident tests/test_ki_gpu_resident.c moo_ki_gpu.c -lvulkan -lm

/tmp/test_ki_gpu_resident

# KIP-G2: Tiled Matmul (Shape-Matrix-Differential + Mikrobenchmark alt vs neu)
gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/test_ki_gpu_matmul tests/test_ki_gpu_matmul.c moo_ki_gpu.c -lvulkan -lm

/tmp/test_ki_gpu_matmul

# KIP-G1 Phase B: Tensor-Dirty-State (valid-Masken-Transitionen + GPU-Download).
# gc-sections droppt ungenutzte Tensor-Funktionen; moo_throw/moo_error gestubbt.
gcc -std=gnu11 -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -DMOO_HAS_VULKAN -I. \
    -o /tmp/test_ki_gpu_dirty tests/test_ki_gpu_dirty.c moo_tensor.c moo_ki_gpu.c -lvulkan -lm

/tmp/test_ki_gpu_dirty
