#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RT="$ROOT/compiler/runtime"
OUT="${1:-${TMPDIR:-/tmp}/moo-p016-active-ime-gate}"
WINVM_HOST="${MOO_WINVM_HOST:-192.168.50.246}"
WINVM_USER="${MOO_WINVM_USER:-pro}"
WINVM_KEY="${MOO_WINVM_KEY:-/tmp/moo-winvm-ed25519}"
RUN_TIMEOUT_MS="${MOO_WINVM_IME_TIMEOUT_MS:-30000}"
CC="${MINGW_CC:-x86_64-w64-mingw32-gcc}"
CXX="${MINGW_CXX:-x86_64-w64-mingw32-g++}"
EVIDENCE_CONTRACT="$ROOT/scripts/windows_active_ime_evidence_contract.sh"

if [[ ! -r "$EVIDENCE_CONTRACT" ]]; then
  printf 'P016 O6 ACTIVE IME GATE ERROR: unreadable_contract=%s\n' "$EVIDENCE_CONTRACT" >&2
  exit 2
fi
source "$EVIDENCE_CONTRACT"

for tool in "$CC" "$CXX" ssh scp sha256sum grep tr cmp iconv base64 uname wc; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'P016 O6 ACTIVE IME GATE ERROR: missing_tool=%s\n' "$tool" >&2
    exit 2
  fi
done
if [[ ! -r "$WINVM_KEY" ]]; then
  printf 'P016 O6 ACTIVE IME GATE ERROR: unreadable_key=%s\n' "$WINVM_KEY" >&2
  exit 2
fi
case "$RUN_TIMEOUT_MS" in
  ''|*[!0-9]*|0)
    printf 'P016 O6 ACTIVE IME GATE ERROR: invalid_timeout_ms=%s\n' "$RUN_TIMEOUT_MS" >&2
    exit 2
    ;;
esac

mkdir -p "$OUT"
shasum_sources="$OUT/source-sha256.txt"
sha256sum \
  "$ROOT/scripts/windows_active_ime_gate.sh" \
  "$EVIDENCE_CONTRACT" \
  "$RT/tests/test_ui_host_parity_win32_native.c" \
  "$RT/moo_ui_host_parity.c" \
  "$RT/moo_ui_host_parity_win32.c" \
  "$RT/moo_ui_host_parity_win32_dwrite.cpp" \
  "$RT/moo_ui_host_parity_instrumentation.c" \
  "$RT/moo_compositor_core.c" \
  "$RT/moo_compositor_effects_state.c" \
  "$RT/moo_compositor_raster.c" \
  "$RT/moo_compositor_effects_damage.c" \
  "$RT/moo_compositor_effects_cpu.c" \
  "$RT/moo_compositor_effects_math.c" \
  "$RT/moo_compositor_animation.c" \
  "$RT/moo_ui_host_parity.h" \
  "$RT/moo_ui_host_parity_win32_dwrite.h" \
  "$RT/moo_ui_host_parity_instrumentation.h" \
  "$RT/moo_ui_host_parity_instrumentation_internal.h" \
  "$RT/moo_compositor_protocol.h" \
  "$RT/moo_compositor_core.h" \
  "$RT/moo_compositor_effects_protocol.h" \
  "$RT/moo_compositor_effects_state.h" \
  "$RT/moo_compositor_effects_cpu.h" \
  "$RT/moo_compositor_effects_damage.h" \
  "$RT/moo_compositor_effects_gpu.h" \
  "$RT/moo_compositor_effects_math.h" \
  "$RT/moo_compositor_animation.h" \
  > "$shasum_sources"

CFLAGS=(-std=c11 -O2 -Wall -Wextra -Wconversion -Werror -pedantic -I"$RT")
LEGACY_MASK_CFLAGS=(-std=c11 -O2 -Wall -Wextra -Werror -pedantic -I"$RT")
CXXFLAGS=(-std=c++17 -O2 -Wall -Wextra -Wconversion -Werror -pedantic -I"$RT")
printf 'kernel=%s\ncc_path=%s\ncc_version=%s\ncxx_path=%s\ncxx_version=%s\n' \
  "$(uname -srm)" "$(command -v "$CC")" "$($CC -dumpfullversion -dumpversion)" \
  "$(command -v "$CXX")" "$($CXX -dumpfullversion -dumpversion)" \
  > "$OUT/local-host-toolchain.txt"

