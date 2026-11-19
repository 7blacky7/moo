#!/usr/bin/env python3
"""OVMF gate with a low-speed and two full-speed USB boot keyboards."""

import argparse
import pathlib
import re
import shutil
import socket
import struct
import subprocess
import threading
import time


# usbredirproto.h (usbredir 0.15): hello is 32-bit, subsequent ids are 64-bit.
HELLO = struct.Struct("<III")
FRAME = struct.Struct("<IIQ")
CONTROL = struct.Struct("<BBBBHHH")
# QEMU redirect.c usbredir_attach requires max-packet, 64-bit IDs and
# 32-bit bulk lengths even on a low-/full-speed USB port paired with xHCI.
CAPS = (1 << 1) | (1 << 4) | (1 << 5) | (1 << 6)
REPORT = bytes.fromhex(
    "05 01 09 06 A1 01 05 07 19 E0 29 E7 15 00 25 01 75 01 95 08 81 02 "
    "95 01 75 08 81 01 95 06 75 08 15 00 25 65 19 00 29 65 81 00 C0"
)
PORT = 0x81
SRC = re.compile(
    rb"MOO-KERNEL USB TASTE BDF ([0-9A-F]{16})\r?\n"
    rb"MOO-KERNEL USB TASTE PORT ([0-9A-F]{16})\r?\n"
    rb"MOO-KERNEL USB TASTE SLOT ([0-9A-F]{16})\r?\n"
    rb"MOO-KERNEL USB HID [0-9A-F]{16}\r?\n"
    rb"MOO-KERNEL USB TASTE ([0-9A-F]{16})"
)


def read_n(sock, count):
    data = bytearray()
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise EOFError
        data.extend(chunk)
    return bytes(data)


class Keyboard:
    def __init__(self, sock, speed, product):
        self.sock = sock
        self.speed = speed
        self.product = product
        self.lock = threading.Lock()
        self.streaming = threading.Event()
        self.error = None
        self.configuration = 0
        self.device = bytes.fromhex("12 01 00 02 00 00 00 08") + struct.pack(
            "<HHHBBBB", 0x1209, product, 0x100, 0, 0, 0, 1
        )
        self.config = bytes([
            9, 2, 34, 0, 1, 1, 0, 0x80, 10,
            9, 4, 0, 0, 1, 3, 1, 1, 0,
            9, 0x21, 0x11, 0x01, 0, 1, 0x22, len(REPORT), 0,
            7, 5, PORT, 3, 8, 0, 10,
        ])

    def send(self, kind, ident, data):
        with self.lock:
            self.sock.sendall(FRAME.pack(kind, len(data), ident) + data)

    def announce(self):
        name = b"moo boot keyboard"
        hello = name.ljust(64, b"\0") + struct.pack("<I", CAPS)
        self.sock.sendall(HELLO.pack(0, len(hello), 0) + hello)
        kind, count, _ = HELLO.unpack(read_n(self.sock, HELLO.size))
        assert kind == 0 and 68 <= count <= 4096
        peer = read_n(self.sock, count)
        assert struct.unpack_from("<I", peer, 64)[0] & CAPS == CAPS

        info = bytearray(4 + 4 * 32)
        struct.pack_into("<I", info, 0, 1)
        info[36], info[68], info[100] = 3, 1, 1
        self.send(4, 0, info)

        types = bytearray([255] * 32)
        types[0], types[16], types[17] = 0, 0, 3
        intervals = bytearray(32)
        intervals[17] = 10
        packets = [0] * 32
        packets[0] = packets[16] = packets[17] = 8
        ep_info = types + intervals + bytes(32) + struct.pack("<32H", *packets)
        self.send(5, 0, ep_info)
        self.send(1, 0, struct.pack("<BBBBHHH", self.speed, 0, 0, 0, 0x1209, self.product, 0x100))

    def control(self, ident, packet):
        ep, request, direction, _, value, index, count = CONTROL.unpack_from(packet)
        status, data = 0, b""
        if request == 6 and direction & 128:
            data = {0x100: self.device, 0x200: self.config, 0x2100: self.config[18:27],
                    0x2200: REPORT, 0x300: b"\x04\x03\x09\x04"}.get(value)
            if data is None:
                status, data = 4, b""
        elif direction == 0x21 and request in (9, 10, 11) and index == 0:
            pass  # SET_REPORT / SET_IDLE / SET_PROTOCOL.
        elif request == 0 and direction & 128:
            data = b"\0\0"  # GET_STATUS.
        else:
            status = 4  # Unsupported request.
        data = data[:count]
        self.send(100, ident, CONTROL.pack(ep, request, direction, status, value, index, len(data)) + data)

    def serve(self):
        try:
            self.announce()
            while True:
                kind, count, ident = FRAME.unpack(read_n(self.sock, FRAME.size))
                assert count <= 65536
                data = read_n(self.sock, count)
                if kind == 3:  # USB reset.
                    self.streaming.clear()
                    self.configuration = 0
                elif kind in (6, 7):
                    if kind == 6:
                        self.configuration = data[0]
                    self.send(8, ident, bytes((0, self.configuration)))
                elif kind in (9, 10):
                    self.send(11, ident, bytes((0, 0, 0)))
                elif kind == 15:
                    assert data[0] == PORT
                    self.send(17, ident, bytes((0, PORT)))
                    self.streaming.set()
                elif kind == 16:
                    self.streaming.clear()
                elif kind == 100:
                    self.control(ident, data)
                elif kind not in (21, 24):
                    raise AssertionError(f"Unexpected USB redirection packet {kind}")
        except (EOFError, ConnectionResetError, BrokenPipeError):
            return
        except Exception as exc:
            self.error = exc

    def key(self, usage):
        assert self.streaming.is_set(), f"No interrupt-IN stream for {self.product:04x}"
        self.send(103, 0, struct.pack("<BBH8B", PORT, 0, 8, 0, 0, usage, 0, 0, 0, 0, 0))


