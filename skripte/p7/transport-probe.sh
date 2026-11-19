#!/usr/bin/env bash
# P7-T11: dieselbe Linux-Probe im isolierten QEMU-Netz und im N100-Runner.
set -euo pipefail
ziel="$1"
interface="$2"
quelle="$3"
ausgabe="$4"
mkdir -p -- "$ausgabe"
fehler=0
if ! uv run --no-project --offline python - "$ziel" "$quelle" "$ausgabe" >"$ausgabe/udp.log" 2>&1 <<'PY'
import pathlib, socket, sys, time

ziel, quelle, ordner = sys.argv[1:]
ausgabe = pathlib.Path(ordner)
anfrage = b"Moo UDP\x00Echo!"
(ausgabe / "udp-anfrage.bin").write_bytes(anfrage)
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
    sock.settimeout(3)
    sock.bind((quelle, 0))
    sock.connect((ziel, 7))
    print(f"UDP {sock.getsockname()} -> {sock.getpeername()}, {len(anfrage)} Bytes", flush=True)
    start = time.monotonic_ns()
    sock.sendall(anfrage)
    antwort = sock.recv(65535)
    rtt_ms = (time.monotonic_ns() - start) / 1_000_000
(ausgabe / "udp-antwort.bin").write_bytes(antwort)
if antwort != anfrage:
    sys.exit(f"UDP-Echo abweichend: {anfrage.hex()} -> {antwort.hex()}")
print(f"UDP-Echo bytegleich, RTT {rtt_ms:.3f} ms, Nutzlast {antwort.hex()}")
PY
then
  fehler=1
fi
cat "$ausgabe/udp.log"
if LC_ALL=C curl -4 --interface "$interface" --noproxy '*' --max-time 5 \
    --http1.1 --fail --silent --show-error --verbose \
    --dump-header "$ausgabe/tcp-kopf.log" --output "$ausgabe/tcp-antwort.txt" \
    "http://$ziel:8080/" >"$ausgabe/tcp.log" 2>&1 &&
   grep -Fq 'HTTP/1.1 200 OK' "$ausgabe/tcp-kopf.log" &&
   cmp -s "$ausgabe/tcp-antwort.txt" <(printf 'Moo TCP\n'); then
  printf '%s\n' 'TCP/8080 HTTP 200 und exakt 8 Nutzbytes' >>"$ausgabe/tcp.log"
else
  fehler=1
  printf '%s\n' 'TCP/8080 HTTP-Antwort fehlt oder weicht ab' >&2
fi
cat "$ausgabe/tcp.log"
exit "$fehler"
