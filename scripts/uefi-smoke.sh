#!/usr/bin/env bash
# P011-F1 + P7-T2: Reproduzierbarer UEFI-Boot-Smoke fuer moo als EFI-Application
# MIT Bildschirmnachweis (QEMU/OVMF-Screenshot).
#
# Baut aus FESTEN Projektdateien (kein /tmp-PoC mehr):
#   beispiele/uefi/hallo_uefi.moos           (moo-App: zeichnet MoOS-Bild per surface_*)
#   compiler/runtime/boot/uefi_entry.c      (efi_main, GOP-Adapter, Heap-Arena)
#   compiler/runtime/boot/uefi_rt.c         (Heap-/String-/libm-Bruecke, _fltused)
#   compiler/runtime/moo_bare.c, moo_bare_alloc.c, moo_freestanding_mem.c
#   compiler/runtime/moo_surface_core.c, moo_surface.c   (vorhandener Surface-Kern)
#   compiler/runtime/moo_native_math.c       (P2-T58: Float-%/** nativer Funktionen, kein libm)
#
# Phasen:
#   1) Moo für x86_64-unknown-uefi, Demo und runtime-Prüfung als LLVM-IR
#   2) Clang/MinGW freestanding baut die C-Runtime. Im Standardprofil wird
#      der erreichbare Code aus Moo- und C-Bitcode als COFF ausgegeben.
#   3) lld-link -> PE32+ EFI application (BOOTX64.EFI), /nodefaultlib /opt:ref
#   4) GPT-Disk mit ESP + EFI/BOOT/BOOTX64.EFI, OVMF-Boot mit -vga std,
#      Marker MOO-UEFI-ENTRY / GOP-OK / HEAP-OK / OK (debugcon 0xE9) sowie
#      MOO-UEFI-MOO + MOO-UEFI-BILD (COM1, aus dem moo-Teil),
#      Screenshot per QEMU-Monitor 'screendump', OCR mit tesseract (falls da),
#      Eingabe per Monitor: sendkey tab/ret -> Marker MOO-UEFI-FOKUS 1 /
#      MOO-UEFI-AKTION 1, usb-tablet mouse_move/mouse_button -> MOO-UEFI-KLICK /
#      MOO-UEFI-AKTION 0 (Maus fail-closed, wenn MOO-UEFI-MAUS-FEHLT),
#      sendkey esc -> Schleife endet -> MOO-UEFI-OK. Akzentlinie nach Aktion
#      bernstein (Pixelprobe mit magick, falls da).
#      Netzwerk-Konsole (P7-T9): -netdev user + e1000 + filter-dump -> net.pcap;
#      Marker MOO-UEFI-NETZ-OK und Nutzlast 'MOO-NET MOOS STARTET' sowie der
#      Tastenname 'Return' muessen im pcap stehen (grep -a).
#
# ARTEFAKTE: Standard temporaer (geloescht). MOO_UEFI_OUT=<dir> behaelt
# BOOTX64.EFI, uefi.img, screenshot.png, serial.log, debug.log, sha256.txt
# (so nutzt es skripte/p7/uefi/sicht-gate.sh). Es werden NUR Image-Dateien
# beschrieben, nie Blockgeraete.
#
# BEWEISKANAL: 0xE9 debugcon ist zuverlaessiger als COM1 im UEFI-Kontext
# (OVMF nutzt COM1 selbst). Der Entry schreibt auf beide; moo nur auf COM1.
#
# SKIP-POLITIK (P010): fehlendes Tooling => transparenter SKIP mit Exit 0.
# SKIP ist KEIN finaler Beweis — das Final-Gate verlangt den echten Boot.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${MOO_COMPILER:-$ROOT/compiler/target/release/moo-compiler}"
# P7-T14: MOO_UEFI_PRUEFUNG=e1000 prueft den Moo-e1000-Treiber (eigene App, eigener QEMU-Lauf).
PRUEFUNG="${MOO_UEFI_PRUEFUNG:-}"
NETZ_PING="${MOO_UEFI_PING:-0}"
NETZ_TRANSPORT="${MOO_UEFI_TRANSPORT:-0}"
NETZ_TCP_ECHO="${MOO_UEFI_TCP_ECHO:-0}"
KRYPTO="${MOO_UEFI_KRYPTO:-0}"
if [ "$PRUEFUNG" = "e1000" ]; then
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/beispiele/uefi/netz_e1000.moos}"
elif [ "$PRUEFUNG" = "lader" ]; then
  # P7-T10: Netz-Lader; Nutzlast ist die e1000-Pruef-App (eigener Lauf, s. u.).
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/beispiele/uefi/netz_lader.moos}"
elif [ "$PRUEFUNG" = "netz-kern" ]; then
  # P7-T5: N100-Weg; Stick-App ist der Netz-Lader (Ablauf s. u.).
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/beispiele/uefi/netz_lader.moos}"
elif [ "$PRUEFUNG" = "runtime" ]; then
  MOO_SRC="$ROOT/compiler/tests/p7/t12_uefi_probe.moos"
elif [ "$PRUEFUNG" = "nativ" ]; then
  # P7-T17: @nativ-Aufruf mit dynamischen Werten (eigene App, Negativfall s. u.).
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/compiler/tests/p7/nativ_uefi_dyn.moos}"
elif [ "$PRUEFUNG" = "nativ-trap" ]; then
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/compiler/tests/p7/nativ_uefi_dyn_trap.moos}"
elif [ "$PRUEFUNG" = "kern" ]; then
  # P7-T5: Moo-UEFI-Loader + Moo-Kern (eigener Ablauf vor den C-Phasen, s. u.).
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/beispiele/uefi/moo_lader.moos}"
else
  MOO_SRC="${MOO_UEFI_APP:-$ROOT/beispiele/uefi/hallo_uefi.moos}"
fi
ENTRY_C="$ROOT/compiler/runtime/boot/uefi_entry.c"
RT_C="$ROOT/compiler/runtime/boot/uefi_rt.c"
RT_SOURCES=(moo_bare.c moo_bare_alloc.c moo_freestanding_mem.c moo_surface_core.c moo_surface.c moo_native_math.c)
CFLAGS=(--target=x86_64-w64-windows-gnu -std=c11 -O2 -ffreestanding -fno-stack-protector
        -mno-stack-arg-probe -ffunction-sections -fdata-sections -DMOO_BARE)
WAIT_S="${MOO_UEFI_WAIT:-45}"
QEMU_ACCEL=()
[ "${MOO_UEFI_KVM:-0}" != 1 ] || QEMU_ACCEL+=(-enable-kvm -cpu host)

fail() { echo "      FAIL: $*"; echo "# ERGEBNIS: ROT"; exit 1; }
phase() { echo "############################################################"; echo "# PHASE $1: $2"; }
skip() {
  echo "      SKIP: $1"
  echo "      SKIP: Dies ist KEIN finaler Beweis — das Final-Gate verlangt den echten Boot (P010-Politik)."
  [ -n "${2:-}" ] && echo "      Hinweis (Arch/CachyOS): $2"
  echo "############################################################"
  echo "# ERGEBNIS: $3"
  echo "############################################################"
  exit 0
}

case "$KRYPTO" in
  0) ;;
  1|rdrand|ohne-quelle)
    [ "$PRUEFUNG" = kern ] && [ "${MOO_UEFI_SAMMEL:-0}" = 1 ] ||
      fail "KRYPTO braucht PRUEFUNG=kern und SAMMEL=1"
    if [ "$KRYPTO" != 1 ]; then
      CPU=()
      MODEL=max
      if [ "${MOO_UEFI_KVM:-0}" = 1 ]; then
        CPU=(-enable-kvm)
        MODEL=host
      fi
      MODEL+=,-rdseed
      [ "$KRYPTO" != ohne-quelle ] || MODEL+=,-rdrand
      QEMU_ACCEL=("${CPU[@]}" -cpu "$MODEL")
    fi
    ;;
  *) fail "KRYPTO: 0, 1, rdrand oder ohne-quelle" ;;
esac

case "$NETZ_TRANSPORT" in
  0) ;;
  1)
    [ "$NETZ_PING" != 0 ] || fail "TRANSPORT braucht PING=e1000 oder igb"
    command -v curl >/dev/null || fail "TRANSPORT braucht curl"
    ;;
  *) fail "TRANSPORT: 0 oder 1" ;;
esac

case "$NETZ_TCP_ECHO" in
  0) ;;
  1) [ "$NETZ_TRANSPORT" = 1 ] || fail "TCP_ECHO braucht TRANSPORT=1" ;;
  *) fail "TCP_ECHO: 0 oder 1" ;;
esac

case "$NETZ_PING" in
  0) ;;
  e1000|igb)
    [ "$PRUEFUNG" = kern ] && [ "${MOO_UEFI_NETZ:-1}" = 1 ] || fail "PING braucht PRUEFUNG=kern und NETZ=1"
    for t in ip ping unshare; do
      command -v "$t" >/dev/null || fail "PING-Gate braucht $t"
    done
    # Linux-ping und QEMU im selben privaten Namespace. Kein Slirp/Forwarding.
    if [ "${MOO_UEFI_PING_NS:-0}" != 1 ]; then
      exec unshare -Urn -- env MOO_UEFI_PING_NS=1 bash "$0" "$@"
    fi
    ;;
  *) fail "PING: 0, e1000 oder igb" ;;
esac

[ -x "$BIN" ] || fail "moo-compiler fehlt: $BIN (erst mise run p4-build-vulkan)"
[ -f "$MOO_SRC" ] || fail "moo-Quelle fehlt: $MOO_SRC"
if [ -n "${MOO_UEFI_OUT:-}" ]; then
  OUT="$MOO_UEFI_OUT"
  mkdir -p "$OUT" || fail "MOO_UEFI_OUT nicht anlegbar: $OUT"
  echo "      Artefakte bleiben in: $OUT"
else
  OUT="$(mktemp -d "${TMPDIR:-/tmp}/moo_uefi_smoke.XXXXXX")"
  trap 'rm -rf "$OUT"' EXIT
fi