"$CC" "${CFLAGS[@]}" -c "$RT/tests/test_ui_host_parity_win32_native.c" -o "$OUT/test.o"
"$CC" "${CFLAGS[@]}" -c "$RT/moo_ui_host_parity.c" -o "$OUT/common.o"
"$CC" "${CFLAGS[@]}" -c "$RT/moo_ui_host_parity_win32.c" -o "$OUT/win32.o"
"$CC" "${CFLAGS[@]}" -c "$RT/moo_ui_host_parity_instrumentation.c" -o "$OUT/instrumentation.o"
"$CC" "${CFLAGS[@]}" -c "$RT/moo_compositor_core.c" -o "$OUT/compositor-core.o"
"$CC" "${LEGACY_MASK_CFLAGS[@]}" -c "$RT/moo_compositor_effects_state.c" -o "$OUT/effects-state.o"
"$CC" "${CFLAGS[@]}" -c "$RT/moo_compositor_raster.c" -o "$OUT/raster.o"
"$CC" "${CFLAGS[@]}" -c "$RT/moo_compositor_effects_damage.c" -o "$OUT/effects-damage.o"
"$CC" "${LEGACY_MASK_CFLAGS[@]}" -c "$RT/moo_compositor_effects_cpu.c" -o "$OUT/effects-cpu.o"
"$CC" "${LEGACY_MASK_CFLAGS[@]}" -c "$RT/moo_compositor_effects_math.c" -o "$OUT/effects-math.o"
"$CC" "${CFLAGS[@]}" -c "$RT/moo_compositor_animation.c" -o "$OUT/animation.o"
"$CXX" "${CXXFLAGS[@]}" -c "$RT/moo_ui_host_parity_win32_dwrite.cpp" -o "$OUT/dwrite.o"
"$CXX" -static -static-libgcc -static-libstdc++ \
  "$OUT/test.o" "$OUT/common.o" "$OUT/win32.o" "$OUT/instrumentation.o" \
  "$OUT/compositor-core.o" "$OUT/effects-state.o" "$OUT/raster.o" \
  "$OUT/effects-damage.o" "$OUT/effects-cpu.o" "$OUT/effects-math.o" \
  "$OUT/animation.o" "$OUT/dwrite.o" \
  -lgdi32 -luser32 -limm32 -loleacc -loleaut32 -lole32 -luuid -ldwrite \
  -o "$OUT/active-ime-gate.exe"
sha256sum -c "$shasum_sources" > "$OUT/source-check-after-build.txt"

local_sha="$(sha256sum "$OUT/active-ime-gate.exe" | { read -r hash _; printf '%s' "$hash"; })"
run_id="p016-o6-active-ime-${local_sha:0:16}"
remote_dir_scp="C:/moo-runs/$run_id"
remote_exe_win="C:\\moo-runs\\$run_id\\active-ime-gate.exe"
target="$WINVM_USER@$WINVM_HOST"
SSH=(ssh -F /dev/null -i "$WINVM_KEY" -o StrictHostKeyChecking=no   -o ConnectTimeout=10 -o ServerAliveInterval=5 -o ServerAliveCountMax=3)
SCP=(scp -F /dev/null -i "$WINVM_KEY" -o StrictHostKeyChecking=no   -o ConnectTimeout=10)

"${SSH[@]}" "$target"  "powershell.exe -NoProfile -NonInteractive -Command \"[System.IO.Directory]::CreateDirectory('C:\\moo-runs\\$run_id') | Out-Null\""
"${SCP[@]}" "$OUT/active-ime-gate.exe"   "$target:$remote_dir_scp/active-ime-gate.exe"

remote_sha="$("${SSH[@]}" "$target"   "powershell.exe -NoProfile -NonInteractive -Command \"(Get-FileHash -Algorithm SHA256 -LiteralPath '$remote_exe_win').Hash.ToLowerInvariant()\""   | tr -d '\r\n')"
if [[ "$remote_sha" != "$local_sha" ]]; then
  printf 'local_sha256=%s\nremote_sha256=%s\nequal=0\n' \
    "$local_sha" "$remote_sha" > "$OUT/artifact-hash.txt"
  printf 'P016 O6 ACTIVE IME GATE ERROR: hash_mismatch local=%s remote=%s\n' \
    "$local_sha" "$remote_sha" >&2
  exit 1
fi
printf 'local_sha256=%s\nremote_sha256=%s\nequal=1\n' \
  "$local_sha" "$remote_sha" > "$OUT/artifact-hash.txt"
