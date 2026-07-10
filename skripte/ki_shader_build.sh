#!/usr/bin/env bash
# ============================================================
# KI-Compute-Shader -> SPIR-V -> eingebetteter C-Header (GPU2).
# Muster: moo_3d_vulkan_shaders.h. Der erzeugte Header wird
# EINGECHECKT — glslc ist nur fuer Shader-Aenderungen noetig.
# ============================================================
set -euo pipefail
cd "$(dirname "$0")/../compiler/runtime"
OUT=moo_ki_gpu_shaders.h
{
  echo "/* AUTOGENERIERT von skripte/ki_shader_build.sh — NICHT von Hand editieren."
  echo " * Quellen: runtime/shader_ki/*.comp (GPU2, Plan-014). */"
} > "$OUT"
for s in matmul matmul_naiv elementwise reduce unary_fwd unary_bw transpose copy grad_accum opt_sgd opt_adam softmax_fwd softmax_bw ce_fwd ce_bw norm_fwd norm_bw reduce_axis broadcast reduce_max_bw gather scatter_add; do
  glslc -O -fshader-stage=compute "shader_ki/$s.comp" -o "/tmp/ki_$s.spv"
  xxd -i -n "ki_${s}_spv" "/tmp/ki_$s.spv" >> "$OUT"
  rm -f "/tmp/ki_$s.spv"
done
echo "OK: $OUT erzeugt"