def main():
    opt = argparse.ArgumentParser(description=__doc__)
    opt.add_argument("--kern-gate", type=pathlib.Path, required=True)
    opt.add_argument("--out", type=pathlib.Path, required=True)
    opt.add_argument("--ovmf-code", type=pathlib.Path, required=True)
    opt.add_argument("--ovmf-vars", type=pathlib.Path, required=True)
    args = opt.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    image = out / "uefi.img"
    elf = out / "KERN.ELF"
    boot = args.kern_gate / "uefi.img"
    assert boot.is_file() and not boot.is_symlink()
    shutil.copyfile(boot, image)
    blob = bytearray((args.kern_gate / "KERN.ELF").read_bytes())
    marker = struct.pack("<3Q", 0x4C4D4D41534F4F4D, 0, 0)
    assert blob.count(marker) == 1
    struct.pack_into("<Q", blob, blob.index(marker) + 8, 1)
    elf.write_bytes(blob)
    subprocess.run(["mcopy", "-o", "-i", f"{image}@@1048576", elf, "::/EFI/MOO/KERN.ELF"], check=True)
    vars_image = out / "vars.fd"
    shutil.copyfile(args.ovmf_vars, vars_image)
    serial = out / "serial.log"
    serial.touch()

    command = [
        "qemu-system-x86_64", "-enable-kvm", "-cpu", "host",
        "-drive", f"if=pflash,unit=0,format=raw,readonly=on,file={args.ovmf_code}",
        "-drive", f"if=pflash,unit=1,format=raw,file={vars_image}",
        "-drive", f"format=raw,snapshot=on,file={image}",
        "-netdev", "user,id=n0", "-device", "e1000,netdev=n0,romfile=",
        "-object", f"filter-dump,id=wire,netdev=n0,file={out / 'net.pcap'}",
        "-display", "none", "-monitor", "none", "-serial", f"file:{serial}",
        "-m", "256M", "-no-reboot",
        "-device", "qemu-xhci,id=xh0,addr=0xd", "-device", "qemu-xhci,id=xh1,addr=0xe",
    ]
    keyboards, peers = [], []
    for index, (speed, bus, port) in enumerate(((0, "xh0", 1), (1, "xh0", 2), (1, "xh1", 1))):
        sock, peer = socket.socketpair()
        keyboards.append(Keyboard(sock, speed, 0xB001 + index))
        peers.append(peer)
        command += [
            "-chardev", f"socket,id=usb{index},fd={peer.fileno()}",
            "-device", f"usb-redir,chardev=usb{index},bus={bus}.0,port={port}",
        ]
    for dev in keyboards:
        threading.Thread(target=dev.serve, daemon=True).start()

    with (out / "qemu.log").open("wb") as qemu_log:
        qemu = subprocess.Popen(command, stdout=qemu_log, stderr=qemu_log, pass_fds=[p.fileno() for p in peers])
        for peer in peers:
            peer.close()

        def until(predicate, stage):
            deadline = time.monotonic() + 25
            while time.monotonic() < deadline:
                for dev in keyboards:
                    if dev.error is not None:
                        raise dev.error
                assert qemu.poll() is None, (stage, qemu.returncode)
                log = serial.read_bytes()
                if predicate(log):
                    return log
                time.sleep(0.02)
            raise AssertionError(f"Missing result at {stage}. See {out}")

        try:
            log = until(lambda blob: b"MOO-KERNEL SAMMEL ENDE " in blob
                        and all(d.streaming.is_set() for d in keyboards), "enumeration")
            assert log.count(b"MOO-KERNEL USB HID BEREIT SPEED 0000000000000002") == 1
            assert log.count(b"MOO-KERNEL USB HID BEREIT SPEED 0000000000000001") == 2
            size = f"MOO-KERNEL USB HID REPORT BYTES {len(REPORT):016X}".encode()
            assert log.count(size) == 3
            assert log.count(b"MOO-KERNEL USB HID INTERVALL 0000000000000006") == 3
            # Press the same key on all three. Do not release it on the earlier device.
            for i, keyboard in enumerate(keyboards, 1):
                keyboard.key(4)
                until(lambda blob: len(SRC.findall(blob)) == i, f"key {i}")
            log = until(lambda blob: b"MOO-KERNEL OK" in blob, "kernel complete")
            keys = [tuple(int(value, 16) for value in key) for key in SRC.findall(log)]
            assert len(keys) == 3 and [row[3] for row in keys] == [97] * 3, keys
            assert [row[0] for row in keys] == [0x68, 0x68, 0x70], keys
            assert keys[0][1] != keys[1][1] and keys[0][2] != keys[1][2], keys
            assert b"MOO-KERNEL USB ANGEHALTEN CODE " not in log
            wire = (out / "net.pcap").read_bytes()
            assert wire.count(size) == 3
            for bdf, port, slot, char in keys:
                print(f"BDF={bdf:04X} Port={port} Slot={slot} ASCII={char:02X}")
            print("PASS xHCI Low-/Full-Speed, 3 HID-Reports bei gehaltenen Tasten, 2 Controller")
        finally:
            if qemu.poll() is None:
                qemu.terminate()
                qemu.wait(timeout=10)
            for dev in keyboards:
                dev.sock.close()


if __name__ == "__main__":
    main()
