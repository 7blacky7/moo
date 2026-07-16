#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RT="$ROOT/compiler/runtime"
OUT="${1:-${TMPDIR:-/tmp}/moo-p016-instrumented-devtools-gate}"
WINVM_HOST="${MOO_WINVM_HOST:-192.168.50.246}"
WINVM_USER="${MOO_WINVM_USER:-pro}"
WINVM_KEY="${MOO_WINVM_KEY:-/tmp/moo-winvm-ed25519}"
RUN_TIMEOUT_MS="${MOO_WINVM_DEVTOOLS_TIMEOUT_MS:-30000}"
CC="${MINGW_CC:-x86_64-w64-mingw32-gcc}"
CXX="${MINGW_CXX:-x86_64-w64-mingw32-g++}"

for tool in "$CC" "$CXX" ssh scp sha256sum grep tr iconv base64 uname wc; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'P016 O6 INSTRUMENTED DEVTOOLS GATE ERROR: missing_tool=%s\n' "$tool" >&2
    exit 2
  fi
done
if [[ ! -r "$WINVM_KEY" ]]; then
  printf 'P016 O6 INSTRUMENTED DEVTOOLS GATE ERROR: unreadable_key=%s\n' "$WINVM_KEY" >&2
  exit 2
fi
case "$RUN_TIMEOUT_MS" in
  ''|*[!0-9]*|0)
    printf 'P016 O6 INSTRUMENTED DEVTOOLS GATE ERROR: invalid_timeout_ms=%s\n' "$RUN_TIMEOUT_MS" >&2
    exit 2
    ;;
esac

mkdir -p "$OUT"
shasum_sources="$OUT/source-sha256.txt"
sha256sum \
  "$ROOT/scripts/windows_instrumented_devtools_gate.sh" \
  "$RT/tests/test_ui_host_parity_win32_instrumented.c" \
  "$RT/moo_ui_host_parity.c" \
  "$RT/moo_ui_host_parity_win32.c" \
  "$RT/moo_ui_host_parity_win32_dwrite.cpp" \
  "$RT/moo_ui_host_parity_instrumentation.c" \
  "$RT/moo_input_core.c" \
  "$RT/moo_a11y_core.c" \
  "$RT/moo_ui_host_parity_devtools.c" \
  "$RT/moo_ui_host_parity_helper_win32.c" \
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
  "$RT/moo_input_core.h" \
  "$RT/moo_input_protocol.h" \
  "$RT/moo_ui_host_parity_devtools.h" \
  "$RT/moo_ui_host_parity_helper_win32.h" \
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

"$CC" "${CFLAGS[@]}" -c "$RT/tests/test_ui_host_parity_win32_instrumented.c" -o "$OUT/test.o"
"$CC" "${CFLAGS[@]}" -c "$RT/moo_ui_host_parity.c" -o "$OUT/common.o"
"$CC" "${CFLAGS[@]}" -c "$RT/moo_ui_host_parity_win32.c" -o "$OUT/win32.o"
"$CC" "${CFLAGS[@]}" -c "$RT/moo_ui_host_parity_instrumentation.c" -o "$OUT/instrumentation.o"
"$CC" "${LEGACY_MASK_CFLAGS[@]}" -c "$RT/moo_input_core.c" -o "$OUT/input-core.o"
"$CC" "${LEGACY_MASK_CFLAGS[@]}" -c "$RT/moo_a11y_core.c" -o "$OUT/a11y-core.o"
"$CC" "${CFLAGS[@]}" -c "$RT/moo_ui_host_parity_devtools.c" -o "$OUT/devtools.o"
"$CC" "${CFLAGS[@]}" -c "$RT/moo_ui_host_parity_helper_win32.c" -o "$OUT/helper-win32.o"
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
  "$OUT/input-core.o" "$OUT/a11y-core.o" "$OUT/devtools.o" "$OUT/helper-win32.o" \
  "$OUT/compositor-core.o" "$OUT/effects-state.o" "$OUT/raster.o" \
  "$OUT/effects-damage.o" "$OUT/effects-cpu.o" "$OUT/effects-math.o" \
  "$OUT/animation.o" "$OUT/dwrite.o" \
  -lgdi32 -luser32 -limm32 -loleacc -loleaut32 -lole32 -luuid -ldwrite \
  -o "$OUT/instrumented-devtools-gate.exe"
sha256sum -c "$shasum_sources" > "$OUT/source-check-after-build.txt"

