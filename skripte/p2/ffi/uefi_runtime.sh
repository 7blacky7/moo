#!/usr/bin/env bash
# Frueher eine eigene UEFI-Pipeline (hallo_uefi.moos nur mit
# uefi_entry.c + uefi_rt.c gelinkt). Seit der Netz-/Surface-Erweiterung ist
# hallo_uefi.moos eine volle App (Netz, Surface, dynamische Moo-Ops) und braucht
# die Laufzeitquellen (RT_SOURCES), /nodefaultlib und den Escape-Ablauf per
# QEMU-Monitor. Eine Quelle der Wahrheit: scripts/uefi-smoke.sh prueft dieselben
# Nachweise (MOO-UEFI-ENTRY und -OK auf debugcon, MOO-UEFI-MOO auf COM1) und mehr.
# Fail-closed: uefi-smoke meldet fehlendes Tooling/OVMF als
# SKIP mit RC 0; fuer dieses Gate ist ein SKIP KEIN Beleg und damit rot.
set -euo pipefail
cd "$(dirname "$0")/../../.."
# Compiler-Wahl: P2_COMPILER ist die Schnittstelle der ffi-Gates.
# Sind P2_COMPILER und MOO_COMPILER beide gesetzt und verschieden, ist unklar,
# welcher geprueft werden soll -> fail-closed abbrechen statt still MOO zu nehmen.
if [[ -n "${P2_COMPILER:-}" ]]; then
    if [[ -n "${MOO_COMPILER:-}" && "$MOO_COMPILER" != "$P2_COMPILER" ]]; then
        echo "FEHLER uefi-runtime: P2_COMPILER ($P2_COMPILER) und MOO_COMPILER ($MOO_COMPILER) widersprechen sich" >&2
        exit 1
    fi
    export MOO_COMPILER="$P2_COMPILER"
fi
log="$(mktemp "${TMPDIR:-/tmp}/uefi_runtime.XXXXXX")"
trap 'rm -f -- "$log"' EXIT
set +e
bash scripts/uefi-smoke.sh "$@" 2>&1 | tee "$log"
rc=${PIPESTATUS[0]}
set -e
if [[ "$rc" -ne 0 ]]; then
    echo "FEHLER uefi-runtime: scripts/uefi-smoke.sh RC=$rc" >&2; exit "$rc"
fi
if grep -q 'SKIP:' "$log"; then
    echo "FEHLER uefi-runtime: uefi-smoke hat uebersprungen (SKIP) - kein Laufzeitbeleg" >&2; exit 1
fi
grep -q 'ERGEBNIS: ALLE 4 Phasen gruen' "$log" \
    || { echo "FEHLER uefi-runtime: Ergebniszeile von uefi-smoke fehlt" >&2; exit 1; }
grep -q 'MOO-UEFI-MOO' "$log" \
    || { echo "FEHLER uefi-runtime: Marker MOO-UEFI-MOO (COM1) nicht belegt" >&2; exit 1; }
echo "PASS uefi-runtime: QEMU/OVMF-Laufzeitbeleg (MOO-UEFI-ENTRY/-MOO/-OK) ueber scripts/uefi-smoke.sh"
