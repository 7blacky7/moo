#!/usr/bin/env bash
# ============================================================
# KIP-G3d Coverage — das G4-Start-Gate (docs/kip/G0-gpu-vertrag-inventur.md §3).
# Fuehrt einen residenten M-A-Trainingsschritt (gather -> RMSNorm -> matmul ->
# fused CE; vollstaendiger Backward; grad_accum; Adam) ueber >= 2 Schritte aus
# und prueft maschinell die sechs Erfolgskriterien (Exit 0, >=2 F+B+Opt-
# Schritte, isfinite, cpu_fallbacks==0, erwartete Op-Positivliste via Submit-
# Zahl, uploads/downloads nur im Randbereich).
#
# MOO_KI_GPU_ERZWINGEN=1 ignoriert die Groessen-Schwellen (kleines Coverage-
# Modell). MOO_KI_GPU_STRIKT=1 = der G4-Hotpath-Vertrag (CPU-Fallback = Fehler);
# der Harness prueft cpu_fallbacks==0 zusaetzlich explizit.
#
# Transparent skippbar: ohne libvulkan/GPU -> SKIP (Exit 0, KEIN Beweis). Nur
# auf der inventarisierten GPU (4070 Ti) aussagekraeftig. Ohne ASan (NVIDIA-
# Treiber-Leak-Noise), wie ki-gpu-smoke.sh.
# ============================================================
set -euo pipefail
cd "$(dirname "$0")/../compiler/runtime"

if [ -z "$(ldconfig -p 2>/dev/null | grep libvulkan || true)" ]; then
    echo "SKIP: libvulkan nicht installiert — G3d-Coverage nicht bewiesen"
    exit 0
fi

gcc -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. \
    -o /tmp/ki_gpu_coverage tests/ki_gpu_coverage.c moo_ki_gpu.c -lvulkan -lm

MOO_KI_GPU_ERZWINGEN=1 MOO_KI_GPU_STRIKT=1 /tmp/ki_gpu_coverage
echo "kip_gpu_coverage: Exit 0 == G3d-Coverage GRUEN (G4 entsperrt)"
