#!/usr/bin/env bash
# P2-T112: Korrektheits-Stresstest fuer geteilte Heap-Werte zwischen Threads.
# Aufruf: rc_geteilt_stress.sh COMPILER ARBEITSVERZ [WIEDERHOLUNGEN] [ETIKETT]
# Kompiliert die Fixtures aus compiler/tests/p2/rc_geteilt und build/opus-t112/prog
# (mt_geteilt) und laeuft jede N-mal; zaehlt korrekte Ausgaben.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
comp="$1"; work="$2"; n="${3:-20}"; tag="${4:-lauf}"
[[ -f "$comp" ]] || comp="$comp.exe"
comp="$(cd "$(dirname "$comp")" && pwd)/$(basename "$comp")"
mkdir -p "$work"
work="$(cd "$work" && pwd)"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) exe=".exe" ;; *) exe="" ;; esac
if [[ "${RC_THREAD_START_TEST:-0}" == 1 ]]; then
  # Vorhandener Runtime-Override; ausschliesslich das Test-Threadobjekt ersetzen.
  thread_native_path() {
    if [[ "$exe" == .exe ]]; then cygpath -m "$1"; else printf '%s' "$1"; fi
  }
  thread_basis="${MOO_RUNTIME_LIB:-}"
  if [[ -z "$thread_basis" ]]; then
    thread_kandidaten=()
    while IFS= read -r p; do thread_kandidaten+=("$p"); done < <(
      find "$(dirname "$comp")/build" -type f \
        \( -name libmoo_runtime.a -o -name moo_runtime.lib \) \
        -newer "$ROOT/compiler/runtime/moo_thread.c" 2>/dev/null)
    [[ ${#thread_kandidaten[@]} -eq 1 ]] || {
      echo "Runtime-Archiv nicht eindeutig gefunden (gesucht: Compiler-build/out); MOO_RUNTIME_LIB setzen" >&2; exit 2; }
    thread_basis="${thread_kandidaten[0]}"
  fi
  [[ "$exe" != .exe ]] || thread_basis="$(cygpath -u "$thread_basis")"
  thread_llvm="${LLVM_SYS_221_PREFIX:-}"
  [[ "$exe" != .exe || -z "$thread_llvm" ]] || thread_llvm="$(cygpath -u "$thread_llvm")"
  thread_cc="${P2_THREAD_CC:-${thread_llvm:+$thread_llvm/bin/clang$exe}}"
  thread_ar="${P2_THREAD_AR:-${thread_llvm:+$thread_llvm/bin/llvm-ar$exe}}"
  thread_cc="$(command -v "${thread_cc:-clang}")" || exit 2
  thread_ar="$(command -v "${thread_ar:-llvm-ar}")" || exit 2
  thread_archiv="$work/$(basename "$thread_basis")"
  thread_member="$("$thread_ar" t "$thread_basis" | tr -d '\r' | awk '/moo_thread[.](o|obj)$/ {print; exit}')"
  [[ -n "$thread_member" ]] || { echo "moo_thread-Archivmitglied nicht gefunden" >&2; exit 2; }
  cp "$thread_basis" "$thread_archiv" || exit 2
  thread_flags=(-O2 -g -std=c11 -D_GNU_SOURCE -DMOO_HAS_SURFACE -DMOO_HAS_3D)
  if [[ "$exe" != .exe ]]; then thread_flags+=(-fPIC); fi
  if [[ "${RC_THREAD_START_ASAN:-0}" == 1 ]]; then
    thread_flags+=(-O1 -fsanitize=address -fno-omit-frame-pointer)
  fi
  "$thread_cc" "${thread_flags[@]}" -I"$(thread_native_path "$ROOT/compiler/runtime")" \
    -include "$(thread_native_path "$ROOT/compiler/runtime/tests/thread_start_fehler_hook.h")" \
    -c "$(thread_native_path "$ROOT/compiler/runtime/moo_thread.c")" \
    -o "$(thread_native_path "$work/thread_test.o")" || exit 2
  "$thread_cc" "${thread_flags[@]}" \
    -c "$(thread_native_path "$ROOT/compiler/runtime/tests/thread_start_fehler_hook.c")" \
    -o "$(thread_native_path "$work/thread_hook.o")" || exit 2
  "$thread_ar" d "$thread_archiv" "$thread_member" || exit 2
  "$thread_ar" rcs "$thread_archiv" "$work/thread_test.o" "$work/thread_hook.o" || exit 2
  if [[ "${RC_THREAD_START_ASAN:-0}" == 1 ]]; then
    # Auch RC-/Capture-Freigaben instrumentieren, nicht nur den Aufrufer.
    thread_memory_member="$("$thread_ar" t "$thread_basis" | tr -d '\r' | awk '/moo_memory[.](o|obj)$/ {print; exit}')"
    [[ -n "$thread_memory_member" ]] || { echo "moo_memory-Archivmitglied nicht gefunden" >&2; exit 2; }
    "$thread_cc" "${thread_flags[@]}" -I"$(thread_native_path "$ROOT/compiler/runtime")" \
      -c "$(thread_native_path "$ROOT/compiler/runtime/moo_memory.c")" \
      -o "$(thread_native_path "$work/memory_test.o")" || exit 2
    "$thread_ar" d "$thread_archiv" "$thread_memory_member" || exit 2
    "$thread_ar" rcs "$thread_archiv" "$work/memory_test.o" || exit 2
  fi
  export MOO_RUNTIME_LIB="$(thread_native_path "$thread_archiv")"
  export MOO_STRENG=1
  if [[ "${RC_THREAD_START_ASAN:-0}" == 1 ]]; then
    # Dieselbe Link-Wrapper-Technik wie im vorhandenen Warten-LSan-Gate.
    mkdir -p "$work/san"
    for linker in cc clang; do
      printf '#!/bin/sh\nexec "%s" -fsanitize=address "$@"\n' "$thread_cc" > "$work/san/$linker"
      chmod +x "$work/san/$linker"
    done
    export PATH="$work/san:$PATH"
    export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
  fi
fi
declare -A erwartet=(
  [thread_start_fehler]="$(<"$ROOT/compiler/tests/p2/rc_geteilt/thread_start_fehler.expected")"
  [sperre_unterelemente]="$(<"$ROOT/compiler/tests/p2/rc_geteilt/sperre_unterelemente.expected")"  # P2-T121
  [methoden_typen]="$(<"$ROOT/compiler/tests/p2/rc_geteilt/methoden_typen.expected")"  # P2-T121
  [mt_geteilt]=8000000
  [einfuegen_kanal]=12000000
  [global_spaet]=8000000
  [fehler_getrennt]=$'Haupt 3000\nA 3000\nB 3000\nHauptthread ohne fremden Fehler'
  [geteilt_eingefroren]="$(<"$ROOT/compiler/tests/p2/rc_geteilt/geteilt_eingefroren.expected")"  # P2-T114
  [geteilt_global]="$(<"$ROOT/compiler/tests/p2/rc_geteilt/geteilt_global.expected")"  # P2-T114 C
  [spiel_kanal]="$(<"$ROOT/compiler/tests/p2/rc_geteilt/spiel_kanal.expected")"  # P2-T114 C
  [atom_zaehler]="$(<"$ROOT/compiler/tests/p2/rc_geteilt/atom_zaehler.expected")"  # P2-T114 B
  [sperre_konto]="$(<"$ROOT/compiler/tests/p2/rc_geteilt/sperre_konto.expected")"  # P2-T114 B
  [uebergib_puffer]="$(<"$ROOT/compiler/tests/p2/rc_geteilt/uebergib_puffer.expected")"  # P2-T114 B
  [warten_mehrfach]="$(<"$ROOT/compiler/tests/p2/rc_geteilt/warten_mehrfach.expected")"  # P2-T114 B
  [text_konstante]="$(<"$ROOT/compiler/tests/p2/rc_geteilt/text_konstante.expected")"  # P2-T114 B
  [starte_mehrere]="$(<"$ROOT/compiler/tests/p2/rc_geteilt/starte_mehrere.expected")"  # P2-T119
  [starte_lambda_mehrere]="$(<"$ROOT/compiler/tests/p2/rc_geteilt/starte_lambda_mehrere.expected")"  # P2-T120
  [warten_freigabe]="$(<"$ROOT/compiler/tests/p2/rc_geteilt/warten_freigabe.expected")"  # P2-T120
)
declare -A quelle=(
  [thread_start_fehler]="$ROOT/compiler/tests/p2/rc_geteilt/thread_start_fehler.moos"
  [sperre_unterelemente]="$ROOT/compiler/tests/p2/rc_geteilt/sperre_unterelemente.moos"
  [methoden_typen]="$ROOT/compiler/tests/p2/rc_geteilt/methoden_typen.moos"
  [mt_geteilt]="$ROOT/compiler/tests/p2/rc_geteilt/mt_geteilt.moos"
  [einfuegen_kanal]="$ROOT/compiler/tests/p2/rc_geteilt/einfuegen_kanal.moos"
  [global_spaet]="$ROOT/compiler/tests/p2/rc_geteilt/global_spaet.moos"
  [fehler_getrennt]="$ROOT/compiler/tests/p2/rc_geteilt/fehler_getrennt.moos"  # P2-T113
  [geteilt_eingefroren]="$ROOT/compiler/tests/p2/rc_geteilt/geteilt_eingefroren.moos"  # P2-T114
  [geteilt_global]="$ROOT/compiler/tests/p2/rc_geteilt/geteilt_global.moos"  # P2-T114 C
  [spiel_kanal]="$ROOT/compiler/tests/p2/rc_geteilt/spiel_kanal.moos"  # P2-T114 C
  [atom_zaehler]="$ROOT/compiler/tests/p2/rc_geteilt/atom_zaehler.moos"  # P2-T114 B
  [sperre_konto]="$ROOT/compiler/tests/p2/rc_geteilt/sperre_konto.moos"  # P2-T114 B
  [uebergib_puffer]="$ROOT/compiler/tests/p2/rc_geteilt/uebergib_puffer.moos"  # P2-T114 B
  [warten_mehrfach]="$ROOT/compiler/tests/p2/rc_geteilt/warten_mehrfach.moos"  # P2-T114 B
  [text_konstante]="$ROOT/compiler/tests/p2/rc_geteilt/text_konstante.moos"  # P2-T114 B
  [starte_mehrere]="$ROOT/compiler/tests/p2/rc_geteilt/starte_mehrere.moos"  # P2-T119
  [starte_lambda_mehrere]="$ROOT/compiler/tests/p2/rc_geteilt/starte_lambda_mehrere.moos"  # P2-T120
  [warten_freigabe]="$ROOT/compiler/tests/p2/rc_geteilt/warten_freigabe.moos"  # P2-T120
)
gesamt_fehler=0
for p in ${RC_STRESS_PROGRAMME:-mt_geteilt einfuegen_kanal global_spaet fehler_getrennt geteilt_eingefroren geteilt_global spiel_kanal atom_zaehler sperre_konto uebergib_puffer warten_mehrfach text_konstante starte_mehrere starte_lambda_mehrere warten_freigabe methoden_typen sperre_unterelemente}; do
  bin="$work/${p}_$tag$exe"
  if ! "$comp" compile "${quelle[$p]}" -o "$bin" >"$work/${p}_$tag.compile.log" 2>&1; then
    echo "$p $tag KOMPILIERFEHLER"; gesamt_fehler=$((gesamt_fehler+1)); continue
  fi
  ok=0; arten=""
  erwarteter_out="${erwartet[$p]//$'\r'/}"
  for _ in $(seq "$n"); do
    if [[ "${RC_THREAD_START_TEST:-0}" == 1 ]]; then
      out="$("$bin" 2>"$work/err.txt")"; rc=$?
    else
      out="$(timeout 120 "$bin" 2>"$work/err.txt")"; rc=$?
    fi
    out="${out//$'\r'/}"
    # P2-T117: unbehandelter Worker-Fehler (fehler_getrennt) -> Exit 1 am Ende.
    want_rc=0; [[ "$p" == fehler_getrennt ]] && want_rc=1
    if [[ $rc -eq $want_rc && "$out" == "$erwarteter_out" ]]; then ok=$((ok+1))
    else arten="$arten rc=$rc:$(head -c 60 "$work/err.txt" | tr '\n' ' ')${out:+ out=$out};"; fi
  done
  echo "$p $tag korrekt=$ok/$n${arten:+ fehler:$arten}"
  [[ $ok -eq $n ]] || gesamt_fehler=$((gesamt_fehler+1))
done
exit $gesamt_fehler
