#!/usr/bin/env bash
# P2-T112: TSan-Gate fuer Refcount-Zugriffe auf geteilte Heap-Werte.
# Aufruf: rc_geteilt_tsan.sh COMPILER RUNTIME_QUELLVERZ ARBEITSVERZ
#   COMPILER          moo-compiler (dessen eingebettete Runtime wird kopiert)
#   RUNTIME_QUELLVERZ Verzeichnis mit den passenden Runtime-Quellen (compiler/runtime)
# Die refcount-relevanten Runtime-Objekte werden mit -fsanitize=thread neu
# gebaut und im Archiv ersetzt; das Programm wird mit TSan gelinkt.
# Exit 0 nur, wenn alle Fixtures 0 TSan-Warnungen haben und richtig rechnen.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
comp="$(realpath "$1")"; rt="$(realpath "$2")"; work="$(realpath -m "$3")"
mkdir -p "$work/obj" "$work/bin"
basis="$(find "$(dirname "$comp")" "$(dirname "$comp")/.." -path '*/out/libmoo_runtime.a' -newer "$rt/moo_memory.c" 2>/dev/null | head -1)"
[[ -n "$basis" ]] || basis="$(find "$(dirname "$comp")" -path '*/out/libmoo_runtime.a' 2>/dev/null | head -1)"
[[ -f "$basis" ]] || { echo "Runtime-Archiv zum Compiler nicht gefunden"; exit 2; }
cp "$basis" "$work/lib.a"
defs="-DMOO_HAS_SURFACE -DMOO_HAS_3D"
for f in moo_error moo_memory moo_thread moo_list moo_dict moo_object moo_buffer moo_struct_box; do
  mitglied="$(ar t "$work/lib.a" | grep -E "(^|-)$f\.o$" | head -1)"
  [[ -n "$mitglied" ]] || { echo "Archivmitglied $f.o fehlt"; exit 2; }
  cc -c -O1 -g -fPIC -fsanitize=thread $defs -I"$rt" "$rt/$f.c" -o "$work/obj/$mitglied" || exit 2
  (cd "$work/obj" && ar r "$work/lib.a" "$mitglied")
done
printf '#!/bin/sh\nexec /usr/bin/cc "$@" -fsanitize=thread\n' >"$work/bin/cc"; chmod +x "$work/bin/cc"
declare -A erwartet=([thread_argument]="" [mt_geteilt_klein]=800000 [einfuegen_kanal]=12000000 [global_spaet]=8000000 [fehler_getrennt]="Hauptthread ohne fremden Fehler" [geteilt_eingefroren]="Kanal: Wert ist zwischen Threads geteilt und damit eingefroren" [geteilt_global]=3 [spiel_kanal]=19000 [atom_zaehler]="keine Zahl: atom erwartet eine Zahl, erhalten: Text" [sperre_konto]=Ende [uebergib_puffer]=4 [warten_mehrfach]=1 [text_konstante]=hallo [starte_mehrere]=Ende [starte_lambda_mehrere]=Ende [warten_freigabe]=40000)
fehler=0
for p in ${RC_TSAN_PROGRAMME:-thread_argument mt_geteilt_klein einfuegen_kanal global_spaet fehler_getrennt geteilt_eingefroren geteilt_global spiel_kanal atom_zaehler sperre_konto uebergib_puffer warten_mehrfach text_konstante starte_mehrere starte_lambda_mehrere warten_freigabe}; do
  PATH="$work/bin:$PATH" MOO_RUNTIME_LIB="$work/lib.a" "$comp" compile "$ROOT/compiler/tests/p2/rc_geteilt/$p.moos" -o "$work/$p" >"$work/$p.clog" 2>&1 \
    || { echo "$p KOMPILIERFEHLER"; fehler=$((fehler+1)); continue; }
  TSAN_OPTIONS="halt_on_error=0 report_signal_unsafe=0" setarch -R "$work/$p" >"$work/$p.out" 2>"$work/$p.tsan"
  warn=$(grep -c "WARNING: ThreadSanitizer" "$work/$p.tsan")
  out="$(tail -1 "$work/$p.out")"
  ok=ja
  [[ -z "${erwartet[$p]}" || "$out" == "${erwartet[$p]}" ]] || ok=nein
  echo "$p tsan_warnungen=$warn ergebnis_korrekt=$ok"
  [[ $warn -eq 0 && $ok == ja ]] || fehler=$((fehler+1))
done
exit $fehler
