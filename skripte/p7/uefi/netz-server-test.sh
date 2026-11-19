#!/usr/bin/env bash
# Regression fuer den echten mise/Serverpfad. IP-Adressen nur im eigenen Netns.
set -euo pipefail
export LC_ALL=C
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
LOADER="${1:?EFI-Testloader als Argument angeben}"
if [ "${MOO_NETZ_TEST_NS:-0}" != 1 ]; then
  exec unshare -Urpn --fork --mount-proc -- env MOO_NETZ_TEST_NS=1 bash "$0" "$@"
fi
[ "$(ip -o link show | wc -l)" -eq 1 ]
export MISE_TRUSTED_CONFIG_PATHS="${MISE_TRUSTED_CONFIG_PATHS:+$MISE_TRUSTED_CONFIG_PATHS:}$ROOT"
mkdir -p "$ROOT/build"
T="$(mktemp -d "$ROOT/build/p7-netz-server-test.XXXXXX")"
mkdir "$T/bin"
echo "Artefakte: $T"
export PATH="$T/bin:$PATH"
export TEST_DEV=v6restore0
export TEST_UUID=12345678-abcd-4321-abcd-123456789012

cat >"$T/bin/sudo" <<'MOCK'
#!/usr/bin/env bash
set -eu
[ "$1" = -n ]; shift
if [[ "$*" == "ip -6 addr del "* ]]; then
  "$@"
  if [[ "$TEST_CASE" == folgesignal ]]; then kill -TERM "$PPID"; fi
  exit 0
fi
if [[ "$TEST_CASE" == fehlgeschlagen && "$*" == "sysctl -q -w net/ipv6/conf/$TEST_DEV/disable_ipv6=1" ]]; then
  exit 23
fi
case "$1" in
  true|ip|sysctl|nmcli|iptables|dnsmasq) exec "$@" ;;
  *) exit 99 ;;
esac
MOCK
cat >"$T/bin/nmcli" <<'MOCK'
#!/usr/bin/env bash
set -eu
if [ "$1" = -w ]; then shift 2; fi
case "$*" in
  "-g GENERAL.NM-MANAGED device show $TEST_DEV") cat "$TEST_DIR/managed" ;;
  "-g GENERAL.CON-UUID device show $TEST_DEV") cat "$TEST_DIR/uuid" ;;
  "device set $TEST_DEV managed no")
    echo no >"$TEST_DIR/managed"
    : >"$TEST_DIR/uuid"
    sysctl -q -w "net/ipv6/conf/$TEST_DEV/disable_ipv6=1"
    echo nm-aus >>"$TEST_DIR/trace"
    ;;
  "device set $TEST_DEV managed yes")
    echo yes >"$TEST_DIR/managed"
    echo nm-an >>"$TEST_DIR/trace"
    ;;
  "connection up uuid $TEST_UUID ifname $TEST_DEV")
    echo "$TEST_UUID" >"$TEST_DIR/uuid"
    echo nm-uuid >>"$TEST_DIR/trace"
    ;;
  *) exit 99 ;;
esac
MOCK
cat >"$T/bin/iptables" <<'MOCK'
#!/usr/bin/env bash
set -eu
case "$1" in
  -I) : >"$TEST_DIR/firewall" ;;
  -D) rm "$TEST_DIR/firewall" ;;
  *) exit 99 ;;
esac
MOCK
cat >"$T/bin/dnsmasq" <<'MOCK'
#!/usr/bin/env bash
trap 'echo dnsmasq-term >>"$TEST_DIR/trace"; exit 0' TERM
echo 'dnsmasq-tftp: TFTP root is synthetic'
while :; do sleep 0.1; done
MOCK
chmod +x "$T/bin/"*

kurz_adressen() { ip -6 -o addr show dev "$TEST_DEV" scope link | awk '{print $4}' | sort; }

