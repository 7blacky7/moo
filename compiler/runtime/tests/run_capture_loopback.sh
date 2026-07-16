#!/usr/bin/env bash
set -euo pipefail

if [[ "${MOO_CAPTURE_LOOPBACK:-0}" != "1" ]]; then
  echo "SKIP: setze MOO_CAPTURE_LOOPBACK=1 auf einem privilegierten Linux-Runner"
  exit 77
fi
for cmd in ffmpeg speaker-test aplay v4l2-ctl; do
  command -v "$cmd" >/dev/null || { echo "SKIP: $cmd fehlt"; exit 77; }
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
COMPILER="$ROOT/compiler/target/debug/moo-compiler"
[[ -x "$COMPILER" ]] || { echo "FAIL: zuerst mise run build"; exit 1; }

CAM="${MOO_CAPTURE_CAMERA_DEVICE:-}"
if [[ -z "$CAM" ]]; then
  for dev in /dev/video*; do
    [[ -e "$dev" ]] || continue
    if v4l2-ctl -d "$dev" --all 2>/dev/null | grep -qi "v4l2 loopback"; then CAM="$dev"; break; fi
  done
fi
[[ -n "$CAM" && -e "$CAM" ]] || { echo "SKIP: kein v4l2loopback-Geraet"; exit 77; }

CAPTURE="${MOO_CAPTURE_AUDIO_DEVICE:-hw:Loopback,1,0}"
PLAYBACK="${MOO_CAPTURE_AUDIO_PLAYBACK_DEVICE:-hw:Loopback,0,0}"
aplay -l 2>/dev/null | grep -qi loopback || { echo "SKIP: snd-aloop nicht geladen"; exit 77; }

ffmpeg -nostdin -loglevel error -re -f lavfi -i testsrc=size=320x240:rate=30   -pix_fmt yuyv422 -f v4l2 "$CAM" &
VIDEO_PID=$!
speaker-test -q -D "$PLAYBACK" -r 48000 -c 1 -t sine -f 1000 &
AUDIO_PID=$!
cleanup() { kill "$VIDEO_PID" "$AUDIO_PID" 2>/dev/null || true; wait "$VIDEO_PID" "$AUDIO_PID" 2>/dev/null || true; }
trap cleanup EXIT INT TERM

sleep 1
OUT="$(MOO_CAPTURE_CAMERA_DEVICE="$CAM" MOO_CAPTURE_AUDIO_DEVICE="$CAPTURE"   timeout 15 "$COMPILER" run "$ROOT/beispiele/ki_capture_loopback_gate.moos" 2>&1)"
printf '%s\n' "$OUT"
grep -q "SELFTEST_RESULT: PASS ki_capture_loopback" <<<"$OUT"
