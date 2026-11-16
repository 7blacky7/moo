#!/usr/bin/env bash
# P2-T112: Korrektheits-Stresstest fuer geteilte Heap-Werte zwischen Threads.
# Aufruf: rc_geteilt_stress.sh COMPILER ARBEITSVERZ [WIEDERHOLUNGEN] [ETIKETT]
# Kompiliert die Fixtures aus compiler/tests/p2/rc_geteilt und build/opus-t112/prog
# (mt_geteilt) und laeuft jede N-mal; zaehlt korrekte Ausgaben.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
comp="$(realpath "$1")"; work="$(realpath -m "$2")"; n="${3:-20}"; tag="${4:-lauf}"
mkdir -p "$work"
declare -A erwartet=(
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
for p in ${RC_STRESS_PROGRAMME:-mt_geteilt einfuegen_kanal global_spaet fehler_getrennt geteilt_eingefroren geteilt_global spiel_kanal atom_zaehler sperre_konto uebergib_puffer warten_mehrfach text_konstante starte_mehrere starte_lambda_mehrere warten_freigabe}; do
  bin="$work/${p}_$tag"
  if ! "$comp" compile "${quelle[$p]}" -o "$bin" >"$work/${p}_$tag.compile.log" 2>&1; then
    echo "$p $tag KOMPILIERFEHLER"; gesamt_fehler=$((gesamt_fehler+1)); continue
  fi
  ok=0; arten=""
  for _ in $(seq "$n"); do
    out="$(timeout 120 "$bin" 2>"$work/err.txt")"; rc=$?
    # P2-T117: unbehandelter Worker-Fehler (fehler_getrennt) -> Exit 1 am Ende.
    want_rc=0; [[ "$p" == fehler_getrennt ]] && want_rc=1
    if [[ $rc -eq $want_rc && "$out" == "${erwartet[$p]}" ]]; then ok=$((ok+1))
    else arten="$arten rc=$rc:$(head -c 60 "$work/err.txt" | tr '\n' ' ')${out:+ out=$out};"; fi
  done
  echo "$p $tag korrekt=$ok/$n${arten:+ fehler:$arten}"
  [[ $ok -eq $n ]] || gesamt_fehler=$((gesamt_fehler+1))
done
exit $gesamt_fehler