# UDP-Nutzlasten gegen jede vollstaendige COM1-Kernzeile pruefen (auch vor NIC-Init).
kern_netz_pruefen() {
  uv run --no-project python - "$1" "$2" "$NETZ_PING" "$NETZ_TRANSPORT" <<'PY'
import collections, pathlib, struct, sys
raw = pathlib.Path(sys.argv[1]).read_bytes()
assert raw[:4] in (b"\xd4\xc3\xb2\xa1", b"\xa1\xb2\xc3\xd4")
endian = "<" if raw[0] == 0xd4 else ">"
assert struct.unpack_from(endian + "I", raw, 20)[0] == 1
packets, pos = [], 24
echo_test = sys.argv[3] != "0"
host_ip = bytes((192, 0, 2, 1))
guest_ip = bytes((192, 0, 2, 2))
guest_mac = bytes.fromhex("525400123459")
arp_requests, arp_replies = 0, 0
echo_requests, echo_replies = collections.Counter(), collections.Counter()
transport_test = sys.argv[4] == "1"
udp_requests, udp_replies = [], []
tcp_streams = collections.defaultdict(list)

def checksum_ok(data):
    data += b"\x00" * (len(data) % 2)
    return sum(struct.unpack("!%dH" % (len(data) // 2), data)) % 65535 == 0

while pos + 16 <= len(raw):
    size = struct.unpack_from(endian + "I", raw, pos + 8)[0]
    frame = raw[pos + 16:pos + 16 + size]
    pos += 16 + size
    if echo_test and len(frame) >= 42:
        if frame[12:20] == b"\x08\x06\x00\x01\x08\x00\x06\x04":
            if frame[20:22] == b"\x00\x01" and frame[28:32] == host_ip and frame[38:42] == guest_ip:
                arp_requests += 1
            if frame[20:22] == b"\x00\x02" and frame[28:32] == guest_ip and frame[38:42] == host_ip:
                assert frame[6:12] == frame[22:28] == guest_mac and frame[:6] == frame[32:38]
                arp_replies += 1
        if frame[12:14] == b"\x08\x00" and frame[14] == 0x45 and frame[23] == 1:
            size_ip = struct.unpack_from("!H", frame, 16)[0]
            icmp = frame[34:14 + size_ip]
            if frame[26:34] == host_ip + guest_ip and icmp[:2] == b"\x08\x00":
                assert frame[:6] == guest_mac
                echo_requests[icmp[4:]] += 1
            if frame[26:34] == guest_ip + host_ip and icmp[:2] == b"\x00\x00":
                assert frame[6:12] == guest_mac and 28 <= size_ip <= len(frame) - 14
                assert checksum_ok(frame[14:34]) and checksum_ok(icmp)
                echo_replies[icmp[4:]] += 1
    if transport_test and len(frame) >= 42 and frame[12:14] == b"\x08\x00" and frame[14] == 0x45:
        addresses = frame[26:34]
        if addresses in (host_ip + guest_ip, guest_ip + host_ip):
            guest = addresses[:4] == guest_ip
            proto = frame[23]
            size_ip = struct.unpack_from("!H", frame, 16)[0]
            segment = frame[34:14 + size_ip]
            if proto in (6, 17) and len(segment) >= 8:
                sport, dport = struct.unpack_from("!HH", segment)
                service, client = (sport, dport) if guest else (dport, sport)
                if (proto, service) in ((17, 7), (6, 8080)):
                    assert size_ip == 20 + len(segment) and checksum_ok(frame[14:34])
                    assert (frame[6:12] if guest else frame[:6]) == guest_mac
                    pseudo = addresses + struct.pack("!BBH", 0, proto, len(segment))
                    assert checksum_ok(pseudo + segment), (proto, guest)
                    if proto == 17:
                        assert struct.unpack_from("!H", segment, 4)[0] == len(segment)
                        (udp_replies if guest else udp_requests).append((client, segment[8:]))
                    else:
                        assert len(segment) >= 20
                        tcp_len = (segment[12] >> 4) * 4
                        assert 20 <= tcp_len <= len(segment)
                        seq, ack = struct.unpack_from("!II", segment, 4)
                        tcp_streams[client].append((guest, seq, ack, segment[13] & 0x3F, segment[tcp_len:]))
    if len(frame) < 42 or frame[12:14] != b"\x08\x00" or frame[23] != 17:
        continue
    ip = frame[14:]
    ihl = (ip[0] & 15) * 4
    if len(ip) < ihl + 8:
        continue
    udp = ip[ihl:]
    if struct.unpack_from("!HH", udp) != (7777, 7777):
        continue
    assert frame[:6] == b"\xff" * 6
    assert ip[12:20] == b"\x00" * 4 + b"\xff" * 4
    assert sum(struct.unpack("!" + "H" * (ihl // 2), ip[:ihl])) % 65535 == 0
    length = struct.unpack_from("!H", udp, 4)[0]
    assert 8 <= length <= len(udp)
    packets.append(udp[8:length].decode("ascii"))
serial = pathlib.Path(sys.argv[2]).read_bytes().replace(b"\r", b"").decode("ascii", "replace")
assert "MOO-KERNEL START\n" in serial
lines = [line for line in serial[serial.index("MOO-KERNEL START\n"):].splitlines() if line]
missing = collections.Counter(lines) - collections.Counter(packets)
assert not missing, "Kernzeilen fehlen im UDP: " + repr(missing)
assert "MOO-KERNEL OK" in packets
assert serial.index("MOO-KERNEL NETZ PORTS ") < serial.index("MOO-KERNEL TABELLEN FREI ")
assert "MOO-KERNEL NETZ DMA-STOP FEHLER" not in serial
print("      OK: %d COM1-Kernzeilen im UDP-pcap (%d Datagramme), Port 7777/IPv4-Pruefsumme" % (len(lines), len(packets)))
if echo_test:
    assert arp_requests and arp_replies, (arp_requests, arp_replies)
    assert sorted(map(len, echo_requests.elements())) == [61] * 20 + [1476]
    assert echo_requests == echo_replies, "ICMP Identifier/Sequenz/Nutzlast bzw. Anzahl weicht ab"
    assert "MOO-KERNEL NETZ IPV4 BEREIT 00000000C0000202" in serial
    print("      OK: Host-ping über %s, ARP + 21 Echo-Paare samt Prüfsummen und unveränderter Nutzlast" % sys.argv[3])
if transport_test:
    assert "MOO-KERNEL NETZ UDP TCP BEREIT 0000000000000001" in serial
    anfrage = pathlib.Path(sys.argv[1]).with_name("udp-anfrage.bin").read_bytes()
    assert len(udp_requests) == 1 and udp_requests == udp_replies
    assert udp_requests[0][1] == anfrage
    assert len(tcp_streams) == 2
    mehrfach = 0
    def fort(seq, n):
        return (seq + n) & 0xffffffff
    for stream in tcp_streams.values():
        client = [row[1:] for row in stream if not row[0]]
        server = [row[1:] for row in stream if row[0]]
        assert client[0][2] == 0x02 and server[0][2] == 0x12
        c_start, s_start = fort(client[0][0], 1), fort(server[0][0], 1)
        assert server[0][1] == c_start
        assert any(seq == c_start and ack == s_start and flags & 0x10 for seq, ack, flags, _ in client[1:])
        request = [row for row in client if row[3]]
        mehrfach += len(request) > 1
        assert b"".join(row[3] for row in request).startswith(b"GET / HTTP/1.1\r\n")
        assert b"".join(row[3] for row in request).endswith(b"\r\n\r\n")
        for packets, start in ((request, c_start), ([row for row in server if row[3]], s_start)):
            for seq, _, _, data in packets:
                assert seq == start
                start = fort(start, len(data))
        reply = b"".join(row[3] for row in server)
        assert reply.startswith(b"HTTP/1.1 200 OK\r\n") and reply.endswith(b"\r\n\r\nMoo TCP\n")
        for packets, gegen in ((client, server), (server, client)):
            seq, _, _, data = next(row for row in packets if row[2] & 1)
            end = fort(seq, len(data) + 1)
            assert any(ack == end and flags & 0x10 for _, ack, flags, _ in gegen)
    assert mehrfach >= 1, "HTTP-Anfrage nicht auf mehrere TCP-Segmente verteilt"
    print("      OK: UDP 7 bytegleich; TCP 8080 zwei Handshakes, HTTP 200 und beidseitig bestaetigtes FIN")
PY
}

if [ "$PRUEFUNG" = "kern" ]; then
  # P7-T5: UEFI-Loader in Moo (beispiele/uefi/moo_lader.moos) laedt den Moo-Kern
  # (beispiele/kernel/uefi_kern.moos) von der ESP, ruft ExitBootServices und
  # springt hinein. Der Loader wird aus GENAU EINEM Objekt (dem Moo-Objekt)
  # gelinkt, ohne uefi_entry.c/uefi_rt.c. Pruefung nach ExitBootServices:
  # Kern-Marker auf COM1, Bild per screendump, Negativfall ohne Kern-Datei.
  LADER_SRC="${MOO_UEFI_APP:-$ROOT/beispiele/uefi/moo_lader.moos}"
  KERN_SRC="${MOO_UEFI_KERN:-$ROOT/beispiele/kernel/uefi_kern.moos}"
  KERN_LD="$ROOT/beispiele/kernel/linker-uefi.ld"
  for t in lld-link ld readelf nm qemu-system-x86_64 parted mformat mmd mcopy; do
    command -v "$t" >/dev/null 2>&1 || skip "fehlendes Tooling: $t" \
      "sudo pacman -S lld binutils qemu-system-x86 parted mtools" "Tooling fehlt — Kern-Pruefung uebersprungen."
  done
  phase K1 "Loader: moo -> COFF -> PE32+ (nur das Moo-Objekt, kein C)"
  "$BIN" compile "$LADER_SRC" --no-stdlib --target x86_64-unknown-uefi -o "$OUT/lader.obj" >"$OUT/lader.err" 2>&1 \
    || { sed 's/^/      | /' "$OUT/lader.err"; fail "Loader nicht kompiliert"; }
  lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /opt:ref /Brepro \
      /out:"$OUT/BOOTX64.EFI" "$OUT/lader.obj" >"$OUT/link.err" 2>&1 \
    || { sed 's/^/      | /' "$OUT/link.err"; fail "Loader nicht gelinkt (fehlende Symbole = versteckte Runtime?)"; }
  file "$OUT/BOOTX64.EFI" | grep -qiE "EFI.*application|PE32\\+" || fail "Loader ist kein PE32+ EFI-Binary"
  echo "      OK: $(file -b "$OUT/BOOTX64.EFI"), $(stat -c %s "$OUT/BOOTX64.EFI") Bytes aus 1 Objekt"
  phase K2 "Kern: moo -> ELF64 (--kernel, linker-uefi.ld, ohne Multiboot)"
  "$BIN" compile "$KERN_SRC" --no-stdlib --target x86_64-bare --kernel --linker-script "$KERN_LD" \
      -o "$OUT/KERN.ELF" >"$OUT/kern.err" 2>&1 || { sed 's/^/      | /' "$OUT/kern.err"; fail "Kern nicht gebaut"; }
  "$BIN" compile "$KERN_SRC" --no-stdlib --target x86_64-bare --emit-ir -o "$OUT/KERN.ll" >"$OUT/kern-ir.err" 2>&1 \
    || { sed 's/^/      | /' "$OUT/kern-ir.err"; fail "Kern-IR nicht gebaut"; }
  uv run --no-project python - "$OUT/KERN.ll" <<'PY'
import pathlib, re, sys
ir = pathlib.Path(sys.argv[1]).read_text()
attrs = dict(re.findall(r"^attributes #(\d+) = \{(.*)\}", ir, re.M))
functions = re.findall(r"^define [^\n]+", ir, re.M)
assert functions
for function in functions:
    group = re.search(r"#(\d+)\s*\{", function)
    assert group and "noredzone" in attrs[group[1]], function
print("      OK: %d Kernel-Funktionsdefinitionen ohne Red-Zone" % len(functions))
PY
  readelf -h "$OUT/KERN.ELF" | grep -q 'Type:.*EXEC' || fail "Kern ist kein ELF EXEC"
  readelf -h "$OUT/KERN.ELF" | grep -q 'Machine:.*X86-64' || fail "Kern ist kein x86-64"
  [ -z "$(nm -u "$OUT/KERN.ELF")" ] || fail "Kern hat undefinierte Symbole"
  ENTRY_ELF="$(readelf -h "$OUT/KERN.ELF" | awk '/Entry point/{print $NF}')"
  ENTRY_SYM="$(nm "$OUT/KERN.ELF" | awk '$3=="moo_kern_start"{print $1}')"
  [ "$((ENTRY_ELF))" -eq "$((16#$ENTRY_SYM))" ] || fail "ELF-Einstieg $ENTRY_ELF ist nicht moo_kern_start ($ENTRY_SYM)"
  od -A n -t x1 "$OUT/KERN.ELF" | tr -d '\n' | grep -q 'd6 50 52 e8' && fail "Multiboot2-Kopf im Kern (GRUB-Pfad nicht verworfen)"
  echo "      OK: ELF64 EXEC x86-64, Einstieg moo_kern_start $ENTRY_ELF, kein Multiboot2-Kopf"
  # P7-T26: Nur eigene reguläre Testimages; N100-Zielwerte nur in privater ELF-Kopie.
  MSC="${MOO_UEFI_USB_STORAGE:-0}"
  MSC_SERIE=FE1234567890ABCD
  case "$MSC" in 0|1|gross|falsche-serie) ;; *) fail "USB_STORAGE: 0, 1, gross oder falsche-serie";; esac
  speicher_fingerabdruck() {
    uv run --no-project python - "${1:-$OUT/usb-gpt.img}" <<'PY'
import hashlib, json, os, stat, sys
fd = os.open(sys.argv[1], os.O_RDONLY)
st = os.fstat(fd)
assert stat.S_ISREG(st.st_mode), "Nur reguläres Testimage"
h, pos, extents = hashlib.sha256(), 0, []
h.update(str(st.st_size).encode())
while pos < st.st_size:
    try:
        start = os.lseek(fd, pos, os.SEEK_DATA)
    except OSError as ex:
        import errno
        if ex.errno == errno.ENXIO:
            break
        raise
    end = os.lseek(fd, start, os.SEEK_HOLE)
    extents.append([start, end])
    h.update(("%d:%d:" % (start, end)).encode())
    os.lseek(fd, start, os.SEEK_SET)
    left = end - start
    while left:
        data = os.read(fd, min(left, 1048576))
        assert data
        h.update(data)
        left -= len(data)
    pos = end
os.close(fd)
print(json.dumps(dict(size=st.st_size, extents=extents, sha256=h.hexdigest()), sort_keys=True))
PY
  }
  # Gemeinsamer vorhandener GPT-Testaufbau fuer USB und NVMe.
  speicher_testimage() {
    command -v sfdisk >/dev/null || fail "Speicher-Gate braucht sfdisk"
    [ ! -e "$1" ] || [ -f "$1" ] || fail "Testimage ist keine regulaere Datei"
    rm -f -- "$1"
    truncate -s "$2" -- "$1" || fail "Speicher-Testimage"
    sfdisk --sector-size "$3" "$1" >"$4" 2>&1 <<'GPT' || fail "GPT-Testimage"
label: gpt
label-id: AABBCCDD-1122-3344-5566-778899AABBCC

start=2048, size=2048, type=linux, uuid=12345678-9ABC-DEF0-1234-56789ABCDEF0, name=MOO-READONLY
GPT
    sfdisk --sector-size "$3" --verify "$1" >>"$4" 2>&1 || fail "GPT-Layoutpruefung"
  }
  if [ "$MSC" != 0 ]; then
    [ "${MOO_UEFI_USB:-1}" = 1 ] && [ "${MOO_UEFI_NETZ:-1}" = 1 ] || fail "MSC-Gate braucht USB=1 und NETZ=1"
    MSC_SIZE=64M; [ "$MSC" != gross ] || MSC_SIZE=3T
    speicher_testimage "$OUT/usb-gpt.img" "$MSC_SIZE" 512 "$OUT/usb-gpt-layout.log"
    speicher_fingerabdruck >"$OUT/usb-gpt-vorher.json" || fail "MSC-Fingerabdruck"
    uv run --no-project python - "$OUT/KERN.ELF" <<'PY'
import pathlib, struct, sys
path = pathlib.Path(sys.argv[1])
elf = bytearray(path.read_bytes())
contract = struct.pack("<QQQ", 0x44524253554F4F4D, 0x68, 0x1000090C)
assert elf.count(contract) == 1, "Eindeutiger USB-Lesezielvertrag fehlt"
pos = elf.index(contract)
struct.pack_into("<Q", elf, pos + 16, 0x000146F4)
assert elf.count(b"AA00000000000489") == 1, "Eindeutige Zielserie fehlt"
elf = elf.replace(b"AA00000000000489", b"FE1234567890ABCD")
path.write_bytes(elf)
print("      OK: private QEMU-Kernkopie, BDF68/46F4:0001/FE1234567890ABCD")
PY
    [ "$?" -eq 0 ] || fail "MSC-Zielvertrag in privater ELF-Kopie"
    [ "$MSC" != falsche-serie ] || MSC_SERIE=FE1234567890ABCE
  fi
  SAMMEL="${MOO_UEFI_SAMMEL:-0}"
  case "$SAMMEL" in 0|1) ;; *) fail "SAMMEL: 0 oder 1";; esac
  SAMMEL_IGB="${MOO_UEFI_SAMMEL_IGB:-0}"
  case "$SAMMEL_IGB" in 0|1) ;; *) fail "SAMMEL_IGB: 0 oder 1";; esac
  [ "$SAMMEL_IGB" != 1 ] || [ "$SAMMEL" = 1 ] || fail "SAMMEL_IGB braucht SAMMEL=1"
  # P7-T27: NVMe nur auf eigenem Image; Produktziel bleibt deaktiviert.
  NVME="${MOO_UEFI_NVME:-0}"
  case "$NVME" in 0|1|gross|4k|inventar|falsche-serie|falscher-namespace) ;; *) fail "NVME: 0, 1, gross, 4k, inventar, falsche-serie oder falscher-namespace";; esac
  NVME_SERIE=MOONVME-READONLY-001
  NVME_BLOCK=512
  if [ "$SAMMEL" = 1 ]; then
    [ "$MSC" = 1 ] && { [ "$NVME" = 1 ] || [ "$NVME" = inventar ]; } || fail "SAMMEL braucht USB_STORAGE=1 und NVME=1 oder inventar"
  elif [ "$NVME" = inventar ]; then
    fail "NVME=inventar braucht SAMMEL=1"
  fi
  if [ "$NVME" != 0 ]; then
    [ "${MOO_UEFI_NETZ:-1}" = 1 ] || fail "NVMe-Gate braucht NETZ=1"
    NVME_SIZE=64M; [ "$NVME" != gross ] || NVME_SIZE=3T
    [ "$NVME" != 4k ] || NVME_BLOCK=4096
    speicher_testimage "$OUT/nvme-gpt.img" "$NVME_SIZE" "$NVME_BLOCK" "$OUT/nvme-gpt-layout.log"
    speicher_fingerabdruck "$OUT/nvme-gpt.img" >"$OUT/nvme-gpt-vorher.json" || fail "NVMe-Fingerabdruck"
    if [ "$NVME" != inventar ]; then
      uv run --no-project python - "$OUT/KERN.ELF" "$NVME" <<'PY'
import pathlib, struct, sys
path = pathlib.Path(sys.argv[1])
elf = bytearray(path.read_bytes())
contract = struct.pack("<8Q", 0x4E564D45524F3031, 65536, 1, 0, 0, 0, 0, 0)
assert elf.count(contract) == 1, "Eindeutiger deaktivierter NVMe-Zielvertrag fehlt"
pos = elf.index(contract)
nsid = 0xfffffffe if sys.argv[2] == "falscher-namespace" else 1
struct.pack_into("<3Q", elf, pos + 8, 0x70, nsid, 20)
elf[pos + 32:pos + 52] = b"MOONVME-READONLY-001"
path.write_bytes(elf)
print("      OK: private QEMU-NVMe-Kernkopie, BDF70/NSID%d/exakteSN20" % nsid)
PY
      [ "$?" -eq 0 ] || fail "NVMe-Zielvertrag in privater ELF-Kopie"
    fi
    [ "$NVME" != falsche-serie ] || NVME_SERIE=MOONVME-READONLY-002
    printf '%s\n' pci_nvme_mmio_start_success pci_nvme_admin_cmd pci_nvme_io_cmd \
      pci_nvme_enqueue_req_completion pci_nvme_read >"$OUT/nvme-trace-events"
    if [ "$SAMMEL_IGB" = 1 ]; then
      printf '%s\n' igb_core_mdic_read igb_core_mdic_read_unhandled \
        igb_core_mdic_write igb_core_mdic_write_unhandled >>"$OUT/nvme-trace-events"
    fi
  fi
  if [ "$SAMMEL" = 1 ]; then
    uv run --no-project python - "$OUT/KERN.ELF" "$SAMMEL_IGB" "$KRYPTO" <<'PY'
import pathlib, struct, sys
path = pathlib.Path(sys.argv[1])
elf = bytearray(path.read_bytes())
contract = struct.pack("<3Q", 0x4C4D4D41534F4F4D, 0, 0)
assert elf.count(contract) == 1, "Eindeutiger Sammelvertrag fehlt"
# Standard-ID des QEMU-NVMe-Controllers (hw/nvme/ctrl.c, nvme_realize).
struct.pack_into("<2Q", elf, elf.index(contract) + 8, 1, 0x00101B36)
if sys.argv[2] == "1":
    nic = struct.pack("<2Q", 0x4F52363232494F4D, 0x125C8086)
    assert elf.count(nic) == 1, "I226-Zielvertrag fehlt"
    struct.pack_into("<Q", elf, elf.index(nic) + 8, 0x10C98086)
