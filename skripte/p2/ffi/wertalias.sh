#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-wertalias
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
timeout 60s "$compiler" compile compiler/tests/p2/ffi_defer_reach_wertalias.moos --emit-ir -o "$out/probe.ll"
mise exec -- opt -passes=verify -disable-output "$out/probe.ll"
grep -Eq '^define .*@probe\(' "$out/probe.ll"
if sed -n '/^define .*@probe(/,/^}/p' "$out/probe.ll" | grep -Fq '@moo_defer_stack_push'; then printf 'DISPATCHER_AKTIV\n'; else printf 'FALLBACK\n'; fi
timeout 60s "$compiler" compile compiler/tests/p2/ffi_defer_reach_wertalias.moos -o "$out/probe"
timeout 30s "$out/probe"