remote_host_command='powershell.exe -NoProfile -NonInteractive -Command "$ErrorActionPreference=[System.Management.Automation.ActionPreference]::Stop; $machine=[System.Environment]::MachineName; $os=[System.Environment]::OSVersion.VersionString; $ps=$PSVersionTable.PSVersion.ToString(); if([string]::IsNullOrWhiteSpace($machine) -or [string]::IsNullOrWhiteSpace($os) -or [string]::IsNullOrWhiteSpace($ps)){ exit 1 }; [PSCustomObject]@{MachineName=$machine;OSVersion=$os;Is64BitOperatingSystem=[System.Environment]::Is64BitOperatingSystem;Is64BitProcess=[System.Environment]::Is64BitProcess;PowerShell=$ps} | ConvertTo-Json -Compress"'
"${SSH[@]}" "$target" "$remote_host_command" | tr -d '\r' \
  > "$OUT/remote-host.json"
remote_host_regex="^[{]\"MachineName\":\"[^\"]+\",\"OSVersion\":\"[^\"]+\",\"Is64BitOperatingSystem\":(true|false),\"Is64BitProcess\":(true|false),\"PowerShell\":\"[0-9]+([.][0-9]+){1,3}\"[}]$"
remote_host_line_count="$(wc -l < "$OUT/remote-host.json")"
if [[ "$remote_host_line_count" -ne 1 ]] ||
   ! grep -Eq "$remote_host_regex" "$OUT/remote-host.json"; then
  printf 'P016 O6 ACTIVE IME GATE ERROR: invalid_remote_host_evidence VM_STAYS_ON=1\n' >&2
  exit 1
fi

language_inventory_command='powershell.exe -NoProfile -NonInteractive -Command "Get-WinUserLanguageList | Select-Object LanguageTag,InputMethodTips | ConvertTo-Json -Compress -Depth 4"'
"${SSH[@]}" "$target" "$language_inventory_command" | tr -d '\r' \
  > "$OUT/ime-inventory-before.json"

read -r -d '' remote_script_template <<'POWERSHELL' || true
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = '__REMOTE_EXE__'
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$p = New-Object System.Diagnostics.Process
$p.StartInfo = $psi
[void]$p.Start()
if (-not $p.WaitForExit(__TIMEOUT__)) {
    $p.Kill()
    $p.WaitForExit()
    exit 124
}
$stdout = $p.StandardOutput.ReadToEnd()
$stderr = $p.StandardError.ReadToEnd()
[Console]::Out.Write($stdout)
[Console]::Error.Write($stderr)
exit $p.ExitCode
POWERSHELL
powershell_script="${remote_script_template//__REMOTE_EXE__/$remote_exe_win}"
powershell_script="${powershell_script//__TIMEOUT__/$RUN_TIMEOUT_MS}"
printf '%s\n' "$powershell_script" > "$OUT/remote-script.ps1"
encoded_command="$(iconv -f UTF-8 -t UTF-16LE "$OUT/remote-script.ps1" | base64 | tr -d '\n')"
remote_command="powershell.exe -NoProfile -NonInteractive -EncodedCommand $encoded_command"
remote_script_sha="$(sha256sum "$OUT/remote-script.ps1" | { read -r hash _; printf '%s' "$hash"; })"
printf 'target=%s\ncommand_path=%s\nargv_count=0\ntimeout_ms=%s\nexe_sha256=%s\nremote_script_sha256=%s\nremote_command=%s\n' \
  "$target" "$remote_exe_win" "$RUN_TIMEOUT_MS" "$local_sha" \
  "$remote_script_sha" "$remote_command" > "$OUT/command.txt"

set +e
runtime_output="$("${SSH[@]}" "$target" "$remote_command" 2>&1)"
remote_status=$?
set -e
printf '%s\n' "$runtime_output" | tr -d '\r' > "$OUT/runtime.txt"
printf 'runner_exit=%s\ntimeout_triggered=%s\n' \
  "$remote_status" "$([[ "$remote_status" -eq 124 ]] && printf 1 || printf 0)" \
  > "$OUT/exit.txt"
"${SSH[@]}" "$target" "$language_inventory_command" | tr -d '\r' \
  > "$OUT/ime-inventory-after.json"
inventory_before_sha="$(sha256sum "$OUT/ime-inventory-before.json" | { read -r hash _; printf '%s' "$hash"; })"
inventory_after_sha="$(sha256sum "$OUT/ime-inventory-after.json" | { read -r hash _; printf '%s' "$hash"; })"
if ! cmp -s "$OUT/ime-inventory-before.json" "$OUT/ime-inventory-after.json"; then
  printf 'unchanged=0\nbefore_sha256=%s\nafter_sha256=%s\n' \
    "$inventory_before_sha" "$inventory_after_sha" > "$OUT/inventory-comparison.txt"
  printf 'P016 O6 ACTIVE IME GATE ERROR: language_inventory_changed VM_STAYS_ON=1\n' >&2
  exit 1