if sys.argv[3] != "0":
    crypto = struct.pack("<2Q", 0x315059524B4F4F4D, 0)
    assert elf.count(crypto) == 1, "Eindeutiger deaktivierter Krypto-Vertrag fehlt"
    struct.pack_into("<Q", elf, elf.index(crypto) + 8, 1)
path.write_bytes(elf)
PY
    [ "$?" -eq 0 ] || fail "Sammelvertrag in privater ELF-Kopie"
  fi
  if [ "$NETZ_PING" != 0 ]; then
    uv run --no-project python - "$OUT/KERN.ELF" "$NETZ_PING" "$NETZ_TRANSPORT" "$NETZ_TCP_ECHO" <<'PY'
import pathlib, struct, sys
path = pathlib.Path(sys.argv[1])
elf = bytearray(path.read_bytes())
vertrag = struct.pack("<3Q", 0x00345650494F4F4D, 0, 0)
assert elf.count(vertrag) == 1, "Eindeutiger deaktivierter IPv4-Vertrag fehlt"
struct.pack_into("<2Q", elf, elf.index(vertrag) + 8, 0xc0000202, int(sys.argv[2] == "igb"))
if sys.argv[3] == "1":
    vertrag = struct.pack("<2Q", 0x474643344C4F4F4D, 0)
    assert elf.count(vertrag) == 1, "Eindeutiger deaktivierter UDP-/TCP-Vertrag fehlt"
    struct.pack_into("<Q", elf, elf.index(vertrag) + 8, 1)
if sys.argv[4] == "1":
    vertrag = struct.pack("<2Q", 0x004F4843454F4F4D, 0)
    assert elf.count(vertrag) == 1, "Eindeutiger deaktivierter TCP-Echo-Vertrag fehlt"
    struct.pack_into("<Q", elf, elf.index(vertrag) + 8, 1)
path.write_bytes(elf)
PY
    [ "$?" -eq 0 ] || fail "IPv4-Vertrag in privater ELF-Kopie"
  fi
  phase K3 "OVMF-Boot: Moo-Loader -> ExitBootServices -> Moo-Kern"
  KOVMF_CODE=""; KOVMF_VARS=""
  for c in /usr/share/edk2/x64/OVMF_CODE.4m.fd /usr/share/edk2-ovmf/x64/OVMF_CODE.4m.fd \
           /usr/share/OVMF/OVMF_CODE.4m.fd /usr/share/edk2/x64/OVMF_CODE.fd; do
    [ -f "$c" ] && { KOVMF_CODE="$c"; KOVMF_VARS="${c/CODE/VARS}"; break; }
  done
  [ -n "$KOVMF_CODE" ] && [ -f "$KOVMF_VARS" ] || skip "OVMF fehlt" "sudo pacman -S edk2-ovmf" "Build K1/K2 gruen — OVMF-Boot GESKIPPT."
  kern_image() {  # Image, mit Kern (1/0)
    rm -f "$1"; truncate -s 48M "$1" || fail "Image nicht anlegbar"
    parted -s "$1" mklabel gpt mkpart ESP fat32 1MiB 100% set 1 esp on >/dev/null 2>&1 || fail "parted"
    mformat -i "$1"@@$((2048*512)) -F :: >/dev/null 2>&1 || fail "mformat"
    mmd -i "$1"@@$((2048*512)) ::/EFI ::/EFI/BOOT ::/EFI/MOO >/dev/null 2>&1
    mcopy -i "$1"@@$((2048*512)) "$OUT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI >/dev/null 2>&1 || fail "mcopy Loader"
    if [ "$2" = 1 ]; then
      mcopy -i "$1"@@$((2048*512)) "$OUT/KERN.ELF" ::/EFI/MOO/KERN.ELF >/dev/null 2>&1 || fail "mcopy Kern"
    fi
  }
  kern_image "$OUT/uefi.img" 1
  kern_image "$OUT/ohne-kern.img" 0
  cp "$KOVMF_VARS" "$OUT/vars.fd"; cp "$KOVMF_VARS" "$OUT/vars-ohne.fd"
  : >"$OUT/serial.log"; : >"$OUT/debug.log"; rm -f "$OUT/monitor.in" "$OUT/screenshot.png"
  mkfifo "$OUT/monitor.in" || fail "FIFO fuer QEMU-Monitor nicht anlegbar"
  exec 3<>"$OUT/monitor.in"
  KERN_QEMU=()
  [ "${MOO_UEFI_DIAG:-0}" != 1 ] || KERN_QEMU+=(-d int,guest_errors,cpu_reset -D "$OUT/cpu.log")
  NETZ="${MOO_UEFI_NETZ:-1}"
  if [ "$NETZ_PING" != 0 ]; then
    ip link set lo up && ip tuntap add dev moo-ping mode tap &&
      ip address add 192.0.2.1/24 dev moo-ping && ip link set moo-ping up || fail "TAP im Test-Namespace nicht bereit"
    KERN_QEMU+=(-netdev tap,id=n0,ifname=moo-ping,script=no,downscript=no,vnet_hdr=off
      -device "$NETZ_PING,netdev=n0,mac=52:54:00:12:34:59,romfile="
      -object filter-dump,id=klog,netdev=n0,file="$OUT/net.pcap")
  elif [ "$NETZ" = 1 ]; then
    KERN_QEMU+=(-netdev user,id=n0 -device e1000,netdev=n0,romfile=
      -object filter-dump,id=klog,netdev=n0,file="$OUT/net.pcap")
  else
    KERN_QEMU+=(-net none)
  fi
  USB="${MOO_UEFI_USB:-1}"
  USB_VERSION="${MOO_UEFI_USB_VERSION:-2}"
  case "$USB_VERSION" in 1|2) ;; *) fail "USB_VERSION: nur 1 (Full-Speed) oder 2 (High-Speed)";; esac
  if [ "$USB" = 1 ]; then
    XHCI_ADDR=""; [ "$MSC" = 0 ] || XHCI_ADDR=",addr=0xd"
    KERN_QEMU+=(-device "qemu-xhci,id=xhci$XHCI_ADDR" -device "usb-kbd,bus=xhci.0,usb_version=$USB_VERSION")
  elif [ "$USB" = hub ]; then
    KERN_QEMU+=(-device qemu-xhci,id=xhci -device usb-hub,bus=xhci.0,port=1
      -device "usb-kbd,bus=xhci.0,port=1.1,usb_version=$USB_VERSION")
  fi
  if [ "$MSC" != 0 ]; then
    KERN_QEMU+=(-drive "if=none,id=msc,format=raw,readonly=on,file=$OUT/usb-gpt.img"
      -device "usb-storage,bus=xhci.0,drive=msc,serial=$MSC_SERIE,pcap=$OUT/usb.pcap")
  fi
  if [ "$NVME" != 0 ]; then
    KERN_QEMU+=(-drive "if=none,id=nvmedisk,format=raw,readonly=on,file=$OUT/nvme-gpt.img"
      -device "nvme,id=nvme0,serial=$NVME_SERIE,addr=0xe"
      -device "nvme-ns,bus=nvme0,drive=nvmedisk,nsid=1,logical_block_size=$NVME_BLOCK,physical_block_size=$NVME_BLOCK"
      -trace "events=$OUT/nvme-trace-events,file=$OUT/nvme.trace")
  fi
  if [ "$SAMMEL_IGB" = 1 ]; then
    KERN_QEMU+=(
      -netdev user,id=igb0,restrict=on -device igb,id=igb_a,addr=0x5,netdev=igb0,mac=52:54:00:22:57:01,romfile=
      -netdev user,id=igb1,restrict=on -device igb,id=igb_b,addr=0xa,netdev=igb1,mac=52:54:00:22:57:02,romfile=)
  fi
  timeout --foreground $((WAIT_S + 60))s qemu-system-x86_64 "${QEMU_ACCEL[@]}" "${KERN_QEMU[@]}" \
      -drive if=pflash,format=raw,unit=0,readonly=on,file="$KOVMF_CODE" \
      -drive if=pflash,format=raw,unit=1,file="$OUT/vars.fd" \
      -drive format=raw,file="$OUT/uefi.img" \
      -m 256M -vga std -display none -no-reboot -monitor stdio \
      -serial file:"$OUT/serial.log" -debugcon file:"$OUT/debug.log" -global isa-debugcon.iobase=0xe9 \
      <"$OUT/monitor.in" >"$OUT/monitor.log" 2>&1 &
  QPID=$!
  KEY_SENT=0
  IGB_LINK=0
  for ((i = 0; i < WAIT_S; i++)); do
    # QEMU igb laesst STATUS.LU nach CTRL.RST geloescht (igb_core.c:
    # 4389,4463–4490). TAP-Link nach dem Reset wie am Kabel neu melden.
    if [ "$NETZ_PING" = igb ] && [ "$IGB_LINK" -eq 0 ] &&
       grep -aqF "MOO-KERNEL NETZ IGB INITIALISIERT " "$OUT/serial.log"; then
      printf 'set_link n0 off\nset_link n0 on\n' >&3
      IGB_LINK=1
    fi
    BEREIT="MOO-KERNEL TASTATUR BEREIT 0000000000000001"
    [ "$USB" != 1 ] || BEREIT="MOO-KERNEL USB BEREIT POLLING 0000000000000001"
    if [ "$KEY_SENT" -eq 0 ] && grep -aqF "$BEREIT" "$OUT/serial.log" 2>/dev/null; then
      printf 'sendkey a\n' >&3
      KEY_SENT=1
    fi
    grep -aq "MOO-KERNEL OK" "$OUT/serial.log" 2>/dev/null && break
    grep -aq "MOO-LADER FEHLER" "$OUT/serial.log" 2>/dev/null && break
    kill -0 "$QPID" 2>/dev/null || break
    sleep 1
  done
  PING_OK=0
  if [ "$NETZ_PING" != 0 ] && grep -aqF "MOO-KERNEL OK" "$OUT/serial.log"; then
    {
      LC_ALL=C ping -4 -n -I moo-ping -c 20 -i 0.25 -W 1 -w 10 -s 57 192.0.2.2 &&
      LC_ALL=C ping -4 -n -I moo-ping -c 1 -W 1 -w 3 -M do -s 1472 192.0.2.2
    } >"$OUT/ping.log" 2>&1 && PING_OK=1
  fi
  TRANSPORT_OK=0
  if [ "$NETZ_TRANSPORT" = 1 ] && [ "$PING_OK" = 1 ] &&
     bash "$ROOT/skripte/p7/transport-probe.sh" 192.0.2.2 moo-ping 192.0.2.1 "$OUT" "$NETZ_TCP_ECHO" >"$OUT/transport.log" 2>&1; then
    # Eine zweite Verbindung nach FIN, CRLF-CRLF ueber zwei Segmente verteilt.
    uv run --no-project --offline python - >"$OUT/tcp-segmente.log" 2>&1 <<'PY'
import socket, time
with socket.create_connection(("192.0.2.2", 8080), timeout=4, source_address=("192.0.2.1", 0)) as sock:
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.sendall(b"GET / HTTP/1.1\r\nHost: 192.0.2.2\r\n\r")
    time.sleep(0.15)
    sock.sendall(b"\n")
    data = bytearray()
    while part := sock.recv(256):
        data.extend(part)
        assert len(data) <= 256
    assert data.startswith(b"HTTP/1.1 200 OK\r\n") and data.endswith(b"\r\n\r\nMoo TCP\n")
    print("HTTP-200-Antwort und EOF nach segmentierter Anfrage")
PY
    [ "$?" -ne 0 ] || TRANSPORT_OK=1
  fi
  TCP_FENSTER_OK=0
  if [ "$NETZ_TCP_ECHO" = 1 ] && [ "$TRANSPORT_OK" = 1 ]; then
    uv run --no-project --offline python "$ROOT/skripte/p7/tcp-fenster-qemu.py" >"$OUT/tcp-fenster.log" 2>&1 && TCP_FENSTER_OK=1
  fi
  # USB bleibt nach OK aktiv: Shift und mehr als 256 Press/Release-Reports
  # pruefen Event-Cycle sowie Link-TRB/Cycle des Transfer-Rings.
  if [ "$USB" = 1 ] && grep -aq "MOO-KERNEL OK" "$OUT/serial.log"; then
    printf 'screendump %s -f png\n' "$OUT/boot-screenshot.png" >&3
    for ((j = 0; j < 15; j++)); do [ -s "$OUT/boot-screenshot.png" ] && break; sleep 0.1; done
    printf 'sendkey shift-b 60\n' >&3
    sleep 0.2
    for ((j = 0; j < 140; j++)); do
      printf 'sendkey c 60\n' >&3
      # Genug Abstand fuer Release-Report und sichtbares Present unter VM-Last.
    sleep 0.25
    done
  fi
  sleep 1
  printf 'info registers\ninfo lapic\n' >&3
  printf 'screendump %s -f png\n' "$OUT/screenshot.png" >&3
  for ((i = 0; i < 15; i++)); do [ -s "$OUT/screenshot.png" ] && break; sleep 1; done
  printf 'quit\n' >&3 2>/dev/null || true; exec 3>&-; wait "$QPID" 2>/dev/null
  tr -d '\000\r' <"$OUT/serial.log" | grep -aE "MOO-(LADER|KERNEL)" | sed 's/^/      | /'
  grep -aq "MOO-LADER FEHLER" "$OUT/serial.log" && fail "Loader meldet FEHLER (siehe oben)"
  for m in "MOO-LADER START (MOO, OHNE C)" "MOO-LADER GOP " "MOO-LADER FB-STRIDE " "MOO-LADER RSDP " \
           "MOO-LADER KERN-DATEI " "MOO-LADER SEGMENT " "MOO-LADER KERN-EINSTIEG " \
           "MOO-LADER KERN-PHYSISCH " "MOO-LADER SEITENTABELLE " \
           "MOO-LADER EXITBOOTSERVICES OK " "MOO-LADER KARTE " "MOO-LADER SPRUNG " \
           "MOO-KERNEL START" "MOO-KERNEL RIP FFFFFFFF8" "MOO-KERNEL RSP FFFFFFFF8" \
           "MOO-KERNEL GDT CS 0000000000000008" "MOO-KERNEL PAGING EIGEN CR3 " \
           "MOO-KERNEL PFA FREI-SEITEN " "MOO-KERNEL HEAP OK 0000000000000001" \
           "MOO-KERNEL TABELLEN FREI " "MOO-KERNEL IDT OK 0000000000000001" \
           "MOO-KERNEL RSDP-SIG 0000000000000001" "MOO-KERNEL ACPI FADT-MADT 0000000000000003" \
           "MOO-KERNEL PRESENT-FORMATE 0000000000000005" "MOO-KERNEL SURFACE 0000000000000001" \
           "MOO-KONSOLE: SURFACE-PRESENT AUF GOP" "MOO-KONSOLE: GDT, PAGING, STACK IN MOO" \
           "MOO-KERNEL AUSNAHME VEKTOR 0000000000000003" \
           "MOO-KERNEL AUSNAHME FEHLERCODE 0000000000000000" "MOO-KERNEL AUSNAHME RIP " \
           "MOO-KERNEL AUSNAHME TEST 0000000000000001" "MOO-KERNEL TIMER QUELLE 0000000000000001" \
           "MOO-KERNEL TIMER STARTWERT " "MOO-KERNEL TIMER TICKS " \
           "MOO-KERNEL TASTATUR BEREIT 0000000000000001" \
           "MOO-KERNEL USB REPORT-TEST 0000000000000001" \
           "MOO-KONSOLE: SPEICHER, IDT, ACPI" \
           "MOO-KERNEL MESS PRESENT-TAKTE " "MOO-KERNEL OK"; do
    grep -aqF -- "$m" "$OUT/serial.log" || fail "Marker fehlt: '$m'"
  done
  if [ "$USB" = 1 ]; then
    for m in "MOO-KERNEL USB BEREIT POLLING 0000000000000001" \
             "MOO-KERNEL USB HID 0000000000000004" \
             "MOO-KERNEL USB TASTE 0000000000000061" \
             "MOO-KERNEL USB TASTE 0000000000000042"; do
      grep -aqF "$m" "$OUT/serial.log" || fail "USB-Marker fehlt: $m"
    done
    USB_C="$(grep -acF 'MOO-KERNEL USB TASTE 0000000000000063' "$OUT/serial.log")"
    [ "$USB_C" -ge 135 ] || fail "USB-Ringumlauf: nur $USB_C von 140 Tastendruecken"
    USB_SPEED=3; [ "$USB_VERSION" != 1 ] || USB_SPEED=1
    grep -aqF "MOO-KERNEL USB SPEED 000000000000000$USB_SPEED" "$OUT/serial.log" || fail "Erwarteter USB-Speed $USB_SPEED fehlt"
    echo "      OK: USB-Version $USB_VERSION, Port-Speed $USB_SPEED, $USB_C/140 Tastendruecke"
  elif [ "$USB" = 0 ]; then
    grep -aqF "MOO-KERNEL TASTATUR SCAN 000000000000001E" "$OUT/serial.log" || fail "PS/2 Scan 0x1E fehlt"
  elif [ "$USB" = hub ]; then
    grep -aqF "MOO-KERNEL USB GERAETEKLASSE 0000000000000009" "$OUT/serial.log" || fail "Hub nicht als Klasse9 diagnostiziert"
    grep -aqF "MOO-KERNEL USB BEREIT POLLING 0000000000000000" "$OUT/serial.log" || fail "Hub faelschlich als Boot-Keyboard bereit"
    grep -aqF "MOO-KERNEL USB TASTE " "$OUT/serial.log" && fail "Hub-Negativfall meldet erfundene Eingabe"
    echo "      OK: USB-Tastatur hinter Hub als Diagnose-/Negativfall (Hubtreiber weiterhin offen)"
  fi
  if [ "$USB" != 0 ]; then
    for m in "HCSPARAMS1" "HCSPARAMS2" "HCCPARAMS1" "PROTOCOL CAP" "PROTOCOL PORTBEREICH" "PORTSC" "CCS" "PED" "PR" "PLS" "PP" "SPEED" "CSC GESEHEN" "EVENT TYP" "EVENT TRB" "EVENT CC" "EVENT REST" "EVENT COMMAND PARAM24" "OUTPUT SLOT STATE" "OUTPUT USB ADRESSE" "OUTPUT EP0 STATE" "OUTPUT EP0 MPS" "CONTROL SETUP FLAGS" "DESCRIPTOR8 ROH" "DESCRIPTOR MPS0"; do
      grep -aqF "MOO-KERNEL USB $m " "$OUT/serial.log" || fail "USB-Diagnose fehlt: $m"
    done
    for phase in 0 1 2 3 4 5; do
      grep -aqF "MOO-KERNEL USB PHASE 000000000000000$phase" "$OUT/serial.log" || fail "USB-Snapshot Phase$phase fehlt"
    done
  fi
  if [ "$MSC" != 0 ]; then
    speicher_fingerabdruck >"$OUT/usb-gpt-nachher.json" || fail "MSC-Fingerabdruck nach QEMU"
    cmp "$OUT/usb-gpt-vorher.json" "$OUT/usb-gpt-nachher.json" || fail "MSC-Testimage verändert"
    uv run --no-project python - "$OUT" "$MSC" <<'PY'
