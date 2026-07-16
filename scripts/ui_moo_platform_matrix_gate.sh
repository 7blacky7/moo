#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONTRACT="$ROOT/scripts/ui_moo_platform_matrix_contract.sh"
OUT="${1:-${TMPDIR:-/tmp}/moo-p016-platform-matrix-$(date -u +%Y%m%dT%H%M%SZ)-$$}"

if [[ ! -r "$CONTRACT" ]]; then
  printf 'P016 P1 PLATFORM MATRIX ERROR: unreadable_contract=%s\n' "$CONTRACT" >&2
  exit 2
fi
source "$CONTRACT"

for tool in mise grep sed sha256sum date; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'P016 P1 PLATFORM MATRIX ERROR: missing_tool=%s\n' "$tool" >&2
    exit 2
  fi
done
if [[ -e "$OUT" ]]; then
  printf 'P016 P1 PLATFORM MATRIX ERROR: output_exists=%s\n' "$OUT" >&2
  exit 2
fi
mkdir -p "$OUT"

run_mise() {
  local label="$1"
  local task="$2"
  set +e
  mise run "$task" >"$OUT/$label.stdout" 2>"$OUT/$label.stderr"
  RUN_RC=$?
  set -e
  printf '%s\n' "$RUN_RC" >"$OUT/$label.rc"
}

marker_in_logs() {
  local label="$1"
  local pattern="$2"
  grep -hFxq "$pattern" "$OUT/$label.stdout" "$OUT/$label.stderr"
}

run_mise aggregate-contract test-ui-moo-platform-matrix-contract
aggregate_contract_rc="$RUN_RC"
if [[ "$aggregate_contract_rc" -ne 0 ]] ||
   ! marker_in_logs aggregate-contract 'P016 P1 PLATFORM MATRIX CONTRACT GREEN: checks=11'; then
  local_o6_rc=1
else
  local_o6_rc=0
fi

run_mise local-o4 test-ui-moo-input
local_o4_rc="$RUN_RC"
if [[ "$local_o4_rc" -eq 0 ]] &&
   ! marker_in_logs local-o4 'P016-O4-MOO-A11Y-OK checks=14 no_ui=1'; then
  local_o4_rc=1
fi

run_mise local-o6 test-ui-moo-parity-win32-active-ime-contract
if [[ "$RUN_RC" -ne 0 ]] ||
   ! marker_in_logs local-o6 'P016 O6 ACTIVE IME EVIDENCE CONTRACT GREEN: checks=7'; then
  local_o6_rc=1
fi

run_mise windows test-ui-moo-parity-win32-active-ime
windows_rc="$RUN_RC"
windows_is_ime=2
mapfile -t windows_canonical < <(
  grep -hE '^P016 O6 WIN32 PARITY GREEN: .* ime_ev=([13]) ime_samples=[0-9]+ ime=[0-9]+/[0-9]+/[0-9]+ a11y=1/1/1 .* is_ime=([01]) inventory_unchanged=1 checks=[0-9]+$' \
    "$OUT/windows.stdout" "$OUT/windows.stderr" || true
)
if [[ "${#windows_canonical[@]}" -eq 1 ]]; then
  windows_is_ime="$(printf '%s\n' "${windows_canonical[0]}" |
    sed -E 's/.* is_ime=([01]) inventory_unchanged=1 checks=.*/\1/')"
fi
mapfile -t windows_ready < <(
  grep -hE '^P016 O6 ACTIVE IME EVIDENCE_READY: ime=2/1/1 samples=4 VM_STAYS_ON=1 artifacts=.+$' \
    "$OUT/windows.stdout" "$OUT/windows.stderr" || true
)
mapfile -t windows_unavailable < <(
  grep -hE '^P016 O6 ACTIVE IME PLATFORM_ENV_UNAVAILABLE: ime=0/0/0 samples=0 VM_STAYS_ON=1 artifacts=.+$' \
    "$OUT/windows.stdout" "$OUT/windows.stderr" || true
)
windows_marker_ok=0
if [[ "$windows_rc" -eq 0 && "${#windows_ready[@]}" -eq 1 &&
      "${#windows_unavailable[@]}" -eq 0 ]]; then
  windows_marker_ok=1
