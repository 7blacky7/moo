#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "$0")/../../.." && pwd)"
output_dir="$project_root/build/p2-runtime-windows-cross"
mkdir -p "$output_dir"
cc=x86_64-w64-mingw32-gcc
cxx=x86_64-w64-mingw32-g++
for name in moo_buffer moo_buffer_index moo_struct_box moo_defer_stack moo_defer_error; do
    "$cc" -std=c11 -O2 -Wall -Wextra -Werror -c \
        "$project_root/compiler/runtime/$name.c" -o "$output_dir/$name.obj"
    llvm-readobj --file-headers "$output_dir/$name.obj" | rg 'IMAGE_FILE_MACHINE_AMD64'
done
"$cxx" -std=c++17 -O2 -Wall -Wextra -Werror -c \
    "$project_root/compiler/runtime/tests/test_p2_buffer_cpp.cpp" -o "$output_dir/adapter.obj"
"$cxx" "$output_dir/adapter.obj" "$output_dir/moo_buffer.obj" \
    "$output_dir/moo_struct_box.obj" "$output_dir/moo_defer_stack.obj" "$output_dir/moo_defer_error.obj" -o "$output_dir/adapter.exe"
llvm-nm --defined-only "$output_dir/moo_buffer_index.obj" | rg ' moo_buffer_index_get$'
llvm-nm --defined-only "$output_dir/moo_defer_stack.obj" | rg ' moo_defer_stack_pop$'
llvm-nm --defined-only "$output_dir/adapter.obj" | rg ' p2_typed_struct_cpp_adapter$'
llvm-readobj --file-headers "$output_dir/adapter.exe" | rg 'IMAGE_FILE_MACHINE_AMD64'