import pathlib, re, struct, sys
out, mode = pathlib.Path(sys.argv[1]), sys.argv[2]
serial = (out / "serial.log").read_text(errors="replace").replace("\r", "")
def pcap(path, linktype):
    raw = path.read_bytes()
    assert raw[:4] in (b"\xd4\xc3\xb2\xa1", b"\xa1\xb2\xc3\xd4")
    order = "<" if raw[0] == 0xd4 else ">"
    assert struct.unpack_from(order + "I", raw, 20)[0] == linktype
    pos = 24
    while pos < len(raw):
        assert pos + 16 <= len(raw)
        sec, usec, size, original = struct.unpack_from(order + "IIII", raw, pos)
        pos += 16
        assert size <= original and pos + size <= len(raw)
        yield sec * 1000000 + usec, raw[pos:pos+size], order
        pos += size
# QEMU net/dump.c nutzt virtuelle Zeit, USB-pcap Hostzeit: keine Uhren mischen.
# ms_seriennummer() beendet GET_DEVICE18 / STRING0 / iSerial mit wLength255.
# Der nachfolgende HID-Pfad fragt keine USB-Strings ab (xh_beschreiben/xh_geraet).
packets = list(pcap(out / "usb.pcap", 220))
completed, pending = [], None
serial_end = None
wire_series = "FE1234567890ABCE" if mode == "falsche-serie" else "FE1234567890ABCD"
for index, (_, packet, order) in enumerate(packets):
    assert len(packet) >= 64
    assert struct.unpack_from(order + "I", packet, 36)[0] == len(packet)
    if packet[9] != 2:
        continue
    if packet[8] == ord("S"):
        pending = struct.unpack_from("<BBHHH", packet, 40)
    elif packet[8] == ord("C") and pending is not None:
        completed.append((pending, packet[64:], struct.unpack_from(order + "i", packet, 28)[0]))
        request, data, status = completed[-1]
        if request[0:2] == (0x80, 6) and request[2] >> 8 == 3 and request[2] & 255 and request[4] == 255:
            expected = wire_series.encode("utf-16le")
            if status == 0 and data == bytes([2 + len(expected), 3]) + expected:
                assert len(completed) >= 3
                devreq, dev, devstatus = completed[-3]
                langreq, lang, langstatus = completed[-2]
                assert devreq == (0x80, 6, 0x0100, 0, 18) and devstatus == 0 and len(dev) == 18
                assert struct.unpack_from("<HH", dev, 8) == (0x46f4, 1)
                assert langreq == (0x80, 6, 0x0300, 0, 255) and langstatus == 0
                assert len(lang) >= 4 and lang[1] == 3
                assert request[2] == 0x0300 | dev[16]
                assert request[3] == struct.unpack_from("<H", lang, 2)[0]
                serial_end = index
        pending = None
assert serial_end is not None, "Abgeschlossene Serienpruefung des Kern-MSC-Pfads fehlt im USB-pcap"
# QEMU hw/usb/pcap.c: LINKTYPE220, 64-byte usbmon header; nur Submit/Bulk/OUT.
outgoing = [packet[64:] for _, packet, _ in packets[serial_end+1:]
            if packet[8] == ord("S") and packet[9] == 3 and not packet[10] & 128]
ops = [int(x, 16) for x in re.findall(r"USB MSC SCSI OPCODE ([0-9A-F]{16})", serial)]
tags = [int(x, 16) for x in re.findall(r"USB MSC CBW TAG ([0-9A-F]{16})", serial)]
assert len(ops) == len(tags) == len(outgoing), (ops, tags, len(outgoing))
for cbw, opcode, tag in zip(outgoing, ops, tags):
    assert len(cbw) == 31 and cbw[:4] == b"USBC", cbw.hex()
    assert struct.unpack_from("<I", cbw, 4)[0] == tag
    length = struct.unpack_from("<I", cbw, 8)[0]
    assert cbw[12] == (128 if length else 0) and cbw[13] == 0
    assert cbw[15] == opcode and opcode in {0, 3, 0x12, 0x25, 0x28, 0x88, 0x9e}
    assert cbw[14] == (6 if opcode in {0,3,0x12} else 10 if opcode in {0x25,0x28} else 16)
    if opcode == 0x9e:
        assert cbw[16] == 0x10
if mode == "falsche-serie":
    assert not ops and "USB MSC SERIE GEPRUEFT 0000000000000000" in serial
    assert "USB MSC BEREIT 0000000000000000" in serial
    assert "USB MSC GPT PART GUID " not in serial
else:
    for marker in ("SERIE GEPRUEFT 0000000000000001", "BEREIT 0000000000000001",
                   "BLOCKGROESSE 0000000000000200", "GPT HEADER CRC OK 0000000000000001",
                   "GPT ARRAY CRC OK 0000000000000001", "GPT BACKUP HEADER OK 0000000000000001",
                   "GPT PARTITIONEN 0000000000000001", "GPT ERSTE LBA 0000000000000800",
                   "GPT LETZTE LBA 0000000000000FFF",
                   "GPT PART GUID {12345678-9ABC-DEF0-1234-56789ABCDEF0} 0000000000000001"):
        assert "USB MSC " + marker in serial, marker
    assert {0,0x12,0x25,0x28} <= set(ops), ops
    if mode == "gross":
        assert {0x9e,0x88} <= set(ops), ops
    assert "USB MSC FEHLER " not in serial and "USB MSC RESET RECOVERY " not in serial
print("      OK: USB-MSC %s, %d CBWs nur lesend, keine Data-OUT-Phase, Image unverändert" % (mode, len(ops)))
PY
    [ "$?" -eq 0 ] || fail "USB-MSC BOT/GPT/Serien-/Nur-Lese-Gate"
  fi
  if [ "$SAMMEL" = 1 ]; then
    uv run --no-project python - "$OUT" "$NVME" "$SAMMEL_IGB" <<'PY'
import pathlib, re, struct, sys
out, mode = pathlib.Path(sys.argv[1]), sys.argv[2]
wire = (out / "net.pcap").read_bytes()
sections = ("START", "PCI START", "PCI ENDE", "I226 START", "I226 ENDE", "USB START", "USB ENDE",
            "USB-MSC START", "USB-MSC ENDE", "NVME START", "NVME ENDE",
            "I226 PHY START", "I226 PHY ENDE", "HID START", "HID ENDE", "ENDE")
pos = 0
for section in sections:
    marker = ("MOO-KERNEL SAMMEL " + section + " ").encode()
    found = wire.find(marker, pos)
    assert found >= 0, section
    pos = found + len(marker)
for label, value in (("PCI NVME-KANDIDATEN", 1), ("NVME GPT-PROFIL", int(mode != "inventar")),
                     ("USB ENDE", 1), ("USB-MSC ENDE", 1), ("NVME ENDE", 1), ("HID ENDE", 1)):
    assert ("MOO-KERNEL SAMMEL %s %016X" % (label, value)).encode() in wire, label
igb = sys.argv[3] == "1"
expected_ports = {0x28: (0x525400225701, 0), 0x50: (0x525400225702, 0)} if igb else {}
diag_start = wire.index(b"MOO-KERNEL SAMMEL I226 START ")
diag_end = wire.index(b"MOO-KERNEL SAMMEL I226 ENDE ", diag_start)
diag = wire[diag_start:diag_end]
ports = list(re.finditer(rb"MOO-KERNEL I226 PORT START ([0-9A-F]{16})", diag))
assert [int(port[1], 16) for port in ports] == list(expected_ports)
assert ("MOO-KERNEL I226 PORTS %016X" % len(ports)).encode() in diag
assert ("MOO-KERNEL SAMMEL I226 ENDE %016X" % len(ports)).encode() in wire
for port in ports:
    bdf = int(port[1], 16)
    end = diag.index(b"MOO-KERNEL I226 PORT ENDE " + port[1], port.end())
    values = {label.decode(): int(value, 16) for label, value in
              re.findall(rb"MOO-KERNEL I226 ([A-Z0-9 ]+?) ([0-9A-F]{16})", diag[port.end():end])}
    mac, link = expected_ports[bdf]
    assert values["PCI ID"] == 0x10C98086 and values["PCI COMMAND"] & 2, (bdf, values)
    assert values["BAR0"] != 0 and "MMIO FEHLER" not in values, (bdf, values)
    assert values["LINK"] == link == bool(values["STATUS"] & 2), values
    assert values["SPEED MBIT"] == (1000 if link else 0), values
    assert values["NVM AUTO READ"] == bool(values["EECD"] & 0x200), values
    raw = struct.pack("<IH", values["RAL0"], values["RAH0"] & 0xFFFF)
    assert values["RAR0 MAC GUELTIG"] == 1 and values["RAH0"] & 0x80000000, values
    assert values["RAR0 MAC"] == mac == int.from_bytes(raw, "big"), values

phy_start = wire.index(b"MOO-KERNEL SAMMEL I226 PHY START ")
phy_end = wire.index(b"MOO-KERNEL SAMMEL I226 PHY ENDE ", phy_start)
phy_log = wire[phy_start:phy_end]
phy_ports = list(re.finditer(rb"MOO-KERNEL I226 PHY PORT START ([0-9A-F]{16})", phy_log))
assert [int(port[1], 16) for port in phy_ports] == list(expected_ports)
assert ("MOO-KERNEL I226 PHY PORTS %016X" % len(phy_ports)).encode() in phy_log
assert ("MOO-KERNEL SAMMEL I226 PHY ENDE %016X" % len(phy_ports)).encode() in wire
mdic_answers = []
for port in phy_ports:
    end = phy_log.index(b"MOO-KERNEL I226 PHY PORT ENDE " + port[1], port.end())
    values = {}
    for label, value in re.findall(rb"MOO-KERNEL I226 ([A-Z0-9 ]+?) ([0-9A-F]{16})", phy_log[port.end():end]):
        values.setdefault(label.decode(), []).append(int(value, 16))
    assert "PHY FEHLER" not in values, values
    assert values["PHY ADRESSE"] == [1] and values["PHY REG"] == [2, 3], values
    assert values["MDIC READ CMD"] == [0x08220000, 0x08230000], values
    assert values["MDIC READY"] == [1, 1] and values["MDIC ERROR"] == [0, 0], values
    assert len(values["PHY SWFW NACH FREIGABE"]) == 2 and all(
        value & 2 == 0 for value in values["PHY SWFW NACH FREIGABE"]
    ), values
    id1, id2 = (values[f"PHY ID{word}"][0] for word in (1, 2))
    assert values["PHY ID ROH"] == [id1 << 16 | id2] and values["PHY ID EMPFANGEN"] == [1], values
    mdic_answers.extend(values["MDIC ANTWORT"])
if igb:
    trace = (out / "nvme.trace").read_text(errors="replace")
    transactions = re.findall(r"igb_core_mdic_read .*?MDIC READ: PHY\[(\d+)\] = 0x([0-9a-f]+)", trace)
    assert [int(reg) for reg, _ in transactions] == [2, 3, 2, 3], transactions
    assert [int(data, 16) | 0x10000000 for _, data in transactions] == mdic_answers
    assert not re.search(r"igb_core_mdic_(write|read_unhandled)", trace)
    print("      OK: igb/82576, zwei BAR0/MAC-Snapshots, genau vier PHY-Reads und SW/FW-Freigabe")
port_starts = re.findall(rb"MOO-KERNEL USB INVENTAR PORT START ([0-9A-F]{16})", wire)
port_ends = re.findall(rb"MOO-KERNEL USB INVENTAR PORT ENDE ([0-9A-F]{16})", wire)
assert len(port_starts) == len(port_ends) and len(set(port_starts)) == len(port_starts) >= 2
assert wire.count(b"MOO-KERNEL USB INVENTAR DEVICE BYTES 0000000000000012") == 2
assert wire.count(b"MOO-KERNEL USB INVENTAR KONFIG BYTES ") == 2
assert wire.count(b"MOO-KERNEL USB INVENTAR INTERFACE OFFSET ") == 2
assert wire.count(b"MOO-KERNEL USB INVENTAR HID OFFSET ") == 1
print("      OK: PCI, alle xHCI-Rootports, USB-MSC und NVMe im Netz-Log, geordnete Abschnittsmarker")
PY
    [ "$?" -eq 0 ] || fail "Sammellauf/Netz-Log"
  fi
  if [ "$NVME" != 0 ]; then
    speicher_fingerabdruck "$OUT/nvme-gpt.img" >"$OUT/nvme-gpt-nachher.json" || fail "NVMe-Fingerabdruck nach QEMU"
    cmp "$OUT/nvme-gpt-vorher.json" "$OUT/nvme-gpt-nachher.json" || fail "NVMe-Testimage veraendert"
    uv run --no-project python - "$OUT" "$NVME" "$NVME_BLOCK" <<'PY'