local_sha="$(sha256sum "$OUT/instrumented-devtools-gate.exe" | { read -r hash _; printf '%s' "$hash"; })"
run_id="p016-o6-instrumented-devtools-${local_sha:0:16}"
remote_dir_scp="C:/moo-runs/$run_id"
remote_exe_win="C:\\moo-runs\\$run_id\\instrumented-devtools-gate.exe"
target="$WINVM_USER@$WINVM_HOST"
SSH=(ssh -F /dev/null -i "$WINVM_KEY" -o StrictHostKeyChecking=no -o WarnWeakCrypto=no -o ConnectTimeout=10 -o ServerAliveInterval=5 -o ServerAliveCountMax=3)
SCP=(scp -F /dev/null -i "$WINVM_KEY" -o StrictHostKeyChecking=no -o WarnWeakCrypto=no -o ConnectTimeout=10)

"${SSH[@]}" "$target"  "powershell.exe -NoProfile -NonInteractive -Command \"[System.IO.Directory]::CreateDirectory('C:\\moo-runs\\$run_id') | Out-Null\""
"${SCP[@]}" "$OUT/instrumented-devtools-gate.exe"   "$target:$remote_dir_scp/instrumented-devtools-gate.exe"

remote_sha="$("${SSH[@]}" "$target"   "powershell.exe -NoProfile -NonInteractive -Command \"(Get-FileHash -Algorithm SHA256 -LiteralPath '$remote_exe_win').Hash.ToLowerInvariant()\""   | tr -d '\r\n')"
if [[ "$remote_sha" != "$local_sha" ]]; then
  printf 'local_sha256=%s\nremote_sha256=%s\nequal=0\n' \
    "$local_sha" "$remote_sha" > "$OUT/artifact-hash.txt"
  printf 'P016 O6 INSTRUMENTED DEVTOOLS GATE ERROR: hash_mismatch local=%s remote=%s\n' \
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
  printf 'P016 O6 INSTRUMENTED DEVTOOLS GATE ERROR: invalid_remote_host_evidence VM_STAYS_ON=1\n' >&2
  exit 1
fi

read -r -d '' remote_script_template <<'POWERSHELL' || true
$ProgressPreference = [System.Management.Automation.ActionPreference]::SilentlyContinue
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
cat "$OUT/runtime.txt"

if [[ "$remote_status" -ne 0 ]]; then
  printf 'P016 O6 INSTRUMENTED DEVTOOLS GATE ERROR: runner_exit=%s VM_STAYS_ON=1\n' \
    "$remote_status" >&2
  exit "$remote_status"
fi

expected_evidence_line='P016 O6 WIN32 INSTRUMENTED DEVTOOLS GREEN: sealed=1/2/2/2/0 default=3/0 unsealed=3/0 checks=59'
runtime_line_count="$(wc -l < "$OUT/runtime.txt")"
mapfile -t canonical_lines < <(grep -Fx "$expected_evidence_line" "$OUT/runtime.txt" || true)
marker_line_count="$(grep -Fc 'P016 O6 WIN32 INSTRUMENTED DEVTOOLS GREEN:' "$OUT/runtime.txt" || true)"
if [[ "$runtime_line_count" -ne 1 || "${#canonical_lines[@]}" -ne 1 ||
      "$marker_line_count" -ne 1 ]]; then
  printf 'P016 O6 INSTRUMENTED DEVTOOLS GATE ERROR: runtime_line_count=%s canonical_evidence_count=%u marker_line_count=%s VM_STAYS_ON=1\n' \
    "$runtime_line_count" "${#canonical_lines[@]}" "$marker_line_count" >&2
  exit 1
fi
evidence_line="${canonical_lines[0]}"
if [[ "$evidence_line" != "$expected_evidence_line" ]]; then
  printf 'P016 O6 INSTRUMENTED DEVTOOLS GATE ERROR: canonical_evidence_parse VM_STAYS_ON=1\n' >&2
  exit 1
fi
printf '%s\n' "$evidence_line" > "$OUT/canonical-evidence.txt"

sha256sum -c "$shasum_sources" > "$OUT/source-check-before-final.txt"
sha256sum \
  "$OUT/instrumented-devtools-gate.exe" "$OUT/command.txt" "$OUT/exit.txt" \
  "$OUT/remote-script.ps1" "$OUT/runtime.txt" "$OUT/canonical-evidence.txt" \
  "$OUT/source-sha256.txt" "$OUT/source-check-after-build.txt" \
  "$OUT/source-check-before-final.txt" "$OUT/local-host-toolchain.txt" \
  "$OUT/remote-host.json" "$OUT/artifact-hash.txt" \
  > "$OUT/final-evidence-sha256.txt"

printf 'P016 O6 INSTRUMENTED DEVTOOLS EVIDENCE_READY: sealed=2/2/0 default=UNSUPPORTED unsealed=UNSUPPORTED VM_STAYS_ON=1 artifacts=%s\n' "$OUT"
exit 0
