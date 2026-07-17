#!/usr/bin/env bash
# ============================================================
# KI-MULTI-V3c Teil 2 — GPU-residentes multimodales Trainings-PoC.
#
# Fuehrt den multimodalen Trainingsschritt KOMPLETT GPU-RESIDENT ueber mehrere
# SGD-Schritte aus und prueft maschinell (Muster: skripte/kip_g4_lm.sh):
#   e_inj [1,ENC] (frozen, einmal hochgeladen) -> Projektion dicht ENC->D
#   (trainierbar) -> CONCAT als Praefix vor T Text-Token-Embeddings (Gather,
#   trainierbar) -> additive Positionen -> 1 Transformer-Block -> Final-Norm ->
#   Head -> MASKIERTE CE (Maske [0]+[1]*T) -> Backward -> SGD.
#
# Gates:
#   [1] FD-Gradcheck der (double) CPU-Referenz — Backward-Mathe inkl. Projektion P.
#   [3] GPU-Loss (float) == CPU-Referenz-Loss (double) je Schritt (rel < 2e-3).
#   [4] Determinismus: zwei GPU-Laeufe bit-identisch.
#   [5] Residenz-Telemetrie: cpu_fallbacks==0, uploads==0 im Loop (e_inj +
#       Konstanten + Params VOR dem Loop), downloads==STEPS (nur CE-Loss),
#       submits konstant je Schritt.
#
# DOKUMENTIERTE ABWEICHUNG (ehrlich): Der Transformer-Block nutzt den in KIP-G4
# bit-genau verifizierten Baustein (RMSNorm ohne Affine + Single-Head-Attention
# + SwiGLU-FFN), NICHT LayerNorm/GELU. Beide waeren resident, aber der G4-Block
# ist der abgesicherte Pfad — der V3c-Kernbeweis (Praefix-Concat via copy_res,
# maskierte CE, Projektions-Gradient) sitzt DRUMHERUM und ist davon unberuehrt.
#
# Transparent skippbar: ohne libvulkan/GPU -> SKIP (Exit 0, KEIN Beweis). Nur
# auf der inventarisierten GPU (4070 Ti) aussagekraeftig. Ohne ASan (NVIDIA-
# Treiber-Leak-Noise), wie ki-gpu-smoke.sh / kip_g4_lm.sh.
#
# Dims via -DV3C_* ueberschreibbar; Default = Korrektheits-Lauf.
# ============================================================
set -euo pipefail
cd "$(dirname "$0")/../compiler/runtime"

if [ -z "$(ldconfig -p 2>/dev/null | grep libvulkan || true)" ]; then
    echo "SKIP: libvulkan nicht installiert — V3c-GPU-PoC nicht bewiesen"
    exit 0
fi

gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/ki_gpu_v3c tests/ki_gpu_v3c.c moo_ki_gpu.c -lvulkan -lm

MOO_KI_GPU_ERZWINGEN=1 MOO_KI_GPU_STRIKT=1 /tmp/ki_gpu_v3c
echo "kip_v3c_gpu: Exit 0 == V3c GRUEN (residente Praefix-Injection + maskierte CE + Projektions-Gradient == CPU, deterministisch, resident)"
