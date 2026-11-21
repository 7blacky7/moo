#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out=build/p11/moofs
mkdir -p "$out"
uv run --no-project --offline python -B - "$out" <<'PY'
from pathlib import Path
import sys
out = Path(sys.argv[1])
(out / "quelle.bin").write_bytes(bytes((i * 37 + 11) & 255 for i in range(1048576)))
for name in ("abbild.img", "leer.img"):
    (out / name).unlink(missing_ok=True)
PY
"$compiler" compile beispiele/werkzeuge/mkfs_moofs.moos -o "$out/mkfs"
"$compiler" compile compiler/tests/p11/moofs_abbild.moos -o "$out/abbild"
MOO_MOOFS_BILD="$PWD/$out/abbild.img" MOO_MOOFS_BYTES=4194304 MOO_MOOFS_QUELLE="$PWD/$out/quelle.bin" MOO_MOOFS_NAME=daten.bin "$out/mkfs"
MOO_MOOFS_PRUEF="$PWD/$out/abbild.img" "$out/abbild" > "$out/abbild.txt"
printf '0\n0\n' > "$out/abbild.expected"
diff -u "$out/abbild.expected" "$out/abbild.txt"
before=$(sha256sum "$out/abbild.img")
if MOO_MOOFS_BILD="$PWD/$out/abbild.img" MOO_MOOFS_BYTES=4194304 MOO_MOOFS_QUELLE= "$out/mkfs" > "$out/existiert.log"; then
  echo 'mkfs hat vorhandenes Ziel akzeptiert' >&2
  exit 1
fi
test "$before" = "$(sha256sum "$out/abbild.img")"
MOO_MOOFS_BILD="$PWD/$out/leer.img" MOO_MOOFS_BYTES=16384 MOO_MOOFS_QUELLE= "$out/mkfs"
uv run --no-project --offline python -B - "$out" <<'PY'
from pathlib import Path
import sys
out = Path(sys.argv[1])
data = bytearray((out / "abbild.img").read_bytes())
# Vier Records (Root-Inode, Dirent, Datei-Inode, Extent), ein Blatt:
# Daten beginnen bei Block 3. Nur das Testmedium wird beschädigt.
data[3 * 4096 + 64] ^= 1
(out / "bitkipper.img").write_bytes(data)
PY
MOO_MOOFS_PRUEF="$PWD/$out/bitkipper.img" "$out/abbild" > "$out/bitkipper.txt"
printf '262145\n3\n' > "$out/bitkipper.expected"
diff -u "$out/bitkipper.expected" "$out/bitkipper.txt"
echo 'MooFS POSIX: 1 MiB bytegleich, O_EXCL, leeres Bild und Bitkipper-Blocknummer grün'
