#!/usr/bin/env bash
# P2-T47 (Plan b1f51d65 C4/O2): Bestandsaudit der Modulaufloesung.
# Vergleicht `ast --imports` fuer alle beispiele/**/*.moos zwischen einem
# Referenzcompiler (ALT) und dem aktuellen (NEU) und listet Dateien, die erst
# mit NEU scheitern (z. B. unbekannter Importname jetzt Fehler).
# Aufruf: import_audit.sh [ALT] [NEU]
set -u
cd "$(dirname "$0")/../../.."
alt="${1:-build/opus-p2-target/release/moo-compiler}"
neu="${2:-${MOO_COMPILER:-compiler/target/release/moo-compiler}}"
out=build/p2-frontend-module/audit
mkdir -p "$out"
: >"$out/neu_fehler.txt"
gesamt=0; alt_fehler=0; neu_fehler=0; nur_neu=0
while IFS= read -r -d '' src; do
  gesamt=$((gesamt + 1))
  timeout 30s "$alt" ast "$src" --imports >/dev/null 2>"$out/alt.err"; rc_alt=$?
  timeout 30s "$neu" ast "$src" --imports >/dev/null 2>"$out/neu.err"; rc_neu=$?
  [ "$rc_alt" -ne 0 ] && alt_fehler=$((alt_fehler + 1))
  [ "$rc_neu" -ne 0 ] && neu_fehler=$((neu_fehler + 1))
  if [ "$rc_neu" -ne 0 ] && [ "$rc_alt" -eq 0 ]; then
    nur_neu=$((nur_neu + 1))
    printf '%s\n' "$src" >>"$out/neu_fehler.txt"
    cat "$out/neu.err" >>"$out/neu_fehler.txt"
  fi
  if [ -s "$out/neu.err" ] && [ "$rc_neu" -eq 0 ]; then
    printf 'WARN %s: %s\n' "$src" "$(head -c 300 "$out/neu.err")"
  fi
done < <(find beispiele stdlib -type f -name '*.moos' -print0)
printf 'AUDIT gesamt=%s alt_fehler=%s neu_fehler=%s nur_neu=%s\n' "$gesamt" "$alt_fehler" "$neu_fehler" "$nur_neu"
cat "$out/neu_fehler.txt"
[ "$nur_neu" -eq 0 ]