elif [[ "$windows_rc" -eq 77 && "${#windows_ready[@]}" -eq 0 &&
        "${#windows_unavailable[@]}" -eq 1 ]]; then
  windows_marker_ok=1
fi
if [[ "$windows_marker_ok" -ne 1 || "$windows_is_ime" -gt 1 ]]; then
  windows_rc=1
  windows_is_ime=2
fi

run_mise macos test-ui-moo-parity-macos-native
macos_rc="$RUN_RC"
mapfile -t macos_executed < <(
  grep -hFx 'P016 O6 MACOS APPKIT GATE EXECUTED: PARITY_STATUS=UNSUPPORTED domains=0xff checks=20' \
    "$OUT/macos.stdout" "$OUT/macos.stderr" || true
)
mapfile -t macos_ready < <(
  grep -hE '^P016 O6 MACOS APPKIT EVIDENCE_READY: parity=UNSUPPORTED artifacts=.+$' \
    "$OUT/macos.stdout" "$OUT/macos.stderr" || true
)
mapfile -t macos_unavailable < <(
  grep -hE '^P016 O6 MACOS APPKIT PLATFORM_UNAVAILABLE: host=[^ ]+ required=Darwin\+AppKit$' \
    "$OUT/macos.stdout" "$OUT/macos.stderr" || true
)
macos_marker_ok=0
if [[ "$macos_rc" -eq 0 && "${#macos_executed[@]}" -eq 1 &&
      "${#macos_ready[@]}" -eq 1 && "${#macos_unavailable[@]}" -eq 0 ]]; then
  macos_marker_ok=1
elif [[ "$macos_rc" -eq 77 && "${#macos_executed[@]}" -eq 0 &&
        "${#macos_ready[@]}" -eq 0 && "${#macos_unavailable[@]}" -eq 1 ]]; then
  macos_marker_ok=1
fi
if [[ "$macos_marker_ok" -ne 1 ]]; then
  macos_rc=1
fi

set +e
moo_ui_platform_matrix_classify \
  "$local_o4_rc" "$local_o6_rc" "$windows_rc" "$windows_is_ime" "$macos_rc"
matrix_rc=$?
set -e
printf 'local_o4_rc=%s\nlocal_o6_rc=%s\nwindows_rc=%s\nwindows_is_ime=%s\nmacos_rc=%s\nmatrix_rc=%s\n' \
  "$local_o4_rc" "$local_o6_rc" "$windows_rc" "$windows_is_ime" "$macos_rc" "$matrix_rc" \
  >"$OUT/status.txt"
sha256sum \
  "$ROOT/.mise.toml" \
  "$ROOT/scripts/ui_moo_platform_matrix_contract.sh" \
  "$ROOT/scripts/ui_moo_platform_matrix_contract_test.sh" \
  "$ROOT/scripts/ui_moo_platform_matrix_gate.sh" \
  >"$OUT/source-sha256.txt"
sha256sum "$OUT"/*.stdout "$OUT"/*.stderr "$OUT"/*.rc \
  "$OUT/status.txt" "$OUT/source-sha256.txt" >"$OUT/final-evidence-sha256.txt"

case "$matrix_rc" in
  0)
    printf 'P016 P1 PLATFORM MATRIX EVIDENCE_READY: local_o4=PASS local_o6=PASS windows=PASS is_ime=1 macos=EXECUTED artifacts=%s\n' "$OUT"
    exit 0
    ;;
  77)
    printf 'P016 P1 PLATFORM MATRIX PLATFORM_ENV_UNAVAILABLE: local_o4=PASS local_o6=PASS windows_rc=%s windows_is_ime=%s macos_rc=%s artifacts=%s\n' \
      "$windows_rc" "$windows_is_ime" "$macos_rc" "$OUT"
    exit 77
    ;;
  *)
    printf 'P016 P1 PLATFORM MATRIX ERROR: local_o4_rc=%s local_o6_rc=%s windows_rc=%s windows_is_ime=%s macos_rc=%s artifacts=%s\n' \
      "$local_o4_rc" "$local_o6_rc" "$windows_rc" "$windows_is_ime" "$macos_rc" "$OUT" >&2
    exit 1
    ;;
esac
