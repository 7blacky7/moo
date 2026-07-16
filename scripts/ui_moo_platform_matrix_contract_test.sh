#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# The tests-only RED is valid only when the missing aggregate classifier is
# the sole semantic dependency.
source "$ROOT/scripts/ui_moo_platform_matrix_contract.sh"

checks=0
failures=0

check_status() {
  local label="$1"
  local expected="$2"
  local actual
  shift 2

  set +e
  moo_ui_platform_matrix_classify "$@"
  actual=$?
  set -e

  checks=$((checks + 1))
  if [[ "$actual" -ne "$expected" ]]; then
    failures=$((failures + 1))
    printf 'FAIL: %s expected=%s actual=%s\n' "$label" "$expected" "$actual" >&2
  fi
}

check_status full-real-platforms 0 0 0 0 1 0
check_status windows-unavailable 77 0 0 77 0 0
check_status macos-unavailable 77 0 0 0 1 77
check_status both-external-unavailable 77 0 0 77 0 77
check_status reject-fake-windows-pass 1 0 0 0 0 0
check_status reject-active-windows-unavailable 1 0 0 77 1 0
check_status reject-local-o4-failure 1 1 0 77 0 77
check_status reject-local-o6-failure 1 0 1 77 0 77
check_status reject-windows-error 1 0 0 1 0 77
check_status reject-macos-error 1 0 0 77 0 1
check_status reject-nonnumeric-input 1 x 0 77 0 77

if [[ "$failures" -ne 0 ]]; then
  printf 'P016 P1 PLATFORM MATRIX CONTRACT FAIL: failures=%s checks=%s\n' \
    "$failures" "$checks" >&2
  exit 1
fi

printf 'P016 P1 PLATFORM MATRIX CONTRACT GREEN: checks=%s\n' "$checks"
