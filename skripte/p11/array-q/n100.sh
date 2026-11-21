#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out="$PWD/build/p11/array-q/t26"
# Reguläre Datei: SMB-Helfer löst Symlinks vor dem Übertragen auf.
cp -L "$out/deps/usr/lib/libisal.so.2" "$out/libisal.so.2"
for datei in "$out/moo-p11-gegen-c" "$out/libisal.so.2" "$out/faktor/moo-p11-faktor" "$out/simd-puffer/moo-p11-simd-puffer" skripte/p11/array-q/n100-wsl.ps1; do
  uv run --no-project --with pywinrm python skripte/p2/windows_minipc/smb_upload.py "$datei"
done
uv run --no-project --with pywinrm python skripte/p2/minipc_winrm.py run 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "D:\MooTest\n100-wsl.ps1"'
