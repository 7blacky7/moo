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
