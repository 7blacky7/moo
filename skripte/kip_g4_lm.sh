#!/usr/bin/env bash
# ============================================================
# KIP-G4 — GPU-residentes Trainings-PoC (Mini-LM komplett auf GPU).
# Design: docs/kip/G4-residentes-training-poc-design.md.
#
# Fuehrt einen echten Transformer-LM (Embedding-Gather + additive Positionen +
# L Layer [RMSNorm -> kausale Single-Head-Attention -> Residual -> RMSNorm ->
# SwiGLU-FFN -> Residual] -> RMSNorm -> Head -> fused CE) KOMPLETT GPU-RESIDENT
# ueber mehrere Adam-Schritte aus und prueft maschinell:
#   [1] FD-Gradcheck der (double) CPU-Referenz — Backward-Mathe unabhaengig.
#   [3] GPU-Loss (float) == CPU-Referenz-Loss (double) je Schritt (rel < 2e-3).
#   [4] Determinismus: zwei GPU-Laeufe bit-identisch.
#   [5] Residenz-Telemetrie: cpu_fallbacks==0, uploads==0 im Loop,
#       downloads==STEPS (nur CE-Loss raus), submits konstant je Schritt.
#   [6] Speedup + Submit-Statistik protokolliert.
#
# Transparent skippbar: ohne libvulkan/GPU -> SKIP (Exit 0, KEIN Beweis). Nur
# auf der inventarisierten GPU (4070 Ti) aussagekraeftig. Ohne ASan (NVIDIA-
# Treiber-Leak-Noise), wie ki-gpu-smoke.sh / kip_gpu_coverage.sh.
#
# Dims via -D ueberschreibbar; Default = Korrektheits-Lauf. Fuer einen P0-nahen
# Speedup-Lauf (dim 256, 6 Layer) siehe Kommentar am Ende.
# ============================================================
set -euo pipefail
cd "$(dirname "$0")/../compiler/runtime"

if [ -z "$(ldconfig -p 2>/dev/null | grep libvulkan || true)" ]; then
    echo "SKIP: libvulkan nicht installiert — G4-PoC nicht bewiesen"
    exit 0
fi

gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/ki_gpu_g4_lm tests/ki_gpu_g4_lm.c moo_ki_gpu.c -lvulkan -lm

/tmp/ki_gpu_g4_lm
echo "kip_g4_lm: Exit 0 == KIP-G4 GRUEN (residentes LM-Training == CPU, deterministisch, resident)"

# P0-naher Speedup-Lauf (langsame double-CPU-Referenz, daher nicht im Default-Gate):
#   gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -DG4_D=256 -DG4_T=256 -DG4_V=256 \
#       -DG4_F=512 -DG4_L=6 -DG4_STEPS=3 -DG4_FD_SAMPLES=2 -I. \
#       -o /tmp/ki_gpu_g4_big tests/ki_gpu_g4_lm.c moo_ki_gpu.c -lvulkan -lm && /tmp/ki_gpu_g4_big
