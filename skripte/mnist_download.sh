#!/usr/bin/env bash
# ============================================================
# MNIST herunterladen + verifizieren (Plan-014 E1).
# Quelle: ossci-datasets.s3.amazonaws.com (PyTorch-CI-Mirror des
# Original-Datensatzes von Yann LeCun — das Original yann.lecun.com
# ist haeufig nicht erreichbar).
# SHA256-Pinning: Hashes der .gz-Dateien wurden beim Erst-Download
# am 2026-07-05 von genau diesem Mirror gemessen und hier fixiert —
# jede spaetere Abweichung bricht ab.
# Ziel: daten/mnist/ (NICHT im Git — siehe .gitignore).
# ============================================================
set -euo pipefail

BASIS="https://ossci-datasets.s3.amazonaws.com/mnist"
ZIEL="$(dirname "$0")/../daten/mnist"
mkdir -p "$ZIEL"

# Datei|SHA256 der .gz
DATEIEN=(
  "train-images-idx3-ubyte.gz|440fcabf73cc546fa21475e81ea370265605f56be210a4024d2ca8f203523609"
  "train-labels-idx1-ubyte.gz|3552534a0a558bbed6aed32b30c495cca23d567ec52cac8be1a0730e8010255c"
  "t10k-images-idx3-ubyte.gz|8d422c7b0a1c1c79245a5bcf07fe86e33eeafee792b84584aec276f5a2dbc4e6"
  "t10k-labels-idx1-ubyte.gz|f7ae60f92e00ec6debd23a6088c31dbd2371eca3ffa0defaefb259924204aec6"
)

for eintrag in "${DATEIEN[@]}"; do
  datei="${eintrag%%|*}"
  soll="${eintrag##*|}"
  entpackt="${datei%.gz}"
  if [[ -f "$ZIEL/$entpackt" ]]; then
    echo "OK (vorhanden): $entpackt"
    continue
  fi
  echo "Lade $datei ..."
  curl -fsSL "$BASIS/$datei" -o "$ZIEL/$datei"
  ist="$(sha256sum "$ZIEL/$datei" | cut -d' ' -f1)"
  if [[ "$ist" != "$soll" ]]; then
    echo "FEHLER: SHA256 von $datei stimmt nicht!" >&2
    echo "  erwartet: $soll" >&2
    echo "  gemessen: $ist" >&2
    rm -f "$ZIEL/$datei"
    exit 1
  fi
  gunzip -f "$ZIEL/$datei"
  echo "OK (geladen + verifiziert): $entpackt"
done

echo "MNIST bereit unter: $ZIEL"
echo "Nutzung in moo:  mnist_laden(\"daten/mnist/train\")  bzw.  mnist_laden(\"daten/mnist/t10k\")"
