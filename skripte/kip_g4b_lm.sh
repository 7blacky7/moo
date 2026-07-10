#!/usr/bin/env bash
# ============================================================
# KIP-G4b — GPU-residentes Trainings-PoC mit ECHTER Multi-Head/GQA-Attention
# + RoPE (loest zwei G4-Vereinfachungen auf: Single-Head -> H-Head/HKV-KV-GQA
# via strided head_slice; additive Pos -> interleaved RoPE via rope_res).
# g4 (kip_g4_lm.sh) bleibt als Single-Head/additive-Pos-Regressions-Anker.
#
# Prueft maschinell (wie G4):
#   [1] FD-Gradcheck der (double) CPU-Referenz — Backward-Mathe unabhaengig
#       (inkl. GQA-Gradakkumulation + RoPE-Backward).
#   [3] GPU-Loss (float) == CPU-Referenz-Loss (double) je Schritt (rel < 2e-3).
#   [4] Determinismus: zwei GPU-Laeufe bit-identisch.
#   [5] Residenz-Telemetrie: cpu_fallbacks==0, uploads==0 im Loop,
#       downloads==STEPS (nur CE-Loss raus), submits konstant je Schritt.
#
# Transparent skippbar: ohne libvulkan/GPU -> SKIP (Exit 0, KEIN Beweis). Nur
# auf der inventarisierten GPU (4070 Ti) aussagekraeftig. Ohne ASan (NVIDIA-
# Treiber-Leak-Noise), wie ki-gpu-smoke.sh / kip_g4_lm.sh.
#
# Dims via -D ueberschreibbar; Default = Korrektheits-Lauf. Constraints:
# D%H==0, head_dim gerade, H%HKV==0 (per _Static_assert im Harness geprueft).
# ============================================================
set -euo pipefail
cd "$(dirname "$0")/../compiler/runtime"

if [ -z "$(ldconfig -p 2>/dev/null | grep libvulkan || true)" ]; then
    echo "SKIP: libvulkan nicht installiert — G4b-PoC nicht bewiesen"
    exit 0
fi

gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/ki_gpu_g4b_lm tests/ki_gpu_g4b_lm.c moo_ki_gpu.c -lvulkan -lm

/tmp/ki_gpu_g4b_lm
echo "kip_g4b_lm: Exit 0 == KIP-G4b GRUEN (residentes MHA/GQA+RoPE-Training == CPU, deterministisch, resident)"
