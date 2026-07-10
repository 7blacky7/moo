#!/usr/bin/env bash
# ============================================================
# KIP-E2b — GPU-/Device-Checkpoint + Cross-Device-Restore.
# Task 64b8cde6. v1-Vertrag: duenne Device<->Host-Schicht um das bestehende
# E2-f32-Checkpointformat (moo_nn_ckpt_speichern) — KEIN zweiter Serializer.
#
# Prueft maschinell (test_e2b_device_ckpt.c):
#   [1] Device-Training K Schritte, fenced Download reflektiert den Schritt.
#   [2] Device-State-Download p*/m/v (G3c mv=2n) -> moo_nn_ckpt_speichern.
#   [3] CPU-Restore: Device-Checkpoint auf reinem Host-Pfad bit-genau ladbar.
#   [4] GPU-Restore: upload + weiter == ununterbrochen (bit-identisch).
#   [5] Cross-Device-Roundtrip upload->download f32 bit-identisch.
#   [6] Alter reiner f32-E2-Checkpoint bleibt ladbar.
#   [7] cpu_fallbacks==0 waehrend der Device-Ops.
#
# Transparent skippbar: ohne libvulkan/GPU -> SKIP (Exit 0, KEIN Beweis). Nur
# auf der inventarisierten GPU (4070 Ti) aussagekraeftig. Ohne ASan (NVIDIA-
# Treiber-Leak-Noise), wie kip_g4_lm.sh / ki-gpu-smoke.sh.
# ============================================================
set -euo pipefail
cd "$(dirname "$0")/../compiler/runtime"

if [ -z "$(ldconfig -p 2>/dev/null | grep libvulkan || true)" ]; then
    echo "SKIP: libvulkan nicht installiert — E2b-Device-Checkpoint nicht bewiesen"
    exit 0
fi

# moo_nn_ckpt_speichern/laden ziehen die volle Runtime; moo_ki_gpu.c mit echtem
# Vulkan-Pfad (-DMOO_HAS_VULKAN + -lvulkan).
clang -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/test_e2b_device_ckpt \
    tests/test_e2b_device_ckpt.c \
    moo_nn.c moo_nn_easy.c moo_json.c moo_tensor.c moo_tensor_ops.c moo_ki_gpu.c \
    moo_autograd.c moo_memory.c moo_value.c moo_print.c moo_string.c moo_dict.c \
    moo_list.c moo_ops.c \
    -lvulkan -lm

/tmp/test_e2b_device_ckpt
echo "kip_e2b: Exit 0 == KIP-E2b GRUEN (Device-Checkpoint via E2-Format, Cross-Device-Restore)"
