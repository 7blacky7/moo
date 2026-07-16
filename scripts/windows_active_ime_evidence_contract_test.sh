#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# This must remain the first semantic dependency: the tests-only RED is valid
# only when this missing production classifier is the sole failure.
source "$ROOT/scripts/windows_active_ime_evidence_contract.sh"

checks=0
failures=0

check_status() {
  local label="$1"
  local expected="$2"
  local actual
  shift 2

  set +e
  moo_windows_active_ime_evidence_classify "$@"
  actual=$?
  set -e

  checks=$((checks + 1))
  if [[ "$actual" -ne "$expected" ]]; then
    failures=$((failures + 1))
    printf 'FAIL: %s expected=%s actual=%s\n' "$label" "$expected" "$actual" >&2
  fi
}

check_status pass-active-ime 0 1 4 2 1 1 1
check_status unavailable-inactive-layout 77 3 0 0 0 0 0
check_status reject-active-ime-regression 1 3 0 0 0 0 1
check_status reject-pass-with-inactive-layout 1 1 4 2 1 1 0
check_status reject-malformed-pass-metrics 1 1 4 2 1 0 1
check_status reject-unknown-evidence 1 2 0 0 0 0 0
check_status reject-nonnumeric-input 1 x 0 0 0 0 0

if [[ "$failures" -ne 0 ]]; then
  printf 'P016 O6 ACTIVE IME EVIDENCE CONTRACT FAIL: failures=%s checks=%s\n' \
    "$failures" "$checks" >&2
  exit 1
fi

printf 'P016 O6 ACTIVE IME EVIDENCE CONTRACT GREEN: checks=%s\n' "$checks"
