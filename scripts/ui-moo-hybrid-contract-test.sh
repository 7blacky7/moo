#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DOC="$ROOT/docs/ui_moo-backendvertrag.md"
CODEGEN="$ROOT/compiler/src/codegen.rs"
BINDINGS="$ROOT/compiler/src/runtime_bindings.rs"
CORE="$ROOT/stdlib/ui_moo_kern.moo"
FEHLER=0

fehler() {
  printf 'FEHLER: %s\n' "$*" >&2
  FEHLER=$((FEHLER + 1))
}

SECTION="$(awk '
  /^## Hybrid-Grenze: native Plattformdienste$/ { aktiv = 1; next }
  aktiv && /^## / { exit }
  aktiv { print }
' "$DOC")"

if [[ -z "$SECTION" ]]; then
  fehler "kanonischer Abschnitt fehlt: ## Hybrid-Grenze: native Plattformdienste"
else
  dialog_ok=1
  for token in '`ui_info`' '`ui_warnung`' '`ui_fehler`' '`ui_frage`' '`ui_eingabe_dialog`' '`ui_datei_oeffnen`' '`ui_datei_speichern`'; do
    [[ "$SECTION" == *"$token"* ]] || dialog_ok=0
  done
  [[ "$dialog_ok" -eq 1 ]] || fehler "Dialog-Zeile nennt nicht alle nativen APIs"

  if [[ "$SECTION" != *'`ui_eingabe`'* || "$SECTION" != *"IME"* ]]; then
    fehler "Text-/IME-Zeile nennt nicht ui_eingabe und IME"
  fi

  menu_ok=1
  for token in '`ui_menueleiste`' '`ui_menue`' '`ui_menue_eintrag`'; do
    [[ "$SECTION" == *"$token"* ]] || menu_ok=0
  done
  [[ "$menu_ok" -eq 1 ]] || fehler "Menu-Zeile nennt nicht alle nativen APIs"

  tray_ok=1
  for token in '`tray_create`' '`tray_menu_add`'; do
    [[ "$SECTION" == *"$token"* ]] || tray_ok=0
  done
  [[ "$tray_ok" -eq 1 ]] || fehler "Tray-Zeile nennt nicht alle nativen APIs"

  if [[ "$SECTION" != *"Host-Adapter"* || "$SECTION" != *"ui_moo implementiert diese Plattformdienste nicht selbst"* ]]; then
    fehler "explizite Host-Adapter-Eigentuemerschaft fehlt"
  fi
fi

codegen_route() {
  local api="$1"
  local symbol="$2"
  awk -v api="$api" -v symbol="$symbol" '
    index($0, "\"" api "\"") && index($0, "=>") { im_arm = 1 }
    im_arm && index($0, "self.call_rt(self.rt." symbol ",") { gefunden = 1; exit }
    im_arm && /^[[:space:]]*}[[:space:]]*$/ { exit }
    END { exit gefunden ? 0 : 1 }
  ' "$CODEGEN"
}

route_ok=1
while IFS=: read -r api symbol header; do
  codegen_route "$api" "$symbol" || route_ok=0
  grep -Eq "^[[:space:]]*pub[[:space:]]+$symbol:[[:space:]]+FunctionValue" "$BINDINGS" || route_ok=0
  grep -Eq "^[[:space:]]*$symbol:[[:space:]]+decl_mv_mv!\\(\"$symbol\"" "$BINDINGS" || route_ok=0
  grep -Eq "^[[:space:]]*MooValue[[:space:]]+$symbol[[:space:]]*\\(" "$ROOT/$header" || route_ok=0
done <<'ROUTES'
ui_info:moo_ui_info:compiler/runtime/moo_ui.h
ui_warnung:moo_ui_warnung:compiler/runtime/moo_ui.h
ui_fehler:moo_ui_fehler:compiler/runtime/moo_ui.h
ui_frage:moo_ui_frage:compiler/runtime/moo_ui.h
ui_eingabe_dialog:moo_ui_eingabe_dialog:compiler/runtime/moo_ui.h
ui_datei_oeffnen:moo_ui_datei_oeffnen:compiler/runtime/moo_ui.h
ui_datei_speichern:moo_ui_datei_speichern:compiler/runtime/moo_ui.h
ui_eingabe:moo_ui_eingabe:compiler/runtime/moo_ui.h
ui_menueleiste:moo_ui_menueleiste:compiler/runtime/moo_ui.h
ui_menue:moo_ui_menue:compiler/runtime/moo_ui.h
ui_menue_eintrag:moo_ui_menue_eintrag:compiler/runtime/moo_ui.h
tray_create:moo_tray_create:compiler/runtime/moo_tray.h
tray_menu_add:moo_tray_menu_add:compiler/runtime/moo_tray.h
ROUTES
[[ "$route_ok" -eq 1 ]] || fehler "mindestens eine exakte Codegen-Binding-ABI-Route fehlt"

FORBIDDEN='^[[:space:]]*funktion[[:space:]]+((ui|uim)_(info|warnung|fehler|frage|eingabe_dialog|datei_oeffnen|datei_speichern|menueleiste|menue|menue_eintrag|tray_create|tray_menu_add|ime_start|ime_update|ime_commit|ime_cancel)|tray_(create|menu_add)|ui_eingabe)[[:space:]]*\('
if grep -Eq "$FORBIDDEN" "$CORE"; then
  fehler "retained ui_moo definiert verbotenen nativen Plattformdienst"
fi

if [[ "$FEHLER" -ne 0 ]]; then
  printf 'P016-D1-HYBRID-CONTRACT-FEHLER %d\n' "$FEHLER" >&2
  exit 1
fi

printf 'P016-D1-HYBRID-CONTRACT-OK checks=8 no_ui=1\n'