import json, pathlib, re, sys
out, mode, block = pathlib.Path(sys.argv[1]), sys.argv[2], int(sys.argv[3])
serial = (out / "serial.log").read_text(errors="replace")
trace = (out / "nvme.trace").read_text(errors="replace")
# Firmware darf eigene Initialisierung haben. Der letzte CC.EN-Start gehoert
# zum Kern; nur dieser Abschnitt muss exakt mit dessen Log uebereinstimmen.
start = trace.rfind("pci_nvme_mmio_start_success")
assert start >= 0, "NVMe-Controllerstart fehlt"
trace = trace[start:]
commands = []
for line in trace.splitlines():
    m = re.search(r"pci_nvme_(admin|io)_cmd cid (\d+)(?: nsid 0x[0-9a-f]+)? sqid (\d+) opc 0x([0-9a-f]+)", line)
    if m:
        kind, cid, queue, op = m.groups()
        cid, queue, op = int(cid), int(queue), int(op, 16)
        assert (kind == "admin" and queue == 0 and op in {0,1,2,4,5,6,10}) or (kind == "io" and queue == 1 and op == 2), line
        commands.append((queue, op, cid))
def values(name):
    return [int(x, 16) for x in re.findall(r"MOO-KERNEL NVME " + name + r" ([0-9A-F]{16})", serial)]
logged = list(zip(values("QUEUE"), values("OPCODE"), values("CID")))
assert commands and commands == logged, (commands, logged)
assert len(values("QUEUE")) == len(values("OPCODE")) == len(values("CID"))
completions = [tuple(int(x, 16) if i >= 2 else int(x) for i,x in enumerate(m))
               for m in re.findall(r"pci_nvme_enqueue_req_completion cid (\d+) cqid (\d+) dw0 0x([0-9a-f]+) dw1 0x([0-9a-f]+) status 0x([0-9a-f]+)", trace)]
words = list(zip(*(values("CQ-DW%d" % i) for i in range(4))))
assert len(completions) == len(commands) == len(words), (len(completions),len(commands),len(words))
queue_counts = [0,0]
for (queue,op,cid), (event_cid,event_queue,dw0,dw1,status), word in zip(commands,completions,words):
    assert (event_queue,event_cid) == (queue,cid)
    assert word[:2] == (dw0,dw1) and word[2] >> 16 == queue and word[2] & 0xffff < 8
    assert word[3] & 0xffff == cid and (word[3] >> 17) == status
    assert (word[3] >> 16) & 1 == (1 ^ ((queue_counts[queue] // 8) & 1))
    expected_status = 0x400b if mode == "falscher-namespace" and cid == 2 else 0
    assert status == expected_status, (queue,op,cid,status)
    queue_counts[queue] += 1
assert values("SCT") == [0] * len(commands)
assert values("SC") == ([0,11] if mode == "falscher-namespace" else [0] * len(commands))
assert values("DNR") == ([0,1] if mode == "falscher-namespace" else [0] * len(commands))
admin = [op for q,op,cid in commands if q == 0]
reads = [tuple(int(x,16) if i == 4 else int(x) for i,x in enumerate(m))
         for m in re.findall(r"pci_nvme_read cid (\d+) nsid (\d+) nlb (\d+) count (\d+) lba 0x([0-9a-f]+)", trace)]
if mode == "inventar":
    assert admin == [6,6] and not reads and all(q == 0 for q,op,cid in commands)
    assert values("INVENTAR BEREIT") == [1] and values("INVENTAR NSID") == [1] and values("READS") == [0]
    sn = b"".join(values("INVENTAR SN%d LE" % i)[0].to_bytes(n, "little") for i,n in enumerate((8,8,4)))
    assert sn == b"MOONVME-READONLY-001" and "MOO-KERNEL NVME GPT " not in serial
    assert not values("SERIE-OK")
elif mode == "falsche-serie":
    assert admin == [6] and not reads and all(q == 0 for q,op,cid in commands)
    assert values("SERIE-OK") == [0] and values("BEREIT") == [0] and values("READS") == [0]
    assert "MOO-KERNEL NVME GPT " not in serial
elif mode == "falscher-namespace":
    assert admin == [6,6] and not reads and all(q == 0 for q,op,cid in commands)
    assert values("SERIE-OK") == [1] and values("BEREIT") == [0] and values("READS") == [0]
    assert values("FEHLER") == [0x1e] and "MOO-KERNEL NVME GPT " not in serial
else:
    assert admin == [6,6,10,2,5,1,0,4], admin
    assert len(reads) == sum(q == 1 for q,op,cid in commands) == values("READS")[0]
    last = (out / "nvme-gpt.img").stat().st_size // block - 1
    assert all(ns == 1 and nlb == 1 and count == block and lba <= last for cid,ns,nlb,count,lba in reads)
    assert {0,1,last} <= {x[4] for x in reads}
    if mode == "gross":
        assert last > 0xffffffff and any(x[4] > 0xffffffff for x in reads)
    if block == 512:
        assert values("IO-CQ-UMLAUF")[0] >= 4
    assert values("ADMIN-CQ-UMLAUF") == [1]
    assert values("SERIE-OK") == [1] and values("BEREIT") == [1] and values("BLOCKBYTES") == [block]
    for marker in ("GPT HEADER CRC OK 0000000000000001", "GPT ARRAY CRC OK 0000000000000001",
                   "GPT BACKUP HEADER OK 0000000000000001", "GPT PARTITIONEN 0000000000000001",
                   "GPT ERSTE LBA 0000000000000800", "GPT LETZTE LBA 0000000000000FFF",
                   "GPT PART GUID {12345678-9ABC-DEF0-1234-56789ABCDEF0} 0000000000000001"):
        assert "MOO-KERNEL NVME " + marker in serial, marker
    assert "MOO-KERNEL NVME FEHLER " not in serial and "MOO-KERNEL NVME OPCODE-GESPERRT " not in serial
(out / "nvme-befehle.json").write_text(json.dumps(dict(mode=mode,blockbytes=block,commands=commands,reads=reads,completions=completions),indent=2)+"\n")
print("      OK: NVMe %s, %d Admin/%d Reads, jedes CQE/CID/Phase gegen QEMU-Trace, Image unveraendert" % (mode,len(admin),len(reads)))
PY
    [ "$?" -eq 0 ] || fail "NVMe-Opcode/CQE/GPT/Serien-/Nur-Lese-Gate"
  fi
  if [ "$NETZ_PING" != 0 ]; then
    [ ! -f "$OUT/ping.log" ] || sed 's/^/      | /' "$OUT/ping.log"
    [ "$PING_OK" -eq 1 ] || fail "Linux-Ping über $NETZ_PING fehlgeschlagen"
  fi
  if [ "$NETZ_TRANSPORT" = 1 ]; then
    for logfile in transport.log tcp-segmente.log; do
      [ ! -f "$OUT/$logfile" ] || sed 's/^/      | /' "$OUT/$logfile"
    done
    [ "$TRANSPORT_OK" -eq 1 ] || fail "UDP-/TCP-Probe ueber $NETZ_PING fehlgeschlagen"
  fi
  if [ "$NETZ_TCP_ECHO" = 1 ]; then
    grep -aqF 'MOO-KERNEL NETZ TCP ECHO BEREIT 0000000000000001' "$OUT/serial.log" || fail "TCP-Echo-Marker fehlt"
    [ ! -f "$OUT/tcp-fenster.log" ] || sed 's/^/      | /' "$OUT/tcp-fenster.log"
    [ "$TCP_FENSTER_OK" -eq 1 ] || fail "TCP-Fenster-/Wiederholungsprobe ueber $NETZ_PING fehlgeschlagen"
  fi
  if [ "$KRYPTO" != 0 ]; then
    uv run --no-project --offline python "$ROOT/skripte/p7/krypto-nachweis.py" "$OUT/serial.log" "$KRYPTO" >"$OUT/krypto.log" 2>&1 || {
      cat "$OUT/krypto.log"
      fail "Krypto-Selbsttest oder Entropieprofil $KRYPTO"
    }
    sed 's/^/      | /' "$OUT/krypto.log"
  fi
  [ "$NETZ" != 1 ] || kern_netz_pruefen "$OUT/net.pcap" "$OUT/serial.log" || fail "Kern-Netz-Log, Ping- oder Transport-pcap unvollstaendig"
  # Reihenfolge: Kern laeuft erst NACH ExitBootServices.
  Z_EBS="$(grep -an 'MOO-LADER EXITBOOTSERVICES OK' "$OUT/serial.log" | head -1 | cut -d: -f1)"
  Z_KERN="$(grep -an 'MOO-KERNEL START' "$OUT/serial.log" | head -1 | cut -d: -f1)"
  [ "$Z_EBS" -lt "$Z_KERN" ] || fail "MOO-KERNEL START steht nicht nach ExitBootServices"
  SPRUNG="$(grep -ao 'MOO-LADER SPRUNG [0-9A-F]*' "$OUT/serial.log" | awk '{print $3}')"
  [ "$((16#$SPRUNG))" -eq "$((ENTRY_ELF))" ] || fail "Sprungziel $SPRUNG != ELF-Einstieg $ENTRY_ELF"
  # Boot-Info gegen die Loader-Werte: Framebuffer und Stride kommen im Kern an.
  for f in "GOP:FB" "FB-BREITE:BREITE" "FB-HOEHE:HOEHE" "FB-STRIDE:STRIDE" "FB-FORMAT:FORMAT" "RSDP:RSDP"; do
    L="$(grep -ao "MOO-LADER ${f%%:*} [0-9A-F]*" "$OUT/serial.log" | awk '{print $3}')"
    K="$(grep -ao "MOO-KERNEL ${f##*:} [0-9A-F]*" "$OUT/serial.log" | awk '{print $3}')"
    [ -n "$L" ] && [ "$L" = "$K" ] || fail "Boot-Info ${f%%:*}: Loader '$L' != Kern '$K'"
  done
  KARTE="$(grep -ao 'MOO-KERNEL KARTE-ANZ [0-9A-F]*' "$OUT/serial.log" | awk '{print $3}')"
  [ "$((16#$KARTE))" -gt 0 ] || fail "Speicherkarte im Kern leer"
  PFA="$(grep -ao 'MOO-KERNEL PFA FREI-SEITEN [0-9A-F]*' "$OUT/serial.log" | awk 'NR==1{print $4}')"
  TABELLEN="$(grep -ao 'MOO-KERNEL TABELLEN FREI [0-9A-F]*' "$OUT/serial.log" | awk 'NR==1{print $4}')"
  TICKS="$(grep -ao 'MOO-KERNEL TIMER TICKS [0-9A-F]*' "$OUT/serial.log" | awk '{print $4}')"
  [ "$((16#$PFA))" -gt 0 ] || fail "PFA meldet keine freien Seiten"
  [ "$((16#$TABELLEN))" -gt 0 ] || fail "Firmware-/Loader-Seitentabellen nicht freigegeben"
  [ "$((16#$TICKS))" -ge 3 ] || fail "LAPIC-Timer lieferte weniger als drei Ticks"
  [ "$KEY_SENT" -eq 1 ] || fail "QEMU sendkey a wurde nicht ausgeloest"
  echo "      OK: Loader -> ExitBootServices (Zeile $Z_EBS) -> Kern (Zeile $Z_KERN), Sprung $SPRUNG = ELF-Einstieg, Boot-Info identisch, $((16#$KARTE)) Kartendeskriptoren, $((16#$PFA)) freie Frames, $((16#$TABELLEN)) alte Tabellenseiten, $((16#$TICKS)) Timer-Ticks, Eingabepfad USB=$USB"
  [ -s "$OUT/screenshot.png" ] || fail "kein Screenshot"
  if command -v magick >/dev/null 2>&1; then
    # Hintergrund (32,96,128) direkt vom Kern, Balken (255,196,64) ueber moo_surface_core.
    HG="$(magick "$OUT/screenshot.png" -format '%[pixel:p{10,400}]' info: 2>/dev/null)"
    BALKEN="$(magick "$OUT/screenshot.png" -format '%[pixel:p{45,45}]' info: 2>/dev/null)"
    case "$HG" in *"32,96,128"*) ;; *) fail "Hintergrund ist '$HG' statt srgb(32,96,128) (Kern hat nicht gezeichnet?)" ;; esac
    case "$BALKEN" in *"255,196,64"*) ;; *) fail "Balken ist '$BALKEN' statt srgb(255,196,64)" ;; esac
    # Text ueber moo_surface_core_text_3x5: \"M\" (Skala 12 ab 64,60) hat links eine
    # volle Spalte (dunkel), die Luecke zwischen M und O (Spalte 3) bleibt Balken;
    # Zeile 2 (Skala 6 ab 64,220) beginnt ebenfalls mit voller M-Spalte (hell).
    GLYPHE="$(magick "$OUT/screenshot.png" -format '%[pixel:p{70,100}]' info: 2>/dev/null)"
    LUECKE="$(magick "$OUT/screenshot.png" -format '%[pixel:p{106,100}]' info: 2>/dev/null)"
    ZEILE2="$(magick "$OUT/screenshot.png" -format '%[pixel:p{66,236}]' info: 2>/dev/null)"
    case "$GLYPHE" in *"20,20,20"*) ;; *) fail "Glyphe M (70,100) ist '$GLYPHE' statt srgb(20,20,20)" ;; esac
    case "$LUECKE" in *"255,196,64"*) ;; *) fail "Zeichenluecke (106,100) ist '$LUECKE' statt Balkenfarbe" ;; esac
    case "$ZEILE2" in *"240,240,240"*) ;; *) fail "Zeile 2 (66,236) ist '$ZEILE2' statt srgb(240,240,240)" ;; esac
    echo "      OK: Pixel Hintergrund $HG, Balken $BALKEN, Glyphe $GLYPHE, Luecke $LUECKE, Zeile 2 $ZEILE2"
    # P7-T6: Textkonsole (Surface-Text, Farbe 200,230,255) ab y=300 per Present sichtbar.
    KON="$(magick "$OUT/screenshot.png" -crop 2000x200+0+296 +repage -format %c histogram:info:- 2>/dev/null | awk '/\(200,230,255/{print $1}' | tr -d ':')"
    [ -n "$KON" ] && [ "$KON" -gt 200 ] || fail "Konsolentext (200,230,255) im Bereich y=296..496 fehlt ('$KON' Pixel)"
    echo "      OK: Konsolentext $KON Pixel in der Konsolenfarbe"
    if [ "$USB" = 1 ]; then
      EINGABE_PIXEL="$(magick "$OUT/screenshot.png" -gravity South -crop 2000x80+0+0 +repage -format %c histogram:info:- 2>/dev/null | awk '/[(]255,230,160/{print $1}' | tr -d ':')"
      [ -n "$EINGABE_PIXEL" ] && [ "$EINGABE_PIXEL" -gt 100 ] || fail "Fester Eingabetext nach Scroll-/Ringstress fehlt"
      echo "      OK: Fester Eingabebereich nach 140 Tasten und Konsolen-Scroll: $EINGABE_PIXEL Textpixel"
    fi
  fi
  tr -d '\000\r' <"$OUT/serial.log" | grep -aE "MOO-KERNEL (MESS|TSC)" | sed 's/^/      Messung: /'
  if command -v tesseract >/dev/null 2>&1; then
    # Nur Info: die 3x5-Pixelschrift liest tesseract unzuverlaessig (M -> H/W).
    echo "      Info OCR: $(tesseract "$OUT/screenshot.png" - --psm 6 2>/dev/null | tr '\n' ' ')"
  fi
  # Negativfall: ohne Kern-Datei kein ExitBootServices, Fehlermeldung, Rueckkehr zur Firmware.
  : >"$OUT/serial-ohne.log"
  timeout --foreground 40s qemu-system-x86_64 "${QEMU_ACCEL[@]}" \
      -drive if=pflash,format=raw,unit=0,readonly=on,file="$KOVMF_CODE" \
      -drive if=pflash,format=raw,unit=1,file="$OUT/vars-ohne.fd" \
      -drive if=virtio,format=raw,file="$OUT/ohne-kern.img" -net none \
      -m 256M -vga std -display none -no-reboot -monitor none \
      -serial file:"$OUT/serial-ohne.log" >"$OUT/qemu-ohne.log" 2>&1 &
  QPID=$!
  for ((i = 0; i < 35; i++)); do
    grep -aq "KERN NICHT GEFUNDEN" "$OUT/serial-ohne.log" 2>/dev/null && break
    kill -0 "$QPID" 2>/dev/null || break
    sleep 1
  done
  kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
  grep -aq "MOO-LADER FEHLER: KERN NICHT GEFUNDEN" "$OUT/serial-ohne.log" || fail "Negativfall: Fehlermeldung KERN NICHT GEFUNDEN fehlt"
  grep -aq "EXITBOOTSERVICES OK" "$OUT/serial-ohne.log" && fail "Negativfall: ExitBootServices trotz fehlendem Kern"
  echo "      OK: Negativfall ohne KERN.ELF: Meldung, kein ExitBootServices"
  if [ -n "${MOO_UEFI_OUT:-}" ]; then
    (cd "$OUT" && sha256sum BOOTX64.EFI KERN.ELF uefi.img screenshot.png serial.log >sha256.txt) && sed 's/^/      sha256: /' "$OUT/sha256.txt"
    sha256sum "$BIN" | sed 's/^/      sha256: /'
  fi
  echo "############################################################"
  echo "# ERGEBNIS: Kern-Pruefung gruen (Moo-Loader ohne C -> ExitBootServices -> Moo-Kern zeichnet)."
  echo "############################################################"
  exit 0
