/**
 * moo_ki_gpu_statistik.c — KIP-FINAL-FIX (e413b176, GPT-Gegenreview KI-PROD-01).
 * ============================================================================
 * Ausgelagert aus moo_ki_gpu.c (KIP-G4c Punkt 5, docs/kip/G4c-production-
 * wiring-plan.md §3.2): MooValue-Wrapper um MooKiGpuTelemetrie fuer die
 * moo-Builtins gpu_statistik()/gpu_statistik_reset() (runtime_bindings.rs/
 * codegen.rs).
 *
 * WARUM EINE EIGENE DATEI: moo_ki_gpu.c ist bewusst schlank gehalten und
 * wird von mehreren Standalone-GPU-Gate-Skripten (skripte/kip_gpu_coverage.sh,
 * kip_g4_lm.sh, kip_g4b_lm.sh, compiler/runtime/ki-gpu-smoke.sh) NUR zusammen
 * mit der jeweiligen Testquelle + Vulkan/libm gelinkt -- OHNE die restliche
 * Runtime (kein moo_dict.c/moo_string.c/moo_value.c). Die hier verschobenen
 * Funktionen brauchen moo_dict_new/moo_string_new/moo_number/moo_dict_set/
 * moo_none, also echte Runtime-Symbole -- lagen sie in moo_ki_gpu.c, zog
 * JEDE Standalone-GPU-Kompilation diese Symbole als undefined references
 * nach sich (real aufgetreten, GPT-Gegenreview KI-PROD-01, HEAD 344162f).
 *
 * Diese Datei wird NUR vom Vollruntime-Build (compiler/build.rs) mitgebaut,
 * NICHT von den Standalone-GPU-Skripten -- dort ist gpu_statistik() ueber
 * die volle moo-Sprache ohnehin nicht erreichbar (kein .moos-Programm-Kontext).
 * Verhalten von moo_ki_gpu_statistik()/_reset() ist UNVERAENDERT: reiner
 * Wrapper um die branch-unabhaengigen moo_ki_gpu_telemetrie()/_reset() aus
 * moo_ki_gpu.c (funktionieren identisch mit und ohne MOO_HAS_VULKAN).
 * ============================================================================
 */
#include "moo_runtime.h"
#include "moo_ki_gpu_api.h"

MooValue moo_ki_gpu_statistik(void) {
    MooKiGpuTelemetrie tel;
    moo_ki_gpu_telemetrie(&tel);
    MooValue d = moo_dict_new();
    moo_dict_set(d, moo_string_new("submits"), moo_number((double)tel.submits));
    moo_dict_set(d, moo_string_new("uploads"), moo_number((double)tel.uploads));
    moo_dict_set(d, moo_string_new("downloads"), moo_number((double)tel.downloads));
    moo_dict_set(d, moo_string_new("cpu_fallbacks"), moo_number((double)tel.cpu_fallbacks));
    return d;
}

MooValue moo_ki_gpu_statistik_reset(void) {
    moo_ki_gpu_telemetrie_reset();
    return moo_none();
}
