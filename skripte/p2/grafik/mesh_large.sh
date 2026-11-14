#!/usr/bin/env bash
set -euo pipefail

grafik_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "$grafik_dir/../../.." && pwd)"
build_dir="$project_dir/build/p2-grafik"
mkdir -p -- "$build_dir"
cc -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Werror \
  "$grafik_dir/test_mesh_large.c" "$grafik_dir/mesh_ref.c" "$grafik_dir/mat_ref.c" \
  -o "$build_dir/test_mesh_large"
"$build_dir/test_mesh_large"