fi

# --- Tooling-Pruefung (clang/lld-link + MinGW-Header fuer den COFF-Build) ---
for t in clang lld-link; do
  command -v "$t" >/dev/null 2>&1 || skip "fehlendes Tooling: $t" \
    "sudo pacman -S clang lld" "Build-Tooling fehlt — UEFI-Smoke uebersprungen."
done
printf '#include <stdlib.h>\n#include <math.h>\n' \
  | clang "${CFLAGS[@]}" -fsyntax-only -x c - >/dev/null 2>&1 \
  || skip "MinGW-Header fuer x86_64-w64-windows-gnu fehlen (stdlib.h/math.h)" \
       "sudo pacman -S mingw-w64-headers" "Header fehlen — UEFI-Smoke uebersprungen."

FORMAT=obj
CLANG_EMIT=()
if [ "$PRUEFUNG" = "runtime" ] || [ "$MOO_SRC" -ef "$ROOT/beispiele/uefi/hallo_uefi.moos" ]; then
  for t in llvm-link opt llc; do
    command -v "$t" >/dev/null 2>&1 || fail "LLVM-Werkzeug fehlt: $t"
  done
  phase 1 "Moo mit Standard-Runtime -> LLVM-Bitcode"
  "$BIN" compile "$MOO_SRC" --target x86_64-unknown-uefi --emit-ir \
      -o "$OUT/uefi_moo.ll" >"$OUT/moo.err" 2>&1 \
    || { sed 's/^/      | /' "$OUT/moo.err"; fail "Moo-IR nicht gebaut"; }
  opt -mtriple=x86_64-w64-windows-gnu "$OUT/uefi_moo.ll" -o "$OUT/uefi_moo.bc" \
    || fail "Moo-Bitcode nicht gebaut"
  FORMAT=bc
  CLANG_EMIT=(-emit-llvm)
  CFLAGS+=(-DMOO_UEFI_COLLECTIONS -DMOO_HAS_SURFACE -D_CRTBLD -D__USE_MINGW_ANSI_STDIO=0)
  RT_SOURCES+=(moo_value.c moo_memory.c moo_string.c moo_list.c moo_dict.c
              moo_stdlib.c moo_print.c moo_frame.c moo_net.c)
else
  phase 1 "moo -> PE/COFF (--target x86_64-unknown-uefi)"
  "$BIN" compile "$MOO_SRC" --no-stdlib --target x86_64-unknown-uefi \
      -o "$OUT/uefi_moo.obj" >"$OUT/moo.err" 2>&1 \
    || { sed 's/^/      | /' "$OUT/moo.err"; fail "moo-COFF nicht gebaut"; }
  file "$OUT/uefi_moo.obj" | grep -qi "COFF" || fail "moo-Objekt ist kein COFF"
  echo "      OK: $(file -b "$OUT/uefi_moo.obj")"
fi

phase 2 "Entry + Runtime + Surface ($FORMAT, clang --target=x86_64-w64-windows-gnu)"
: >"$OUT/cc.err"
clang "${CFLAGS[@]}" "${CLANG_EMIT[@]}" -DMOO_UEFI_SURFACE -c "$ENTRY_C" -o "$OUT/uefi_entry.$FORMAT" 2>>"$OUT/cc.err" \
  || { sed 's/^/      | /' "$OUT/cc.err"; fail "uefi_entry.c-Build fehlgeschlagen"; }
clang "${CFLAGS[@]}" "${CLANG_EMIT[@]}" -DMOO_UEFI_USE_BARE_RUNTIME -c "$RT_C" -o "$OUT/uefi_rt.$FORMAT" 2>>"$OUT/cc.err" \
  || { sed 's/^/      | /' "$OUT/cc.err"; fail "uefi_rt.c-Build fehlgeschlagen"; }
clang "${CFLAGS[@]}" "${CLANG_EMIT[@]}" -c "$ROOT/compiler/runtime/boot/uefi_net.c" -o "$OUT/uefi_net.$FORMAT" 2>>"$OUT/cc.err" \
  || { sed 's/^/      | /' "$OUT/cc.err"; fail "uefi_net.c-Build fehlgeschlagen"; }
OBJS=("$OUT/uefi_entry.$FORMAT" "$OUT/uefi_moo.$FORMAT" "$OUT/uefi_rt.$FORMAT" "$OUT/uefi_net.$FORMAT")
for src in "${RT_SOURCES[@]}"; do
  obj="$OUT/${src%.c}.$FORMAT"
  clang "${CFLAGS[@]}" "${CLANG_EMIT[@]}" -c "$ROOT/compiler/runtime/$src" -o "$obj" 2>>"$OUT/cc.err" \
    || { sed 's/^/      | /' "$OUT/cc.err"; fail "$src-Build fehlgeschlagen"; }
  OBJS+=("$obj")
done

phase 3 "lld-link -> PE32+ EFI application"
if [ "$FORMAT" = "bc" ]; then
  # Die gemeinsame Runtime enthaelt auch Host-Funktionen. LLVM entfernt
  # unerreichbaren Code samt Importen vor dem COFF-Link, ab efi_main.
  llvm-link "${OBJS[@]}" -o "$OUT/uefi_all.bc" || fail "LLVM-Module nicht verbunden"
  opt -passes=internalize,globaldce -internalize-public-api-list=efi_main,_fltused \
      "$OUT/uefi_all.bc" -o "$OUT/uefi_live.bc" || fail "UEFI-Bitcode nicht reduziert"
  llc -mtriple=x86_64-unknown-uefi -filetype=obj "$OUT/uefi_live.bc" -o "$OUT/uefi_app.obj" \
    || fail "UEFI-COFF nicht gebaut"
  OBJS=("$OUT/uefi_app.obj")
fi
lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /opt:ref \
    /out:"$OUT/BOOTX64.EFI" "${OBJS[@]}" >"$OUT/link.err" 2>&1 \
  || { sed 's/^/      | /' "$OUT/link.err"; fail "BOOTX64.EFI nicht gelinkt"; }
file "$OUT/BOOTX64.EFI" | grep -qiE "EFI.*application|PE32\\+" || fail "kein PE32+ EFI-Binary"
echo "      OK: $(file -b "$OUT/BOOTX64.EFI")"

phase 4 "OVMF-Boot (GPT-Disk mit ESP) + Screenshot"
# Tooling fuer den echten Boot
MISS=()
for t in qemu-system-x86_64 parted mformat mmd mcopy; do
  command -v "$t" >/dev/null 2>&1 || MISS+=("$t")
done
OVMF_CODE=""
for c in /usr/share/edk2/x64/OVMF_CODE.4m.fd /usr/share/edk2-ovmf/x64/OVMF_CODE.4m.fd \
         /usr/share/OVMF/OVMF_CODE.4m.fd /usr/share/edk2/x64/OVMF_CODE.fd; do
  [ -f "$c" ] && { OVMF_CODE="$c"; break; }
done
OVMF_VARS=""
for v in /usr/share/edk2/x64/OVMF_VARS.4m.fd /usr/share/edk2-ovmf/x64/OVMF_VARS.4m.fd \
         /usr/share/OVMF/OVMF_VARS.4m.fd /usr/share/edk2/x64/OVMF_VARS.fd; do
  [ -f "$v" ] && { OVMF_VARS="$v"; break; }
done
[ -z "$OVMF_CODE" ] && MISS+=("OVMF_CODE.fd")
[ -z "$OVMF_VARS" ] && MISS+=("OVMF_VARS.fd")
if [ "${#MISS[@]}" -gt 0 ]; then
  skip "fehlendes Tooling: ${MISS[*]}" \
    "sudo pacman -S qemu-system-x86 parted mtools edk2-ovmf" \
    "Build-Phasen 1-3 gruen — OVMF-Boot GESKIPPT (Tooling)."
fi

DISK="$OUT/uefi.img"
rm -f "$DISK"
truncate -s 48M "$DISK" || fail "Image-Datei nicht anlegbar"
[ -f "$DISK" ] && [ ! -L "$DISK" ] || fail "Image ist keine regulaere Datei"
parted -s "$DISK" mklabel gpt mkpart ESP fat32 1MiB 100% set 1 esp on >/dev/null 2>&1 \
  || fail "parted GPT/ESP fehlgeschlagen"
OFF=$((2048*512))
mformat -i "$DISK"@@${OFF} -F :: >/dev/null 2>&1 || fail "mformat ESP fehlgeschlagen"
mmd -i "$DISK"@@${OFF} ::/EFI ::/EFI/BOOT >/dev/null 2>&1
mcopy -i "$DISK"@@${OFF} "$OUT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI >/dev/null 2>&1 \
  || fail "mcopy BOOTX64.EFI fehlgeschlagen"
# P7-T9: Fremder UEFI-Treiber (z. B. selbst gebautes iPXE intel.efidrv fuer die
# I226 des N100). Liegt er vor, kommt er nach \EFI\MOO\treiber\ und die NICs
# laufen OHNE Option-ROM (romfile=): SNP kann dann nur vom geladenen Treiber
# stammen. MOO_UEFI_TREIBER= (leer) erzwingt den Lauf ohne Treiber.
TREIBER="${MOO_UEFI_TREIBER-$ROOT/build/p7-ipxe/intel.efidrv}"
# Im e1000-Pruefmodus darf kein Firmware-Treiber die NIC binden.
[ "$PRUEFUNG" = "e1000" ] && TREIBER=""
[ "$PRUEFUNG" = "lader" ] && TREIBER=""
[ "$PRUEFUNG" = "netz-kern" ] && TREIBER=""
[ "${PRUEFUNG#nativ}" != "$PRUEFUNG" ] && TREIBER=""
[ "$PRUEFUNG" = "runtime" ] && TREIBER=""
NIC_ROM=""
TREIBER_ERWARTET="FEHLT"
if [ -n "$TREIBER" ] && [ -f "$TREIBER" ]; then
  mmd -i "$DISK"@@${OFF} ::/EFI/MOO ::/EFI/MOO/treiber >/dev/null 2>&1
  mcopy -i "$DISK"@@${OFF} "$TREIBER" ::/EFI/MOO/treiber/intel.efi >/dev/null 2>&1 \
    || fail "mcopy Treiber fehlgeschlagen"
  cp "$TREIBER" "$OUT/treiber.efi"
  NIC_ROM=",romfile="
  TREIBER_ERWARTET="OK"
  echo "      Treiber: $TREIBER -> \\EFI\\MOO\\treiber\\intel.efi (NICs ohne Option-ROM)"
else
  echo "      Treiber: keiner (erwartet MOO-UEFI-TREIBER-FEHLT, NICs mit QEMU-Option-ROM)"
fi

cp "$OVMF_VARS" "$OUT/vars.fd"
SERIAL_LOG="$OUT/serial.log"
DEBUG_LOG="$OUT/debug.log"
SHOT="$OUT/screenshot.png"
: >"$SERIAL_LOG"; : >"$DEBUG_LOG"; rm -f "$SHOT" "$OUT/monitor.in"

if [ "$PRUEFUNG" = "e1000" ]; then
  # P7-T14: Moo-e1000-Treiber (beispiele/uefi/netz_treiber.moos). q35 hat ECAM
  # (ACPI-MCFG); die NIC laeuft ohne Option-ROM, OVMF bindet sie also nicht.
  # slirp beantwortet die ARP-Anfrage nach 10.0.2.2 -> Empfangsbeweis.
  # P7-T18: MOO_UEFI_KVM=1 startet mit KVM (Messlaeufe); Standard bleibt TCG.
  timeout --foreground $((WAIT_S + 30))s qemu-system-x86_64 "${QEMU_ACCEL[@]}" -machine q35 \
      -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
      -drive if=pflash,format=raw,unit=1,file="$OUT/vars.fd" \
      -drive format=raw,file="$DISK" \
      -m 256M -vga std -display none -no-reboot -monitor none \
      -netdev user,id=n0 -device e1000,netdev=n0,mac=52:54:00:12:34:58,romfile= \
      -object filter-dump,id=f0,netdev=n0,file="$OUT/net.pcap" \
      -serial file:"$SERIAL_LOG" \
      -debugcon file:"$DEBUG_LOG" -global isa-debugcon.iobase=0xe9 \
      >"$OUT/qemu.log" 2>&1 &
  QPID=$!
  for ((i = 0; i < WAIT_S + 20; i++)); do
    grep -q "MOO-E1000-ENDE" "$SERIAL_LOG" 2>/dev/null && break
    kill -0 "$QPID" 2>/dev/null || break
    sleep 1
  done
  kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
  grep -a "MOO-E1000" "$SERIAL_LOG" | sed 's/^/      | /'
  grep -q "MOO-E1000-ENDE" "$SERIAL_LOG" || { sed 's/^/      | /' "$DEBUG_LOG" | tail -10; fail "Marker MOO-E1000-ENDE fehlt"; }
  grep -q "MOO-E1000-FEHLER" "$SERIAL_LOG" && fail "Moo-Treiber meldet FEHLER (siehe oben)"
  grep -q "MOO-E1000-SELBST 00" "$SERIAL_LOG" || fail "Selbsttests der Treiberlogik melden Fehler (MOO-E1000-SELBST != 00)"
  grep -q "MOO-E1000-I226 0" "$SERIAL_LOG" || fail "I226-Zaehlung ohne Geraet nicht 0"
  # P7-T10: 1000 Rahmen > 16 Ringplaetze; TX-Rueckgewinnung ohne Verwerfen und alle im pcap.
  grep -aq "MOO-E1000-SERIE 03ea 0000" "$SERIAL_LOG" || { grep -a "MOO-E1000-SERIE" "$SERIAL_LOG"; fail "Serie: erwartet 1002 gesendet (2 + 1000), 0 verworfen"; }
  SERIE=$(grep -a -o "MOO-E1000-SERIE" "$OUT/net.pcap" | wc -l)
  [ "$SERIE" -eq 1000 ] || fail "Serie: $SERIE statt 1000 Rahmen im pcap"
  echo "      OK: TX-Serie: 1000 Rahmen ueber 16er-Ring, 0 verworfen, 1000 im pcap"
  # P7-T10: 200 Logzeilen ueber 3 Ports (echt, festgefahrener Ring, ohne Link):
  # festgefahrener Port 15 gesendet/185 verworfen, Port ohne Link 0, je Zeile < 5 ms.
  grep -aq "MOO-E1000-LOG 000f 00b9 0000" "$SERIAL_LOG" || { grep -a "MOO-E1000-LOG" "$SERIAL_LOG"; fail "Log: erwartet LOG 000f 00b9 0000"; }
  LOGMS=$(tcpdump -r "$OUT/net.pcap" -n -tt -A 'udp port 7777' 2>/dev/null | awk '/^[0-9]{10}/{t=$1; next} /MOO-E1000-LOG/{if(!a)a=t; b=t; n++} END{if(n>1) printf "%d %.3f", n, (b-a)*1000/(n-1); else print n " 999"}')
  set -- $LOGMS
  [ "$1" -eq 200 ] || fail "Log: $1 statt 200 Logzeilen im pcap"
  awk -v ms="$2" 'BEGIN{exit !(ms < 5)}' || fail "Log: $2 ms je Zeile (Grenze 5 ms)"
  echo "      OK: 200 Logzeilen, $2 ms je Zeile, festgefahrener Port verwirft ohne Warten, Port ohne Link unberuehrt"
  ECAM="$(grep -ao 'MOO-E1000-ECAM [0-9a-f]*' "$SERIAL_LOG" | awk '{print $2}')"
  [ -n "$ECAM" ] && [ "$((16#$ECAM))" -ne 0 ] || fail "keine ECAM-Basis aus ACPI-MCFG"
  grep -q "MOO-E1000-PCI " "$SERIAL_LOG" || fail "e1000 im PCI-Scan nicht gefunden"
  grep -q "MOO-E1000-MAC 525400123458" "$SERIAL_LOG" || fail "MAC stimmt nicht (erwartet 52:54:00:12:34:58)"
  grep -q "MOO-E1000-LINK 1" "$SERIAL_LOG" || fail "kein Link"
  grep -q "MOO-E1000-TX 2" "$SERIAL_LOG" || fail "Senden: nicht beide Rahmen fertig (STA.DD)"
  grep -q "MOO-E1000-ARP 1" "$SERIAL_LOG" || fail "Empfang: keine ARP-Antwort von 10.0.2.2"
  grep -aq "MOO-E1000-MESS .* 01adb0" "$SERIAL_LOG" || fail "Messzeile MESS fehlt oder Link-Zaehler != 110000"
  [ -s "$OUT/net.pcap" ] || fail "kein net.pcap"
  HALLO=$(grep -a -c "MOO-E1000-HALLO" "$OUT/net.pcap")
  [ "$HALLO" -ge 1 ] || fail "UDP-Rahmen 'MOO-E1000-HALLO' fehlt im pcap"
  echo "      OK: Moo-e1000: ECAM 0x$ECAM, MAC/Link ok, 2 Rahmen gesendet, ARP-Antwort empfangen, pcap $(stat -c %s "$OUT/net.pcap") Bytes"
  if [ -n "${MOO_UEFI_OUT:-}" ]; then
    (cd "$OUT" && sha256sum BOOTX64.EFI uefi.img net.pcap serial.log >sha256.txt) && sed 's/^/      sha256: /' "$OUT/sha256.txt"
  fi
  echo "############################################################"
  echo "# ERGEBNIS: e1000-Pruefung gruen (Moo-Treiber sendet und empfaengt in QEMU)."
  echo "############################################################"
  exit 0
