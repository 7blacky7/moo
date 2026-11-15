#!/usr/bin/env bash
# P2-T25 (Plan 962e249a): Retained-Netz-Gates.
#   layout       Eckpunkt == MooVertex (C-Header aus dem Compiler + statischer C-Vergleich)
#   ohne-fenster Fehler ohne Backend fangbar, Pin geloest (immer lauffaehig)
#   xvfb         GL33 im PRIVATEN Xvfb (llvmpipe, nie Desktop): Bildgleichheit,
#                Fehlerfaelle, Lebensdauer, Matrix, Schliessen mit lebenden Netzen
#   messen       wie xvfb mit n=256 (131072 Dreiecke) + Zeiten; llvmpipe ist
#                keine GPU-Leistungsaussage
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
out=build/p2-grafik/netz3d
mkdir -p "$out"
modus="${1:-alle}"

layout() {
  timeout 30s "$compiler" layout stdlib/netz3d.moos --c-header > "$out/netz3d_layout.h"
  timeout 30s cc -std=c11 -Wall -Wextra -Werror -I"$out" -Icompiler/runtime \
    -c compiler/tests/p2/netz3d_layout_check.c -o "$out/layout_check.o"
  # Kontrollprobe: ein verfaelschter Header muss scheitern.
  mkdir -p "$out/kontrolle"
  sed 's/vec3 normale;/vec3 normale; float extra;/' "$out/netz3d_layout.h" > "$out/kontrolle/netz3d_layout.h"
  if cc -std=c11 -w -I"$out/kontrolle" -Icompiler/runtime -c compiler/tests/p2/netz3d_layout_check.c \
      -o "$out/kontrolle.o" 2>/dev/null; then
    echo "FAIL layout: Kontrollprobe nicht erkannt"; exit 1
  fi
  echo "PASS layout: Eckpunkt bytegleich zu MooVertex (Kontrollprobe erkannt)"
}

ohne_fenster() {
  timeout 120s "$compiler" compile compiler/tests/p2/netz3d_ohne_fenster.moos -o "$out/ohne_fenster"
  env -u DISPLAY -u WAYLAND_DISPLAY timeout 30s "$out/ohne_fenster" > "$out/ohne_fenster.out"
  diff -u compiler/tests/p2/netz3d_ohne_fenster.expected "$out/ohne_fenster.out"
  echo "PASS ohne-fenster"
}

szene() { # gross: 0|1
  for werkzeug in xvfb-run timeout; do
    command -v "$werkzeug" >/dev/null || { echo "FEHLT Voraussetzung: $werkzeug" >&2; exit 2; }
  done
  timeout 120s "$compiler" compile compiler/tests/p2/netz3d_szene.moos -o "$out/szene"
  laufzeit="$out/runtime"
  mkdir -p "$laufzeit"
  chmod 700 "$laufzeit"
  env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET -u NETZ3D_NUR_PFADE XDG_RUNTIME_DIR="$PWD/$laufzeit" \
    GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 \
    __GLX_VENDOR_LIBRARY_NAME=mesa GALLIUM_DRIVER=llvmpipe \
    MOO_KI_GPU=0 MOO_3D_BACKEND="${NETZ3D_BACKEND:-gl33}" NETZ3D_GROSS="$1" \
    xvfb-run -a timeout 600 "$out/szene" > "$out/szene_$1.out"
  cat "$out/szene_$1.out"
  # Review 6008ef18: jede fehlgeschlagene Pruefung laesst das Gate scheitern.
  if rg -q '^FAIL' "$out/szene_$1.out"; then echo "FAIL szene: fehlgeschlagene Pruefung (siehe oben)"; exit 1; fi
  rg -q -x 'NETZ3D PASS' "$out/szene_$1.out"
  for erwartet in \
    'netz3d: Eckpunkt- bzw. Indexanzahl nicht durch 3 teilbar' \
    'netz3d: Index ausserhalb der Eckpunkte' \
    'netz3d: puffer[f32]-Laenge nicht durch 9 teilbar' \
    'netz3d: Hochladen waehrend chunk_beginne nicht erlaubt' \
    'netz3d: kein 3D-Backend aktiv'; do
    rg -q -F "$erwartet" "$out/szene_$1.out" || { echo "FAIL Fehlerfall fehlt: $erwartet"; exit 1; }
  done
  echo "PASS szene (gross=$1, backend=${NETZ3D_BACKEND:-gl33})"
}

# NICHT gatend: Vulkan-Pfadlauf (test_frame_grab unter Vulkan unzuverlaessig).
# Faehrt alle Netzpfade bis zum Ende und protokolliert FAIL-Zeilen; Ergebnis ist
# eine Diagnose (DIAG), nie ein Gate-PASS.
vulkan_diagnose() {
  timeout 120s "$compiler" compile compiler/tests/p2/netz3d_szene.moos -o "$out/szene_diag"
  laufzeit="$out/runtime"
  mkdir -p "$laufzeit"
  chmod 700 "$laufzeit"
  set +e
  env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET XDG_RUNTIME_DIR="$PWD/$laufzeit" \
    GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 \
    MOO_KI_GPU=0 MOO_3D_BACKEND=vulkan NETZ3D_NUR_PFADE=1 NETZ3D_GROSS=0 \
    xvfb-run -a timeout 300 "$out/szene_diag" > "$out/vulkan_diag.out" 2> "$out/vulkan_diag.err"
  rc=$?
  set -e
  sed 's/^/DIAG /' "$out/vulkan_diag.out"
  echo "DIAG vulkan-diagnose rc=$rc, FAIL-Zeilen=$(rg -c '^FAIL' "$out/vulkan_diag.out" || echo 0) (kein Gate)"
}

case "$modus" in
  vulkan-diagnose) vulkan_diagnose ;;
  layout) layout ;;
  ohne-fenster) ohne_fenster ;;
  xvfb) szene 0 ;;
  messen) szene 1 ;;
  alle) layout; ohne_fenster; szene 0 ;;
  *) echo "Unbekannter Modus" >&2; exit 2 ;;
esac
