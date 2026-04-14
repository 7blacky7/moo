#!/usr/bin/env python3
"""Kompletter Test für Mini-Redis — alle Commands."""
import socket
import sys

def cmd(command):
    """Sendet ein Command und gibt die Antwort zurück."""
    s = socket.socket()
    s.settimeout(2)
    try:
        s.connect(('127.0.0.1', 6379))
        s.send(command)
        r = s.recv(1024)
        s.close()
        return r
    except Exception as e:
        return f"ERR: {e}".encode()

tests = [
    # (command, expected pattern)
    (b'PING\r\n', b'+PONG'),
    (b'SET name Anna\r\n', b'+OK'),
    (b'GET name\r\n', b'Anna'),
    (b'SET alter 25\r\n', b'+OK'),
    (b'GET alter\r\n', b'25'),
    (b'EXISTS name\r\n', b':1'),
    (b'EXISTS unknown\r\n', b':0'),
    (b'SET stadt Berlin\r\n', b'+OK'),
    (b'GET stadt\r\n', b'Berlin'),
    (b'DEL stadt\r\n', b':1'),
    (b'EXISTS stadt\r\n', b':0'),
    (b'FLUSHALL\r\n', b'+OK'),
    (b'EXISTS name\r\n', b':0'),
    (b'PING\r\n', b'+PONG'),
]

ok = 0
fail = 0
for command, expected in tests:
    r = cmd(command)
    cmd_str = command.strip().decode()
    if expected in r:
        print(f"✓ {cmd_str:25} => {r!r}")
        ok += 1
    else:
        print(f"✗ {cmd_str:25} => {r!r} (erwartet: {expected!r})")
        fail += 1

print(f"\n{ok}/{ok+fail} Tests bestanden")
sys.exit(0 if fail == 0 else 1)