fi
if [ "$PRUEFUNG" = "nativ" ] || [ "$PRUEFUNG" = "nativ-trap" ] || [ "$PRUEFUNG" = "runtime" ]; then
  # P7-T17: @nativ-Systemkoerper mit dynamischen Werten im UEFI-Profil.
  # Virtio-Block vermeidet QEMU11-IDE-SIGSEGV vor EFI-Entry.
  # Die App schreibt Zeilen "N <wert>" auf COM1. nativ: alle Werte exakt
  # (u64 2^60+8, i64 -6e18-1), danach ruft sie sich als nativ-trap mit dem
  # Negativfall auf (256 an u8 -> Trap, keine Folgezeile).
  timeout --foreground $((WAIT_S + 30))s qemu-system-x86_64 "${QEMU_ACCEL[@]}" -machine q35 \
      -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
      -drive if=pflash,format=raw,unit=1,file="$OUT/vars.fd" \
      -drive if=virtio,format=raw,file="$DISK" \
      -m 256M -vga std -display none -no-reboot -monitor none -net none \
      -serial file:"$SERIAL_LOG" \
      -debugcon file:"$DEBUG_LOG" -global isa-debugcon.iobase=0xe9 \
      >"$OUT/qemu.log" 2>&1 &
  QPID=$!
  ENDE="N 999"; [ "$PRUEFUNG" = "nativ-trap" ] && ENDE="N 111"
  MARKER_LOG="$SERIAL_LOG"
  if [ "$PRUEFUNG" = "runtime" ]; then
    MARKER_LOG="$DEBUG_LOG"
    ENDE="MOO-UEFI-OK"
  fi
  for ((i = 0; i < WAIT_S + 20; i++)); do
    tr -d '\r' <"$MARKER_LOG" 2>/dev/null | grep -qax "$ENDE" && break
    kill -0 "$QPID" 2>/dev/null || break
    sleep 1
  done
  # Negativfall: Zeit fuer (verbotene) Folgezeilen nach dem Trap.
  [ "$PRUEFUNG" = "nativ-trap" ] && sleep 5
  kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
  if [ "$PRUEFUNG" = "runtime" ]; then
    grep -aE "MOO-UEFI|KERN-PANIK" "$DEBUG_LOG" | sed 's/^/      | /'
    for m in MOO-UEFI-RUNTIME-WERTE-OK MOO-UEFI-RUNTIME-ARENA-OK MOO-UEFI-OK; do
      grep -qax "$m" "$DEBUG_LOG" || fail "Runtime-Marker fehlt: $m"
    done
    echo "# ERGEBNIS: Runtime-Pruefung gruen (Werte, Callback und Tasten-Arena unter OVMF)."
    exit 0
  fi
  tr -d '\r' <"$SERIAL_LOG" | grep -a '^N ' | sed 's/^/      | /'
  if [ "$PRUEFUNG" = "nativ" ]; then
    # P7-T19: "N 3"/"N 90" = u64 aus @nativ als Laenge/Adresse an kern_text_in_puffer.
    # P7-T22: "N 700481670"/"N 165" = reine Hardwarekoerper (Port-I/O) dynamisch gerufen.
    for m in "N -12345" "N 213" "N 1152921504606846984" "N -6000000000000000001" "N 1" "N 3" "N 90" "N 700481670" "N 165" "N 2023" "N 2024" "N 2025" "N 2026" "N 2027" "N 2028" "N 2029" "N 2030" "N 2031" "N 2032" "N 2033" "N 2034" "N 2035" "N 2036" "N 2037" "N 999"; do
      tr -d '\r' <"$SERIAL_LOG" | grep -qax -- "$m" || fail "Zeile '$m' fehlt auf COM1"
    done
    echo "      OK: dynamische Argumente, exakte i64/u64-Argumente und -Rueckgaben im UEFI-Profil"
    MOO_UEFI_PRUEFUNG=nativ-trap MOO_UEFI_APP="$ROOT/compiler/tests/p7/nativ_uefi_dyn_trap.moos" \
      MOO_UEFI_OUT="$OUT/trap" bash "$0" >"$OUT/trap.log" 2>&1 \
      || { sed 's/^/      | /' "$OUT/trap.log" | tail -20; fail "Negativfall (Trap) nicht gruen"; }
    grep -q "ERGEBNIS: nativ-trap gruen" "$OUT/trap.log" \
      || { sed 's/^/      | /' "$OUT/trap.log" | tail -20; fail "Negativfall ohne Ergebniszeile (SKIP?)"; }
    echo "      OK: Negativfall: 256 an u8 -> Trap, keine Folgezeile"
    # P7-T19: Typfehler an einem C-Builtin (Text statt Zahl) -> Trap.
    MOO_UEFI_PRUEFUNG=nativ-trap MOO_UEFI_APP="$ROOT/compiler/tests/p7/nativ_uefi_typ_trap.moos" \
      MOO_UEFI_OUT="$OUT/typ" bash "$0" >"$OUT/typ.log" 2>&1 \
      || { sed 's/^/      | /' "$OUT/typ.log" | tail -20; fail "Negativfall Typfehler nicht gruen"; }
    grep -q "ERGEBNIS: nativ-trap gruen" "$OUT/typ.log" \
      || { sed 's/^/      | /' "$OUT/typ.log" | tail -20; fail "Negativfall Typfehler ohne Ergebniszeile"; }
    echo "      OK: Negativfall: Text an kern_warte_us -> Trap, keine Folgezeile"
    echo "############################################################"
    echo "# ERGEBNIS: nativ-Pruefung gruen (Positiv- und Negativfall in OVMF)."
    echo "############################################################"
    exit 0
  fi
  tr -d '\r' <"$SERIAL_LOG" | grep -qax "N 111" || fail "Zeile 'N 111' vor dem Trap fehlt"
  for m in "N 222" "N 999"; do
    tr -d '\r' <"$SERIAL_LOG" | grep -qax "$m" && fail "Zeile '$m' nach dem Trap erschienen"
  done
  echo "# ERGEBNIS: nativ-trap gruen (Trap haelt die App an)."
  exit 0
fi
if [ "$PRUEFUNG" = "lader" ]; then
  # P7-T10: Nutzlast bauen und pruefen (e1000-Modus dieses Skripts), dann per
  # QEMU-slirp-TFTP (-netdev user,tftp=) an den Lader ausliefern.
  MOO_UEFI_PRUEFUNG=e1000 MOO_UEFI_OUT="$OUT/nutzlast" bash "$0" >"$OUT/nutzlast.log" 2>&1 \
    || { tail -5 "$OUT/nutzlast.log" | sed 's/^/      | /'; fail "Nutzlast (e1000-Modus) nicht gruen"; }
  echo "      OK: Nutzlast gebaut und einzeln gruen ($OUT/nutzlast/BOOTX64.EFI)"
  pruefdatei() {  # Datei Ziel: "<laenge> <crc32hex>" (CRC32 aus dem gzip-Trailer, RFC 1952)
    printf '%s %s\n' "$(stat -c %s "$1")" "$(gzip -c -n "$1" | tail -c8 | od -An -tx4 -N4 | tr -d ' \n')" >"$2"
  }
  mkdir -p "$OUT/tftp" "$OUT/tftp-falsch"
  cp "$OUT/nutzlast/BOOTX64.EFI" "$OUT/tftp/moo.efi"
  pruefdatei "$OUT/tftp/moo.efi" "$OUT/tftp/moo.efi.pruef"
  cp "$OUT/tftp/moo.efi" "$OUT/tftp-falsch/moo.efi"
  awk '{ printf "%s 00000000\n", $1 }' "$OUT/tftp/moo.efi.pruef" >"$OUT/tftp-falsch/moo.efi.pruef"
  echo "      Pruefdatei: $(cat "$OUT/tftp/moo.efi.pruef")"
  lader_qemu() {  # tftp-Verzeichnis, Serial-Log, Pcap, Endemuster
    cp "$OVMF_VARS" "$OUT/vars-lader.fd"
    : >"$2"
    timeout --foreground $((WAIT_S + 60))s qemu-system-x86_64 "${QEMU_ACCEL[@]}" -machine q35 \
        -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
        -drive if=pflash,format=raw,unit=1,file="$OUT/vars-lader.fd" \
        -drive format=raw,file="$DISK" \
        -m 512M -vga std -display none -no-reboot -monitor none \
        -netdev user,id=n0,tftp="$1" -device e1000,netdev=n0,mac=52:54:00:12:34:58,romfile= \
        -object filter-dump,id=f0,netdev=n0,file="$3" \
        -serial file:"$2" -debugcon file:"$DEBUG_LOG" -global isa-debugcon.iobase=0xe9 \
        >"$OUT/qemu-lader.log" 2>&1 &
    local pid=$! i
    for ((i = 0; i < WAIT_S + 50; i++)); do
      grep -aq -- "$4" "$2" 2>/dev/null && break
      kill -0 "$pid" 2>/dev/null || break
      sleep 1
    done
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
    grep -a -E "MOO-LADER|MOO-UEFI-BILD|MOO-E1000-(SELBST|ARP|ENDE)" "$2" | tr -d '\000' | sed 's/^/      | /'
  }
  lader_qemu "$OUT/tftp" "$SERIAL_LOG" "$OUT/net.pcap" "MOO-E1000-ENDE"
  for m in "MOO-LADER NETZKARTEN: 1 (E1000)" "MOO-LADER LINK: PORT 0" "MOO-LADER SERVER 10.0.2.2" \
           "MOO-LADER PRUEFSUMME OK" "MOO-LADER STARTE IMAGE" "MOO-UEFI-BILD-START" \
           "MOO-E1000-SELBST 00" "MOO-E1000-ARP 1" "MOO-E1000-ENDE"; do
    grep -aq -- "$m" "$SERIAL_LOG" || fail "Lader: '$m' fehlt"
  done
  grep -aq "MOO-LADER FEHLER" "$SERIAL_LOG" && fail "Lader meldet FEHLER"
  echo "      OK: Lader holt moo.efi per TFTP, CRC32/PE ok, startet die Nutzlast, Nutzlast laeuft (ARP 1, ENDE)"
  # Negativfall: falsche Pruefsumme -> nichts starten, Statusanzeige bleibt.
  lader_qemu "$OUT/tftp-falsch" "$OUT/serial-falsch.log" "$OUT/net-falsch.pcap" "MOO-LADER R = NEU"
  grep -aq "MOO-LADER FEHLER: PRUEFSUMME FALSCH" "$OUT/serial-falsch.log" || fail "Negativfall: PRUEFSUMME FALSCH fehlt"
  grep -aq "MOO-UEFI-BILD-START" "$OUT/serial-falsch.log" && fail "Negativfall: Image trotz falscher Pruefsumme gestartet"
  grep -aq "MOO-LADER R = NEU" "$OUT/serial-falsch.log" || fail "Negativfall: Statusanzeige (R/ESC) fehlt"
  echo "      OK: Negativfall: falsche Pruefsumme erkannt, nichts gestartet, Statusanzeige bleibt"
  if [ -n "${MOO_UEFI_OUT:-}" ]; then
    (cd "$OUT" && sha256sum BOOTX64.EFI uefi.img tftp/moo.efi tftp/moo.efi.pruef net.pcap serial.log >sha256.txt) && sed 's/^/      sha256: /' "$OUT/sha256.txt"
  fi
  echo "############################################################"
  echo "# ERGEBNIS: Lader-Pruefung gruen (TFTP, Pruefung, Start aus dem Speicher, Negativfall)."
  echo "############################################################"
  exit 0