fi
printf 'unchanged=1\nbefore_sha256=%s\nafter_sha256=%s\n' \
  "$inventory_before_sha" "$inventory_after_sha" > "$OUT/inventory-comparison.txt"
cat "$OUT/runtime.txt"

if [[ "$remote_status" -ne 0 ]]; then
  printf 'P016 O6 ACTIVE IME GATE ERROR: runner_exit=%s VM_STAYS_ON=1\n'     "$remote_status" >&2
  exit "$remote_status"
fi

canonical_evidence_regex="^P016 O6 WIN32 PARITY GREEN: typo_samples=[0-9]+ baseline_mpx=[0-9]+ advance_mpx=[0-9]+ missing_glyphs=[0-9]+ ime_ev=([0-9]+) ime_samples=([0-9]+) ime=([0-9]+)/([0-9]+)/([0-9]+) a11y=[0-9]+/[0-9]+/[0-9]+ monitors=[0-9]+ dpi=[0-9]+\.\.[0-9]+ hkl=[0-9A-Fa-f]+ klid=[0-9A-Fa-f]{8} is_ime=([01]) inventory_unchanged=1 checks=[0-9]+$"

mapfile -t canonical_lines < <(grep -E "$canonical_evidence_regex" "$OUT/runtime.txt" || true)
ime_line_count="$(grep -Ec '(^|[[:space:]])ime_ev=' "$OUT/runtime.txt" || true)"
if [[ "${#canonical_lines[@]}" -ne 1 || "$ime_line_count" -ne 1 ]]; then
  printf 'P016 O6 ACTIVE IME GATE ERROR: canonical_evidence_count=%u ime_line_count=%s VM_STAYS_ON=1\n' \
    "${#canonical_lines[@]}" "$ime_line_count" >&2
  exit 1
fi
evidence_line="${canonical_lines[0]}"
if ! [[ "$evidence_line" =~ $canonical_evidence_regex ]]; then
  printf 'P016 O6 ACTIVE IME GATE ERROR: canonical_evidence_parse VM_STAYS_ON=1\n' >&2
  exit 1
fi
ime_evidence="${BASH_REMATCH[1]}"
ime_samples="${BASH_REMATCH[2]}"
ime_value_a="${BASH_REMATCH[3]}"
ime_value_b="${BASH_REMATCH[4]}"
ime_value_c="${BASH_REMATCH[5]}"
active_layout_is_ime="${BASH_REMATCH[6]}"
printf '%s\n' "$evidence_line" > "$OUT/canonical-evidence.txt"
sha256sum -c "$shasum_sources" > "$OUT/source-check-before-final.txt"
sha256sum \
  "$OUT/active-ime-gate.exe" "$OUT/command.txt" "$OUT/exit.txt" \
  "$OUT/remote-script.ps1" "$OUT/ime-inventory-before.json" \
  "$OUT/ime-inventory-after.json" "$OUT/inventory-comparison.txt" \
  "$OUT/runtime.txt" "$OUT/canonical-evidence.txt" "$OUT/source-sha256.txt" \
  "$OUT/source-check-after-build.txt" "$OUT/source-check-before-final.txt" \
  "$OUT/local-host-toolchain.txt" "$OUT/remote-host.json" \
  "$OUT/artifact-hash.txt" > "$OUT/final-evidence-sha256.txt"

set +e
moo_windows_active_ime_evidence_classify \
  "$ime_evidence" "$ime_samples" "$ime_value_a" "$ime_value_b" \
  "$ime_value_c" "$active_layout_is_ime"
evidence_status=$?
set -e

case "$evidence_status" in
  0)
    printf 'P016 O6 ACTIVE IME EVIDENCE_READY: ime=2/1/1 samples=4 VM_STAYS_ON=1 artifacts=%s\n' "$OUT"
    exit 0
    ;;
  77)
    printf 'P016 O6 ACTIVE IME PLATFORM_ENV_UNAVAILABLE: ime=0/0/0 samples=0 VM_STAYS_ON=1 artifacts=%s\n' "$OUT"
    exit 77
    ;;
  *)
    printf 'P016 O6 ACTIVE IME GATE ERROR: unexpected_evidence VM_STAYS_ON=1\n' >&2
    exit 1
    ;;
esac