for profil in folgesignal verwaltet fehlgeschlagen nur-link; do
  export TEST_CASE="$profil" TEST_DIR="$T/$profil"
  mkdir -p "$TEST_DIR/steuerung"
  : >"$TEST_DIR/trace"
  : >"$TEST_DIR/uuid"
  echo no >"$TEST_DIR/managed"
  ip link add "$TEST_DEV" type dummy
  sysctl -q -w "net/ipv6/conf/$TEST_DEV/addr_gen_mode=1" "net/ipv6/conf/$TEST_DEV/disable_ipv6=1"
  ip link set "$TEST_DEV" up
  if [ "$profil" = verwaltet ]; then
    echo yes >"$TEST_DIR/managed"
    echo "$TEST_UUID" >"$TEST_DIR/uuid"
    sysctl -q -w "net/ipv6/conf/$TEST_DEV/disable_ipv6=0"
    ip -6 addr add fe80::72/64 dev "$TEST_DEV" nodad
    ip -6 addr add fe80::73/64 dev "$TEST_DEV" nodad
  elif [ "$profil" != nur-link ]; then
    ip -4 addr add 192.168.77.1/24 dev "$TEST_DEV"
  fi
  sysctl -n "net/ipv6/conf/$TEST_DEV/disable_ipv6" >"$TEST_DIR/disable.before"
  kurz_adressen >"$TEST_DIR/ll.before"
  ip -4 -o addr show dev "$TEST_DEV" >"$TEST_DIR/v4.before"

  link=0
  bereit='TFTP root is synthetic'
  loader="$LOADER"
  if [ "$profil" = nur-link ]; then
    link=1
    bereit='MOO-NETZ LINK BEREIT'
    loader="$TEST_DIR/kein-loader.efi"
  fi
  MOO_NETZ_NUR_LINK="$link" MOO_NETZ_IF="$TEST_DEV" MOO_NETZ_TFTP="$TEST_DIR/tftp" \
    MOO_NETZ_STEUERUNG="$TEST_DIR/steuerung" MOO_NETZ_IPV6=1 \
    setsid mise run -C "$ROOT/skripte/p7/uefi" netz-server "$loader" >"$TEST_DIR/server.log" 2>&1 &
  pid=$!
  for ((i=0; i<100; i++)); do
    grep -Fq "$bereit" "$TEST_DIR/server.log" && break
    kill -0 "$pid" 2>/dev/null || { cat "$TEST_DIR/server.log"; exit 1; }
    sleep 0.05
  done
  grep -Fq "$bereit" "$TEST_DIR/server.log"
  ip -4 -o addr show dev "$TEST_DEV" | grep -Fq 'inet 192.168.77.1/24'
  [ "$(sysctl -n "net/ipv6/conf/$TEST_DEV/disable_ipv6")" = 0 ]
  [ -n "$(kurz_adressen)" ]
  : >"$TEST_DIR/steuerung/stopp"
  for ((i=0; i<120; i++)); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.05
  done
  ! kill -0 "$pid" 2>/dev/null || { cat "$TEST_DIR/server.log"; exit 1; }
  if [ "$profil" = fehlgeschlagen ]; then
    if wait "$pid"; then exit 1; fi
    grep -qx 1 "$TEST_DIR/steuerung/fertig"
    grep -Fq 'MOO-NETZ IPV6 FEHLER' "$TEST_DIR/server.log"
  else
    wait "$pid" || { cat "$TEST_DIR/server.log"; exit 1; }
    grep -qx 0 "$TEST_DIR/steuerung/fertig"
    grep -Fq "MOO-NETZ IPV6 ZURUECK $TEST_DEV" "$TEST_DIR/server.log"
    sysctl -n "net/ipv6/conf/$TEST_DEV/disable_ipv6" >"$TEST_DIR/disable.after"
    kurz_adressen >"$TEST_DIR/ll.after"
    cmp "$TEST_DIR/disable.before" "$TEST_DIR/disable.after"
    cmp "$TEST_DIR/ll.before" "$TEST_DIR/ll.after"
  fi
  ip -4 -o addr show dev "$TEST_DEV" >"$TEST_DIR/v4.after"
  cmp "$TEST_DIR/v4.before" "$TEST_DIR/v4.after"
  [ ! -e "$TEST_DIR/firewall" ]
  if [ "$profil" = nur-link ]; then
    [ ! -e "$TEST_DIR/tftp" ]
    ! grep -Fq 'TFTP root is synthetic' "$TEST_DIR/server.log"
    ! grep -qx dnsmasq-term "$TEST_DIR/trace"
  else
    grep -qx dnsmasq-term "$TEST_DIR/trace"
  fi
  if [ "$profil" = verwaltet ]; then
    grep -qx yes "$TEST_DIR/managed"
    grep -qx "$TEST_UUID" "$TEST_DIR/uuid"
    grep -qx nm-uuid "$TEST_DIR/trace"
    [ -s "$TEST_DIR/steuerung/link-local.ip" ]
  else
    grep -qx no "$TEST_DIR/managed"
    ! grep -q '^nm-' "$TEST_DIR/trace"
  fi
  ip link del "$TEST_DEV"
  echo "PASS: $profil"
done
