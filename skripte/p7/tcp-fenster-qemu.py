#!/usr/bin/env python3
"""TCP-Timer und Fenster auf dem privaten TAP des QEMU-Smokes pruefen."""
import os
import socket
import struct
import sys
import time

GAST_MAC = bytes.fromhex("525400123459")
# Bewusst unzugewiesene Quell-IP im privaten TAP. Der Linux-TCP-Stack
# soll fuer diese Testverbindung weder ACK noch RST erzeugen.
QUELLE, ZIEL = map(socket.inet_aton, ("192.0.2.9", "192.0.2.2"))
PORT, DIENST = 40001, 2222
SYN, ACK, FIN = 2, 16, 1


def checksum(daten):
    if len(daten) % 2:
        daten += b"\0"
    summe = sum(struct.unpack(f"!{len(daten) // 2}H", daten))
    while summe >> 16:
        summe = (summe & 65535) + (summe >> 16)
    return summe ^ 65535


def seq32(seq):
    return seq & 0xffffffff


def sende(sock, seq, ack, flags, fenster, daten=b""):
    tcp = struct.pack("!HHIIBBHHH", PORT, DIENST, seq32(seq), seq32(ack), 80, flags, fenster, 0, 0)
    pseudo = QUELLE + ZIEL + struct.pack("!BBH", 0, 6, len(tcp) + len(daten))
    tcp = tcp[:16] + struct.pack("!H", checksum(pseudo + tcp + daten)) + tcp[18:]
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(tcp) + len(daten), 0, 0x4000, 64, 6, 0, QUELLE, ZIEL)
    ip = ip[:10] + struct.pack("!H", checksum(ip)) + ip[12:]
    sock.sendall(GAST_MAC + sock.getsockname()[4][:6] + b"\x08\x00" + ip + tcp + daten)


def erwarte(sock, test, flags, ack, seq=None, daten=b""):
    start = time.monotonic()
    while (rest := start + 4 - time.monotonic()) > 0:
        sock.settimeout(rest)
        try:
            rahmen = sock.recv(2048)
        except TimeoutError:
            break
        if len(rahmen) < 54 or rahmen[12:15] != b"\x08\x00\x45" or rahmen[23] != 6:
            continue
        if rahmen[26:34] != ZIEL + QUELLE or rahmen[34:38] != struct.pack("!HH", DIENST, PORT):
            continue
        _, _, rseq, rack, kopf, rflags, fenster, _, _ = struct.unpack_from("!HHIIBBHHH", rahmen, 34)
        ende = 14 + struct.unpack_from("!H", rahmen, 16)[0]
        rdata = rahmen[34 + (kopf >> 4) * 4:ende]
        if rflags & ~8 == flags and rack == seq32(ack) and (seq is None or rseq == seq32(seq)) and rdata == daten:
            return rseq, fenster, time.monotonic() - start
    raise TimeoutError(f"{test}: TCP-Antwort fehlt (ACK {seq32(ack):08x}, {len(daten)} Datenbytes)")


def pruefe(sock):
    # Ueberlauf der Empfangssequenz waehrend des Nutzdatenstroms.
    client = 0xfffffff9
    sende(sock, client - 1, 0, SYN, 0)
    iss, _, _ = erwarte(sock, "SYN-ACK", SYN | ACK, client)
    _, _, zeit = erwarte(sock, "SYN-ACK-Retransmit", SYN | ACK, client, iss)
    assert zeit >= 0.5, zeit
    print(f"SYN-ACK ohne Client-Retransmit wiederholt nach {zeit:.3f} s", flush=True)

    server = seq32(iss + 1)
    daten = b"Fenster und Retransmit!"
    ende = seq32(client + len(daten))
    sende(sock, client, server, ACK, 0)
    sende(sock, client + 5, server, ACK, 0, daten[5:])
    erwarte(sock, "Sequenzluecke", ACK, client, server)
    sende(sock, client, server, ACK, 0, daten)
    erwarte(sock, "Kumulatives ACK", ACK, ende, server)
    erwarte(sock, "Nullfensterprobe", ACK, ende, server - 1)
    sende(sock, ende, server, ACK, 5)
    erwarte(sock, "Fuenf-Byte-Fenster", ACK, ende, server, daten[:5])

    sende(sock, ende, server + 3, ACK, 5)
    _, _, zeit = erwarte(sock, "Teil-ACK-Retransmit", ACK, ende, server + 3, daten[3:5])
    assert zeit >= 0.5, zeit
    sende(sock, ende, server + 5, ACK, 1024)
    erwarte(sock, "Echo-Rest", ACK, ende, server + 5, daten[5:])
    sende(sock, ende, server + len(daten), FIN | ACK, 1024)
    erwarte(sock, "Server-FIN", FIN | ACK, ende + 1, server + len(daten))
    sende(sock, ende + 1, server + len(daten) + 1, ACK, 1024)
    print(f"Nullfensterprobe, 5-Byte-Fenster, Teil-ACK-Retransmit nach {zeit:.3f} s, Sequenzumlauf und FIN belegt")


if __name__ == "__main__":
    if os.environ.get("MOO_UEFI_PING_NS") != "1":
        sys.exit("TCP-Fensterprobe braucht den privaten QEMU-Netznamespace")
    with socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0800)) as sock:
        sock.bind(("moo-ping", 0))
        pruefe(sock)
