#!/usr/bin/env python3
"""Zwei aufeinanderfolgende Echo-Streams vom Linux-Host zum Moo-Kern."""
import hashlib
import random
import select
import socket
import sys
import time


def pruefe(ziel, quelle, groesse, frist):
    daten = random.Random(groesse).randbytes(groesse)
    start = time.monotonic()
    gesendet = gelesen = 0
    with socket.create_connection((ziel, 2222), timeout=4, source_address=(quelle, 0)) as sock:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.setblocking(False)
        print(f"TCP {sock.getsockname()} -> {sock.getpeername()}, {groesse} Bytes", flush=True)
        while True:
            rest = start + frist - time.monotonic()
            if rest <= 0:
                raise TimeoutError(f"Echo-Frist: {gesendet} Bytes gesendet, {gelesen} gelesen")
            lesen, schreiben, _ = select.select(
                [sock], [sock] if gesendet < groesse else [], [], rest
            )
            if schreiben:
                gesendet += sock.send(daten[gesendet:gesendet + 65536])
                if gesendet == groesse:
                    sock.shutdown(socket.SHUT_WR)
            if lesen:
                antwort = sock.recv(65536)
                if not antwort:
                    if gelesen != groesse or gesendet != groesse:
                        raise ValueError(f"Vorzeitiges EOF: {gesendet} Bytes gesendet, {gelesen} gelesen")
                    break
                if antwort != daten[gelesen:gelesen + len(antwort)]:
                    raise ValueError(f"Echo-Daten weichen ab bei Offset {gelesen}")
                gelesen += len(antwort)
                # Der Testkern wartet je Segment auf ein ACK.
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_QUICKACK, 1)
    dauer = time.monotonic() - start
    print(f"TCP/2222 {gelesen} Bytes bytegleich, EOF, SHA256 {hashlib.sha256(daten).hexdigest()}, {dauer:.3f} s")


if __name__ == "__main__":
    ziel, quelle = sys.argv[1:]
    pruefe(ziel, quelle, 1048576, 60)
    pruefe(ziel, quelle, 65536, 15)