fi
if [ "$PRUEFUNG" = "netz-kern" ]; then
  # P7-T5: N100-Weg in QEMU. Auf der ESP liegt nur der Netz-Lader (wie auf dem
  # Stick); per TFTP kommen moo.efi = Moo-UEFI-Loader und moo.kern = Moo-Kern.
  # Der Netz-Lader reicht den Kern als LoadOptions (MooKernUebergabe) weiter,
  # der Loader springt nach ExitBootServices hinein.
  MOO_UEFI_PRUEFUNG=kern MOO_UEFI_OUT="$OUT/kern" bash "$0" >"$OUT/kern.log" 2>&1 \
    || { tail -5 "$OUT/kern.log" | sed 's/^/      | /'; fail "Kern-Modus (Loader + Kern) nicht gruen"; }
  echo "      OK: Loader und Kern gebaut und einzeln gruen ($OUT/kern)"
  mkdir -p "$OUT/tftp"
  cp "$OUT/kern/BOOTX64.EFI" "$OUT/tftp/moo.efi"
  cp "$OUT/kern/KERN.ELF" "$OUT/tftp/moo.kern"
  for f in moo.efi moo.kern; do  # "<laenge> <crc32hex>" (CRC32 aus dem gzip-Trailer)
    printf '%s %s\n' "$(stat -c %s "$OUT/tftp/$f")" \
      "$(gzip -c -n "$OUT/tftp/$f" | tail -c8 | od -An -tx4 -N4 | tr -d ' \n')" >"$OUT/tftp/$f.pruef"
  done
  cp "$OVMF_VARS" "$OUT/vars-netz.fd"
  : >"$SERIAL_LOG"; : >"$DEBUG_LOG"
  NETZ_KERN_QEMU=()
  timeout --foreground $((WAIT_S + 60))s qemu-system-x86_64 "${QEMU_ACCEL[@]}" -machine q35 "${NETZ_KERN_QEMU[@]}" \
      -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
      -drive if=pflash,format=raw,unit=1,file="$OUT/vars-netz.fd" \
      -drive format=raw,file="$DISK" \
      -m 512M -vga std -display none -no-reboot -monitor none \
      -netdev user,id=n0,tftp="$OUT/tftp" -device e1000,netdev=n0,mac=52:54:00:12:34:58,romfile= \
      -object filter-dump,id=nklog,netdev=n0,file="$OUT/net.pcap" \
      -serial file:"$SERIAL_LOG" -debugcon file:"$DEBUG_LOG" -global isa-debugcon.iobase=0xe9 \
      >"$OUT/qemu-netz.log" 2>&1 &
  QPID=$!
  for ((i = 0; i < WAIT_S + 50; i++)); do
    grep -aq "MOO-KERNEL OK" "$SERIAL_LOG" 2>/dev/null && break
    grep -aqE "MOO-LADER (FEHLER|R = NEU)" "$SERIAL_LOG" 2>/dev/null && break
    kill -0 "$QPID" 2>/dev/null || break
    sleep 1
  done
  kill "$QPID" 2>/dev/null; wait "$QPID" 2>/dev/null
  tr -d '\000\r' <"$SERIAL_LOG" | grep -aE "MOO-(LADER|KERNEL|UEFI-BILD)" | sed 's/^/      | /'
  for m in "UEBERGABE AN MOO.EFI" "MOO-UEFI-BILD-KERN" "MOO-LADER START (MOO, OHNE C)" \
           "MOO-LADER KERN AUS UEBERGABE" "MOO-LADER EXITBOOTSERVICES OK" \
           "MOO-KERNEL RIP FFFFFFFF8" "MOO-KERNEL OK"; do
    grep -aqF -- "$m" "$SERIAL_LOG" || fail "Netz-Kern: '$m' fehlt"
  done
  grep -aq "MOO-LADER FEHLER" "$SERIAL_LOG" && fail "Netz-Kern: Fehler gemeldet"
  kern_netz_pruefen "$OUT/net.pcap" "$SERIAL_LOG" || fail "Netz-Boot: Kern-Netz-Log unvollstaendig"
  echo "      OK: Netz-Lader -> TFTP moo.efi + moo.kern -> Uebergabe -> Moo-Loader -> ExitBootServices -> Moo-Kern"
  if [ -n "${MOO_UEFI_OUT:-}" ]; then
    (cd "$OUT" && sha256sum BOOTX64.EFI tftp/moo.efi tftp/moo.efi.pruef tftp/moo.kern tftp/moo.kern.pruef serial.log >sha256.txt) && sed 's/^/      sha256: /' "$OUT/sha256.txt"
  fi
  echo "############################################################"
  echo "# ERGEBNIS: Netz-Kern-Pruefung gruen (N100-Weg: Netz-Lader -> Moo-Loader -> Moo-Kern)."
  echo "############################################################"
  exit 0
fi
# QEMU-Monitor ueber FIFO: nach dem Rueckkehr-Marker 'screendump', dann 'quit'.
mkfifo "$OUT/monitor.in" || fail "FIFO fuer QEMU-Monitor nicht anlegbar"
exec 3<>"$OUT/monitor.in"
timeout --foreground $((WAIT_S + 30))s qemu-system-x86_64 "${QEMU_ACCEL[@]}" \
    -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,unit=1,file="$OUT/vars.fd" \
    -drive format=raw,file="$DISK" \
    -m 128M -vga std -display none -no-reboot \
    -device qemu-xhci -device usb-tablet \
    -netdev user,id=n0 -device e1000,netdev=n0,mac=52:54:00:12:34:56${NIC_ROM} \
    -netdev user,id=n1 -device e1000,netdev=n1,mac=52:54:00:12:34:57${NIC_ROM} \
    -object filter-dump,id=f0,netdev=n0,file="$OUT/net.pcap" \
    -object filter-dump,id=f1,netdev=n1,file="$OUT/net1.pcap" \
    -monitor stdio -serial file:"$SERIAL_LOG" \
    -debugcon file:"$DEBUG_LOG" -global isa-debugcon.iobase=0xe9 \
    <"$OUT/monitor.in" >"$OUT/monitor.log" 2>&1 &
QPID=$!
qemu_ende() { printf 'quit\n' >&3 2>/dev/null || true; exec 3>&-; wait "$QPID" 2>/dev/null || true; }

warte_marker() {  # Datei Muster Sekunden
  local f="$1" m="$2" n="${3:-15}" i
  for ((i = 0; i < n; i++)); do
    grep -q -- "$m" "$f" 2>/dev/null && return 0
    kill -0 "$QPID" 2>/dev/null || return 1
    sleep 1
  done
  return 1
}
schnappschuss() {  # Pfad
  rm -f "$1"; printf 'screendump %s -f png\n' "$1" >&3
  for ((i = 0; i < 15; i++)); do [ -s "$1" ] && break; sleep 1; done
  [ -s "$1" ]
}
monitor() { printf '%s\n' "$1" >&3; }

# Start: Moo hat gezeichnet und praesentiert (MOO-UEFI-BILD) und wartet in
# der Ereignisschleife (kern_eingabe_warten) auf Tastatur/Maus.
if ! warte_marker "$SERIAL_LOG" "MOO-UEFI-BILD" "$WAIT_S"; then
  qemu_ende
  sed 's/^/      | /' "$DEBUG_LOG" "$SERIAL_LOG" | tail -20
  fail "Marker MOO-UEFI-BILD fehlt (GOP/HEAP-FAIL oder kern_bild_zeige lieferte falsch)"
fi
schnappschuss "$OUT/screenshot_start.png" || { qemu_ende; fail "kein Start-Screenshot"; }
# Kabel an NIC 2 ziehen (nach der Startzeile): ab jetzt darf nur NIC 1 senden.
monitor 'set_link n1 off'
sleep 1
# Tastatur: Tab wechselt den Fokus auf INFO, Enter loest INFO aus.
monitor 'sendkey tab'
warte_marker "$SERIAL_LOG" "MOO-UEFI-FOKUS 1" 10 \
  || { qemu_ende; fail "Tastatur: Tab erreicht Moo nicht (MOO-UEFI-FOKUS 1 fehlt)"; }
monitor 'sendkey ret'
warte_marker "$SERIAL_LOG" "MOO-UEFI-AKTION 1" 10 \
  || { qemu_ende; fail "Tastatur: Enter loest INFO nicht aus (MOO-UEFI-AKTION 1 fehlt)"; }
schnappschuss "$OUT/screenshot_tastatur.png" || { qemu_ende; fail "kein Tastatur-Screenshot"; }
# Maus (usb-tablet, absolut 0..32767): Klick auf die Mitte von WEITER.
# Layout-Herleitung (Font 19 px, 1280x800): Knopf x=52..138, y=253..296 -> Mitte (95,274).
MAUS="Zeigerprotokoll fehlt (fail-closed: MOO-UEFI-MAUS-FEHLT), nur Tastatur geprueft"
if grep -q "MOO-UEFI-MAUS-OK" "$DEBUG_LOG"; then
  # QEMU schickt Mausbefehle an das aktive Geraet (Standard: PS/2-Maus #2, die
  # OVMF nicht als Pointer-Protokoll anbietet). Das usb-tablet per 'info mice'
  # finden und mit 'mouse_set' aktivieren.
  monitor 'info mice'
  sleep 1
  TABLET="$(tr -d '\r' < "$OUT/monitor.log" | grep -oE 'Mouse #[0-9]+: QEMU HID Tablet' | tail -1 | grep -oE '[0-9]+')"
  [ -n "$TABLET" ] || { qemu_ende; fail "Maus: usb-tablet nicht in 'info mice' gefunden"; }
  monitor "mouse_set $TABLET"
  sleep 1
  monitor "mouse_move $((95 * 32767 / 1280)) $((274 * 32767 / 800))"
  sleep 1
  monitor 'mouse_button 1'; sleep 1; monitor 'mouse_button 0'
  warte_marker "$SERIAL_LOG" "MOO-UEFI-KLICK" 10 \
    || { qemu_ende; fail "Maus: Klick auf WEITER nicht erkannt (MOO-UEFI-KLICK fehlt)"; }
  warte_marker "$SERIAL_LOG" "MOO-UEFI-AKTION 0" 10 \
    || { qemu_ende; fail "Maus: Klick loest WEITER nicht aus (MOO-UEFI-AKTION 0 fehlt)"; }
  MAUS="Klick auf WEITER erkannt (MOO-UEFI-KLICK, MOO-UEFI-AKTION 0)"
fi
schnappschuss "$SHOT" || { qemu_ende; fail "kein End-Screenshot ($SHOT)"; }
# Escape beendet die Moo-Schleife -> Rueckkehr aus moo_efi_haupt -> MOO-UEFI-OK.
monitor 'sendkey esc'
warte_marker "$DEBUG_LOG" "MOO-UEFI-OK" 10 \
  || { qemu_ende; fail "Escape beendet die Ereignisschleife nicht (MOO-UEFI-OK fehlt)"; }
qemu_ende

grep -q "MOO-UEFI-ENTRY"   "$DEBUG_LOG" || fail "Marker MOO-UEFI-ENTRY fehlt"
grep -q "MOO-UEFI-GOP-OK"  "$DEBUG_LOG" || fail "Marker MOO-UEFI-GOP-OK fehlt (GOP nicht gefunden/Format)"
grep -q "MOO-UEFI-HEAP-OK" "$DEBUG_LOG" || fail "Marker MOO-UEFI-HEAP-OK fehlt (AllocatePages)"
grep -q "MOO-UEFI-WATCHDOG-AUS" "$DEBUG_LOG" || fail "Marker MOO-UEFI-WATCHDOG-AUS fehlt (SetWatchdogTimer(0) - sonst Neustart nach 5 min)"
grep -q "MOO-UEFI-MOO"     "$SERIAL_LOG" || { sed 's/^/      | /' "$SERIAL_LOG" | tail -10; fail "Marker MOO-UEFI-MOO fehlt (moo-Teil lief nicht)"; }
grep -q "MOO-UEFI-TASTE"   "$SERIAL_LOG" || fail "Marker MOO-UEFI-TASTE fehlt"
# P7-T15: QEMU hat keine I226 -> der Moo-Treiber findet 0 Ports (Statuszeile "I226 FEHLT").
grep -q "MOO-UEFI-I226 0" "$SERIAL_LOG" || fail "Marker MOO-UEFI-I226 0 fehlt (I226-Erkennung ohne Geraet)"
echo "      OK: I226-Erkennung ohne Geraet: MOO-UEFI-I226 0"
grep -q "MOO-UEFI-TREIBER-$TREIBER_ERWARTET" "$DEBUG_LOG" \
  || { grep -a "MOO-UEFI-TREIBER" "$DEBUG_LOG" | sed 's/^/      | /'; fail "Marker MOO-UEFI-TREIBER-$TREIBER_ERWARTET fehlt"; }
echo "      OK: Treiber-Lader meldet MOO-UEFI-TREIBER-$TREIBER_ERWARTET"
# Netzwerk-Konsole: SNP in OVMF (e1000) muss da sein; Nutzlast im pcap pruefen.
grep -q "MOO-UEFI-NETZ-OK" "$DEBUG_LOG" || fail "Marker MOO-UEFI-NETZ-OK fehlt (SNP/e1000 nicht initialisiert)"
# Zwei NICs (n0, n1), beide bei Init mit Link. Danach zieht das Gate das Kabel an n1
# (Monitor set_link n1 off, siehe unten) und prueft, dass nur n0 noch sendet.
grep -q "MOO-NET-NICS 2 LINKS 2" "$DEBUG_LOG" || fail "NIC-Zaehlung falsch: erwartet 2 NICs mit Link (MOO-NET-NICS 2 LINKS 2 fehlt, siehe $DEBUG_LOG)"
[ -s "$OUT/net.pcap" ] || fail "kein net.pcap (filter-dump)"
NETZ_START=$(grep -a -c "MOO-NET MOOS STARTET" "$OUT/net.pcap")
NETZ_TASTE=$(grep -a -c "Return" "$OUT/net.pcap")
[ "$NETZ_START" -ge 1 ] || fail "UDP-Log 'MOO-NET MOOS STARTET' fehlt im pcap"
[ "$NETZ_TASTE" -ge 1 ] || fail "UDP-Log des Tastennamens 'Return' fehlt im pcap"
[ -s "$OUT/net1.pcap" ] || fail "kein net1.pcap (filter-dump NIC 2)"
NIC1_START=$(grep -a -c "MOO-NET MOOS STARTET" "$OUT/net1.pcap")
NIC1_TASTE=$(grep -a -c "Return" "$OUT/net1.pcap")
[ "$NIC1_START" -ge 1 ] || fail "NIC 2 (Link an) sendete die Startzeile nicht"
[ "$NIC1_TASTE" -eq 0 ] || fail "NIC 2 (Link aus) sendet trotzdem $NIC1_TASTE Tastenzeile(n)"
echo "      OK: Zwei NICs: NIC0 Return=$NETZ_TASTE (Link an); NIC1 Start=$NIC1_START vor Kabelzug, Return=$NIC1_TASTE nach Kabelzug"
echo "      OK: Netzwerk-Konsole: $NETZ_START Startzeile(n), $NETZ_TASTE Tastenzeile(n) im pcap $(stat -c %s "$OUT/net.pcap") Bytes"
echo "      OK: Tastatur Tab -> FOKUS 1, Enter -> AKTION 1; Maus: $MAUS"
[ -s "$SHOT" ] || fail "kein Screenshot ($SHOT) — QEMU-Monitor screendump"
grep -E "MOO-UEFI" "$DEBUG_LOG" "$SERIAL_LOG" | sed 's/^/      | /'
echo "      OK: Screenshot $(stat -c %s "$SHOT") Bytes -> $SHOT"
if command -v magick >/dev/null 2>&1; then
  # Akzentlinie (y = kopf_h .. kopf_h+3, kopf_h = 19*3 + 32 = 89) ist nach einer
  # Aktion bernstein (255,196,64) statt tuerkis (73,192,169).
  AKZENT="$(magick "$SHOT" -format '%[pixel:p{5,90}]' info: 2>/dev/null)"
  case "$AKZENT" in
    *"255,196,64"*) echo "      OK: Akzentlinie nach Aktion bernstein ($AKZENT)" ;;
    *) fail "Akzentlinie zeigt keine Aktion (erwartet srgb(255,196,64), ist '$AKZENT')" ;;
  esac
fi
OCR="uebersprungen (tesseract fehlt)"
if command -v tesseract >/dev/null 2>&1; then
  tesseract "$SHOT" "$OUT/ocr" --psm 6 >/dev/null 2>&1
  if grep -Fq "MOO UEFI SCREEN" "$OUT/ocr.txt" 2>/dev/null; then
    OCR="'MOO UEFI SCREEN' erkannt"
  else
    sed 's/^/      | ocr: /' "$OUT/ocr.txt" 2>/dev/null | head -10
    fail "OCR findet 'MOO UEFI SCREEN' nicht im Screenshot (Firmware-Bild statt Moo-Bild?)"
  fi
fi
echo "      OK: OCR $OCR"
if command -v tesseract >/dev/null 2>&1; then
  echo "      Statuszeile (OCR, nur Info): $(tesseract "$SHOT" - --psm 11 2>/dev/null | grep -aE 'I226|1226|TREIBER|NETZ' | tr '\n' ' ')"
fi
if [ -n "${MOO_UEFI_OUT:-}" ]; then
  (cd "$OUT" && sha256sum BOOTX64.EFI uefi.img screenshot_start.png screenshot_tastatur.png screenshot.png net.pcap net1.pcap $( [ -f treiber.efi ] && echo treiber.efi ) >sha256.txt) && sed 's/^/      sha256: /' "$OUT/sha256.txt"
  sha256sum "$BIN" | sed 's/^/      sha256: /'
fi
echo "############################################################"
echo "# ERGEBNIS: ALLE 4 Phasen gruen (moo zeichnet als UEFI-App auf den GOP-Bildschirm)."
echo "############################################################"
