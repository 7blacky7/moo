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

# KIP-G3d-a: unaere/Skalar/Aktivierungs-Ops (Fwd+Grad Differential vs CPU)
gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/test_ki_gpu_unary tests/test_ki_gpu_unary.c moo_ki_gpu.c -lvulkan -lm

/tmp/test_ki_gpu_unary

# KIP-G3d-c: Layout-Ops transpose/reshape/concat (Fwd+Bwd Differential, bit-exakt)
gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/test_ki_gpu_layout tests/test_ki_gpu_layout.c moo_ki_gpu.c -lvulkan -lm

/tmp/test_ki_gpu_layout

# KIP-G3c: grad_accum + Optimizer-Schritt SGD/Adam/AdamW (Differential vs CPU, mehrere Schritte)
gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/test_ki_gpu_optim tests/test_ki_gpu_optim.c moo_ki_gpu.c -lvulkan -lm

/tmp/test_ki_gpu_optim

# KIP-G3a: Softmax/LogSoftmax + fused CE (Fwd+Bwd Differential vs CPU)
gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/test_ki_gpu_softmax tests/test_ki_gpu_softmax.c moo_ki_gpu.c -lvulkan -lm

/tmp/test_ki_gpu_softmax

# KIP-G3b: LayerNorm/RMSNorm-Kern (Fwd+Bwd Differential vs CPU + FD-Gradcheck)
gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/test_ki_gpu_norm tests/test_ki_gpu_norm.c moo_ki_gpu.c -lvulkan -lm

/tmp/test_ki_gpu_norm

# KIP-G3d-b: Achsen-Reduktion + Broadcast + Max-Subgradient (Differential vs CPU)
gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/test_ki_gpu_reduce tests/test_ki_gpu_reduce.c moo_ki_gpu.c -lvulkan -lm

/tmp/test_ki_gpu_reduce

# KIP-G3d-d: gather + deterministische scatter-add (Differential vs CPU + Determinismus)
gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/test_ki_gpu_gather tests/test_ki_gpu_gather.c moo_ki_gpu.c -lvulkan -lm

/tmp/test_ki_gpu_gather

# KIP-G3d-e: matmul-Backward (Komposition, dA/dB Differential + grad_accum Fan-out)
gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/test_ki_gpu_matmul_bw tests/test_ki_gpu_matmul_bw.c moo_ki_gpu.c -lvulkan -lm

/tmp/test_ki_gpu_matmul_bw

# KIP-G4b: strided RoPE-Paarrotation + Kopf-Slice (MHA/GQA/MQA) — Fwd/Bwd
# Differential vs double-Referenz, Pos-0-Identitaet, Orthogonalitaets-Roundtrip.
gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/test_ki_gpu_rope tests/test_ki_gpu_rope.c moo_ki_gpu.c -lvulkan -lm

/tmp/test_ki_gpu_rope

# KIP-G1 Phase B: Tensor-Dirty-State (valid-Masken-Transitionen + GPU-Download).
# gc-sections droppt ungenutzte Tensor-Funktionen; moo_throw/moo_error gestubbt.
gcc -std=gnu11 -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -DMOO_HAS_VULKAN -I. \
    -o /tmp/test_ki_gpu_dirty tests/test_ki_gpu_dirty.c moo_tensor.c moo_ki_gpu.c -lvulkan -lm

/tmp/test_ki_gpu_dirty
