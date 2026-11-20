#!/usr/bin/env bash
# Wirklicher Moo-Träger; Hostanschluss ersetzt ausschließlich Kernel-Primitiven.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
out="$root/build/p10-t14/host-backend"
mkdir -p "$out/obj"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
sources=(moo_bare moo_value moo_memory moo_string moo_list moo_dict moo_stdlib moo_print
    moo_frame moo_object moo_buffer moo_buffer_index moo_struct_box moo_struct_dyn
    moo_handle moo_defer_stack moo_defer_error moo_surface moo_surface_core)
objects=()
for source in "${sources[@]}"; do
    obj="$out/obj/$source.o"
    ${CC:-cc} -std=c11 -O1 -g -D_GNU_SOURCE -DMOO_UEFI_COLLECTIONS -DMOO_BARE_THREADS \
        -DMOO_HAS_SURFACE -ffunction-sections -fdata-sections -fno-omit-frame-pointer \
        -fsanitize=address,undefined -Icompiler/runtime -c "compiler/runtime/$source.c" -o "$obj"
    objects+=("$obj")
done
api='main,moo_thread_spawn,moo_thread_spawn_n,moo_thread_wait,moo_thread_done,moo_thread_free,moo_channel_new,moo_channel_send,moo_channel_recv,moo_channel_close,moo_channel_uebergeben,moo_channel_free,moo_atom_neu,moo_atom_laden,moo_atom_speichern,moo_atom_addieren,moo_atom_tauschen,moo_atom_vergleichen_und_tauschen,moo_atom_free,moo_sperre_neu,moo_sperre_betreten,moo_sperre_verlassen,moo_sperre_free,moo_throw,moo_throw_ort,moo_try_enter,moo_try_check,moo_try_leave,moo_try_take_error,moo_get_error,moo_fehler_zustand,moo_fehler_unbehandelt,moo_global_fremd,moo_global_fremdzugriff,moo_global_pruefen,moo_native_cleanup_enter,moo_native_cleanup_step,moo_native_cleanup_leave,moo_native_thread_markieren,moo_native_rueckruf_thread,moo_kern_error_flag,moo_kern_last_error,moo_kern_try_depth,moo_kern_fehler_ort,moo_kern_fehler_gemeldet,moo_kern_worker_flag,__moo_native.laufzeit_post_leeren,__moo_native.laufzeit_post_schliessen,__moo_native.laufzeit_post_freigeben,__moo_native.laufzeit_status'
deletes=(--func=__moo_native.laufzeit_kontext --func=__moo_native.smp_planer --func=__moo_native.smp_online_maske --func=__moo_native.smp_faden_anbieten --func=__moo_native.mutex_nimm --func=__moo_native.mutex_gib --func=__moo_native.bedingung_warten --func=__moo_native.bedingung_wecken --func=__moo_native.laufzeit_alle_wecken --func=malloc --func=free --func=realloc --func=calloc --func=moo_uefi_print --func=kern_panic)
for fixture in ${MOO_NATIVE_HOST_FIXTURES:-atom_zaehler sperre_konto warten_mehrfach spiel_kanal starte_mehrere starte_lambda_mehrere}; do
    case "$fixture" in atom_zaehler|sperre_konto|warten_mehrfach|spiel_kanal|starte_mehrere|starte_lambda_mehrere) ;; *) exit 2 ;; esac
    "$compiler" compile "skripte/p8/smp/laufzeit_host_$fixture.moos" --no-stdlib \
        --target x86_64-bare --emit-ir -o "$out/$fixture.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$fixture.ll"
    # Der Compiler erzeugt diese Bodies; nur ihre Testanschlüsse werden ersetzt.
    mise exec -- llvm-extract --delete "${deletes[@]}" --glob=llvm.used \
        "$out/$fixture.ll" -o "$out/$fixture.backend.bc"
    mise exec -- opt -passes=internalize,globaldce -internalize-public-api-list="$api" \
        "$out/$fixture.backend.bc" -o "$out/$fixture.bc"
    mise exec -- opt -passes=verify -disable-output "$out/$fixture.bc"
    mise exec -- llc -filetype=obj -function-sections -data-sections "$out/$fixture.bc" -o "$out/$fixture.o"
    ${CXX:-c++} -std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -no-pie \
        -fsanitize=address,undefined -Wl,--gc-sections skripte/p8/smp/laufzeit_host_backend.cpp \
        "$out/$fixture.o" "${objects[@]}" -lm -o "$out/$fixture"
    ASAN_OPTIONS=detect_leaks=1 timeout 120 "$out/$fixture" >"$out/$fixture.out" 2>"$out/$fixture.err"
    diff -u "compiler/tests/p2/rc_geteilt/$fixture.expected" "$out/$fixture.out"
    grep -F "PASS nativer Moo-Traeger" "$out/$fixture.err"
    echo "PASS $fixture: nativer Moo-Traeger mit C-Kernen/ASan/UBSan/LSan, .expected identisch"
done
