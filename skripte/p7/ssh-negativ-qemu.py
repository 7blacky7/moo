#!/usr/bin/env python3
"""SSH-Transport-Grenzfaelle am privaten TAP der QEMU-Pruefung."""
import os
import socket
import struct
import sys

HOST, GAST, PORT = "192.0.2.1", "192.0.2.2", 2223
KENNUNG = b"SSH-2.0-MooQemuProbe_1\r\n"


def string(daten):
    return struct.pack("!I", len(daten)) + daten


def paket(nutzlast):
    pad = 8 - ((5 + len(nutzlast)) % 8)
    if pad < 4:
        pad += 8
    return struct.pack("!IB", 1 + len(nutzlast) + pad, pad) + nutzlast + bytes(pad)


def kexinit_ohne_strict():
    listen = (
        b"curve25519-sha256", b"ssh-ed25519",
        b"chacha20-poly1305@openssh.com", b"chacha20-poly1305@openssh.com",
        b"hmac-sha2-256", b"hmac-sha2-256", b"none", b"none", b"", b"",
    )
    return paket(b"\x14" + bytes(16) + b"".join(map(string, listen)) + bytes(5))


def erwarte_abbruch(name, testdaten):
    with socket.create_connection((GAST, PORT), timeout=4, source_address=(HOST, 0)) as sock:
        sock.sendall(KENNUNG + testdaten)
        antwort = bytearray()
        while block := sock.recv(1024):
            antwort.extend(block)
            if len(antwort) > 1024:
                raise ValueError(f"{name}: zu viele Antwortbytes")
    ident, sep, kexinit = antwort.partition(b"\r\n")
    if not sep or not ident.startswith(b"SSH-2.0-") or len(kexinit) < 6:
        raise ValueError(f"{name}: Identifikation und Server-KEXINIT fehlen")
    n = struct.unpack_from("!I", kexinit)[0]
    if 4 + n != len(kexinit) or kexinit[5] != 20:
        raise ValueError(f"{name}: nach Server-KEXINIT kamen weitere Nachrichten")
    print(f"SSH-Negativprobe {name}: TCP-Abbruch vor NEWKEYS")


if __name__ == "__main__":
    if os.environ.get("MOO_UEFI_PING_NS") != "1":
        sys.exit("SSH-Negativprobe braucht den privaten QEMU-Netznamespace")
    erwarte_abbruch("fehlendes Strict KEX", kexinit_ohne_strict())
    erwarte_abbruch("reservierter Nachrichtentyp", paket(b"\xff"))
    erwarte_abbruch("Paketlaenge ausserhalb Grenze", struct.pack("!I", 35004))
    erwarte_abbruch("ungueltiges Padding", struct.pack("!IBB", 12, 3, 20) + bytes(10))
