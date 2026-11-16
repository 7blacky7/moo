#!/usr/bin/env bash
# P2-T112: IR zweier Compiler vergleichen. Programme ohne starte/spawn muessen
# bitgleiches IR liefern; Programme mit starte duerfen nur die Global-Hooks
# (moo_globale_anmelden / moo_global_geschrieben) zusaetzlich haben.
# P2-T114 Phase C: diese Hooks gibt es nicht mehr; Programme mit starte
# unterscheiden sich nur noch um moo_global_pruefen (unbeweisbarer Thread-Code)
# und moo_teilen fuer 'geteilt' markierte Globale.
# Aufruf: rc_ir_vergleich.sh COMPILER_VORHER COMPILER_NACHHER ARBEITSVERZ DATEI...
set -uo pipefail
a="$(realpath "$1")"; b="$(realpath "$2")"; work="$(realpath -m "$3")"; shift 3
mkdir -p "$work"
n=0; gleich=0; fehler=0
for f in "$@"; do
  name="$(basename "$f" .moos)"
  "$a" compile --emit-ir "$f" -o "$work/${name}_v.ll" >/dev/null 2>&1 || continue
  if ! "$b" compile --emit-ir "$f" -o "$work/${name}_n.ll" >/dev/null 2>&1; then
    echo "NACHHER-FEHLER $f"; fehler=$((fehler+1)); continue
  fi
  n=$((n+1))
  if cmp -s <(grep -v 'source_filename\|ModuleID' "$work/${name}_v.ll") <(grep -v 'source_filename\|ModuleID' "$work/${name}_n.ll"); then
    gleich=$((gleich+1))
  elif grep -qE '"starte"|starte\(|spawn\(' "$f"; then
    echo "ANDERS(starte, erwartet) $f"
  else
    echo "ANDERS(ohne starte!) $f"; fehler=$((fehler+1))
  fi
done
echo "verglichen=$n bitgleich=$gleich fehler=$fehler"
exit $fehler
