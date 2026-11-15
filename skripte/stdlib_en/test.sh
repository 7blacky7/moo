#!/usr/bin/env bash
# P1-T506: Fixtures DE + EN fuer die stdlib-Module dns, websocket, ui/ui_test.
# Jede Fixture laeuft ohne Netz und ohne Fenster und muss 'PASS' ausgeben.
# Aufruf aus dem Repo-Root oder ueber `mise run -C skripte/stdlib_en test`.
# Compiler ueberschreibbar mit MOO_COMPILER.
set -u
cd "$(dirname "$0")/../.."
moo="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
failed=0
for modul in dns websocket ui; do
  for sprache in de en; do
    src="skripte/stdlib_en/${modul}_${sprache}.moos"
    log="$(mktemp)"
    if timeout 120s "$moo" run "$src" >"$log" 2>&1 \
       && grep -q "^STDLIB-EN ${modul} ${sprache} PASS$" "$log" \
       && ! grep -q 'FAIL' "$log"; then
      echo "PASS ${modul} ${sprache}"
    else
      echo "FAIL ${modul} ${sprache}"
      tail -n 15 "$log"
      failed=$((failed + 1))
    fi
    rm -f "$log"
  done
done
# Kollisionsfall: Benutzerfunktionen mit generischen Namen bleiben erhalten.
log="$(mktemp)"
if timeout 120s "$moo" run skripte/stdlib_en/collision.moos >"$log" 2>&1 \
   && grep -q '^STDLIB-EN collision PASS$' "$log" && ! grep -q 'FAIL' "$log"; then
  echo "PASS collision"
else
  echo "FAIL collision"
  tail -n 15 "$log"
  failed=$((failed + 1))
fi
rm -f "$log"
[ "$failed" -eq 0 ]
