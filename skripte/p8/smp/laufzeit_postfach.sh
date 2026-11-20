#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
out="$root/build/p10-t14/postfach"
mkdir -p "$out"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
"$compiler" compile beispiele/kernel/kern_laufzeit_postfach.moos --no-stdlib --emit-ir -o "$out/post.ll"
mise exec -- opt -passes=verify -disable-output "$out/post.ll"
mise exec -- llvm-extract -S --recursive --func=__moo_native.laufzeit_post_neu \
    --func=__moo_native.laufzeit_post_freigeben --func=__moo_native.laufzeit_post_leeren \
    --func=__moo_native.laufzeit_post_schliessen --func=__moo_native.laufzeit_post_uebernehmen \
    --func=__moo_native.laufzeit_post_abgeben "$out/post.ll" -o "$out/probe.ll"
mise exec -- llc -filetype=obj "$out/probe.ll" -o "$out/probe.o"
${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -Werror -pthread -no-pie -fsanitize=address,undefined \
    skripte/p8/smp/laufzeit_postfach_orakel.cpp "$out/probe.o" -o "$out/post-orakel"
ASAN_OPTIONS=detect_leaks=1 "$out/post-orakel"
"$compiler" compile beispiele/kernel/kern_laufzeit_postfach.moos --no-stdlib --target x86_64-bare --emit-ir -o "$out/bare.ll"
mise exec -- opt -passes=verify -disable-output "$out/bare.ll"
echo "PASS Besitz-Handoff Hosted/Bare, ASan/UBSan/LSan-Orakel"
