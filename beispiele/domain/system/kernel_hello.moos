# Minimales "OS" — schreibt direkt auf den VGA-Text-Buffer
# Kompilieren: moo-compiler compile kernel_hello.moo --linker-script kernel.ld --entry _start
#
# VGA Text-Buffer: 0xB8000
# Format pro Zeichen: 2 Bytes — [ASCII-Zeichen] [Attribut-Byte]
# Attribut 0x07 = hellgrau auf schwarz

unsicher:
    # H
    speicher_schreiben(0xB8000, 0x0748, 2)
    # e
    speicher_schreiben(0xB8002, 0x0765, 2)
    # l
    speicher_schreiben(0xB8004, 0x076C, 2)
    # l
    speicher_schreiben(0xB8006, 0x076C, 2)
    # o
    speicher_schreiben(0xB8008, 0x076F, 2)
    # (Leerzeichen)
    speicher_schreiben(0xB800A, 0x0720, 2)
    # m
    speicher_schreiben(0xB800C, 0x076D, 2)
    # o
    speicher_schreiben(0xB800E, 0x076F, 2)
    # o
    speicher_schreiben(0xB8010, 0x076F, 2)
    # !
    speicher_schreiben(0xB8012, 0x0721, 2)
