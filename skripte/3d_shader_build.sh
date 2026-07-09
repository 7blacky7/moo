#!/usr/bin/env bash
# ============================================================
# Baut die 3D-Vulkan-Shader: GLSL -> SPIR-V -> eingecheckte C-Header.
# Die erzeugten Header werden EINGECHECKT — glslc ist nur fuer
# Shader-AENDERUNGEN noetig (Vorbild: skripte/ki_shader_build.sh).
# ============================================================
set -euo pipefail
cd "$(dirname "$0")/.."

glslc -O compiler/runtime/shader_3d/vulkan_3d.vert -o /tmp/moo_3d_vert.spv
glslc -O compiler/runtime/shader_3d/vulkan_3d.frag -o /tmp/moo_3d_frag.spv

{
  echo "/* Auto-generiert von skripte/3d_shader_build.sh — NICHT von Hand editieren."
  echo " * Quelle: compiler/runtime/shader_3d/vulkan_3d.vert */"
  xxd -i -n vulkan_vert_spv /tmp/moo_3d_vert.spv
} > compiler/runtime/moo_3d_vulkan_vert_spv.h

{
  echo "/* Auto-generiert von skripte/3d_shader_build.sh — NICHT von Hand editieren."
  echo " * Quelle: compiler/runtime/shader_3d/vulkan_3d.frag */"
  xxd -i -n vulkan_frag_spv /tmp/moo_3d_frag.spv
} > compiler/runtime/moo_3d_vulkan_frag_spv.h

echo "OK: moo_3d_vulkan_vert_spv.h + moo_3d_vulkan_frag_spv.h regeneriert."
