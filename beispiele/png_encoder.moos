# ============================================================
# moo PNG-Encoder — schreibt PNG-Dateien in pure moo
#
# Kompilieren: moo-compiler compile png_encoder.moo -o png_encoder
# Starten:     ./png_encoder
#
# Erzeugt zwei Testbilder:
#   verlauf.png      256x256 RGB-Verlauf (rot/gruen/blau)
#   mandelbrot.png   320x240 Mandelbrot-Fraktal in Graustufen
#
# Implementierung:
#   - PNG Signature + IHDR + IDAT + IEND
#   - Alle Chunks big-endian mit CRC32 (Polynom 0xEDB88320)
#   - IDAT ist zlib mit stored deflate-Bloecken (BTYPE=00)
#     plus Adler32 am Ende
#   - Bild-Daten: pro Zeile ein 0-Filter-Byte + RGB-Bytes
# ============================================================

# --- Helper: Big-Endian Writer ---
funktion push_be32(liste, v):
    liste.hinzufügen(boden(v / 16777216) % 256)
    liste.hinzufügen(boden(v / 65536) % 256)
    liste.hinzufügen(boden(v / 256) % 256)
    liste.hinzufügen(v % 256)

funktion push_le16(liste, v):
    liste.hinzufügen(v % 256)
    liste.hinzufügen(boden(v / 256) % 256)

funktion push_ascii(liste, s):
    setze bs auf bytes_zu_liste(s)
    setze i auf 0
    setze n auf bs.länge()
    solange i < n:
        liste.hinzufügen(bs[i])
        setze i auf i + 1

# --- CRC32 Tabelle (Polynom 0xEDB88320) ---
setze CRC32_TABLE auf []
setze i auf 0
solange i < 256:
    setze c auf i
    setze j auf 0
    solange j < 8:
        wenn c % 2 == 1:
            setze c auf boden(c / 2) ^ 0xEDB88320
        sonst:
            setze c auf boden(c / 2)
        setze j auf j + 1
    CRC32_TABLE.hinzufügen(c)
    setze i auf i + 1

# CRC32 ueber eine Byte-Liste [start..start+len)
funktion crc32_berechne(bs, start, len):
    setze c auf 0xFFFFFFFF
    setze i auf 0
    solange i < len:
        setze idx auf (c ^ bs[start + i]) % 256
        setze c auf CRC32_TABLE[idx] ^ boden(c / 256)
        setze i auf i + 1
    gib_zurück c ^ 0xFFFFFFFF

# --- Adler32 ---
funktion adler32(bs):
    setze a auf 1
    setze b auf 0
    setze i auf 0
    setze n auf bs.länge()
    solange i < n:
        setze a auf (a + bs[i]) % 65521
        setze b auf (b + a) % 65521
        setze i auf i + 1
    gib_zurück b * 65536 + a

# --- ZLIB-Wrapper mit stored deflate-Bloecken ---
# Max-Blockgroesse 65535, letzter Block hat BFINAL=1
konstante MAX_BLOCK auf 65535

funktion zlib_encode(daten):
    setze out auf []
    # 2-byte zlib Header: CMF=0x78, FLG=0x01
    out.hinzufügen(0x78)
    out.hinzufügen(0x01)

    setze n auf daten.länge()
    setze pos auf 0
    solange pos < n:
        setze len auf n - pos
        wenn len > MAX_BLOCK:
            setze len auf MAX_BLOCK
        setze final auf 0
        wenn pos + len == n:
            setze final auf 1
        # 1 byte BFINAL/BTYPE — BTYPE=00 (stored)
        out.hinzufügen(final)
        # LEN und NLEN in little-endian
        push_le16(out, len)
        push_le16(out, 65535 - len)
        # Payload kopieren
        setze i auf 0
        solange i < len:
            out.hinzufügen(daten[pos + i])
            setze i auf i + 1
        setze pos auf pos + len

    # Adler32 am Ende (big-endian!)
    setze ad auf adler32(daten)
    push_be32(out, ad)
    gib_zurück out

# --- PNG Chunk Writer ---
funktion chunk_schreiben(out, typ, data):
    setze data_len auf data.länge()
    push_be32(out, data_len)
    # CRC wird ueber type + data berechnet. Wir bauen einen
    # temporaeren Buffer dafuer.
    setze crcbuf auf []
    push_ascii(crcbuf, typ)
    setze i auf 0
    solange i < data_len:
        crcbuf.hinzufügen(data[i])
        setze i auf i + 1
    # Type + Data in out
    push_ascii(out, typ)
    setze i auf 0
    solange i < data_len:
        out.hinzufügen(data[i])
        setze i auf i + 1
    setze crc auf crc32_berechne(crcbuf, 0, crcbuf.länge())
    push_be32(out, crc)

# --- PNG-Bild schreiben ---
# pixel_liste: flach RGB [r0,g0,b0, r1,g1,b1, ...], laenge = breite*hoehe*3
funktion png_schreiben(pfad, breite, hoehe, pixel_liste):
    zeige "  Baue IHDR..."
    setze out auf []
    # PNG-Signature: 89 50 4E 47 0D 0A 1A 0A
    out.hinzufügen(0x89)
    out.hinzufügen(0x50)
    out.hinzufügen(0x4E)
    out.hinzufügen(0x47)
    out.hinzufügen(0x0D)
    out.hinzufügen(0x0A)
    out.hinzufügen(0x1A)
    out.hinzufügen(0x0A)

    # IHDR (13 bytes)
    setze ihdr auf []
    push_be32(ihdr, breite)
    push_be32(ihdr, hoehe)
    ihdr.hinzufügen(8)   # bit depth
    ihdr.hinzufügen(2)   # color type = RGB
    ihdr.hinzufügen(0)   # compression = deflate
    ihdr.hinzufügen(0)   # filter = none
    ihdr.hinzufügen(0)   # interlace = no
    chunk_schreiben(out, "IHDR", ihdr)

    # Filtered image data: pro Zeile 1 Filter-Byte 0 + RGB-Bytes
    zeige "  Filter-Scanlines..."
    setze gefiltert auf []
    setze y auf 0
    setze pitch auf breite * 3
    solange y < hoehe:
        gefiltert.hinzufügen(0)
        setze x auf 0
        setze row_off auf y * pitch
        solange x < pitch:
            gefiltert.hinzufügen(pixel_liste[row_off + x])
            setze x auf x + 1
        setze y auf y + 1
    zeige "  Gefiltert: " + text(gefiltert.länge()) + " bytes"

    zeige "  zlib-kodieren..."
    setze idat auf zlib_encode(gefiltert)
    zeige "  IDAT: " + text(idat.länge()) + " bytes"

    chunk_schreiben(out, "IDAT", idat)
    chunk_schreiben(out, "IEND", [])

    zeige "  Schreibe " + pfad + " (" + text(out.länge()) + " bytes)..."
    datei_schreiben_bytes(pfad, out)
    zeige "  OK"

# --- Bild 1: Farb-Verlauf 256x256 ---
zeige "=== moo PNG-Encoder ==="
zeige ""
zeige "Bild 1: 256x256 RGB-Verlauf → /tmp/verlauf.png"
setze pixel auf []
setze y auf 0
solange y < 256:
    setze x auf 0
    solange x < 256:
        pixel.hinzufügen(x)                    # R: horizontal
        pixel.hinzufügen(y)                    # G: vertikal
        pixel.hinzufügen(boden((x + y) / 2))   # B: diagonal
        setze x auf x + 1
    setze y auf y + 1
png_schreiben("/tmp/verlauf.png", 256, 256, pixel)

# --- Bild 2: Mandelbrot 320x240 (Graustufen) ---
zeige ""
zeige "Bild 2: 320x240 Mandelbrot → /tmp/mandelbrot.png"
setze mb auf []
konstante MB_B auf 320
konstante MB_H auf 240
konstante MB_ITER auf 60
setze py auf 0
solange py < MB_H:
    setze px auf 0
    solange px < MB_B:
        # c = (x,y) aus Pixel-Koordinaten in komplexer Ebene
        setze cx auf (px - MB_B * 0.65) / (MB_B * 0.3)
        setze cy auf (py - MB_H / 2) / (MB_H * 0.35)
        setze zx auf 0.0
        setze zy auf 0.0
        setze it auf 0
        solange it < MB_ITER:
            setze zx2 auf zx * zx
            setze zy2 auf zy * zy
            wenn zx2 + zy2 > 4:
                setze it auf MB_ITER + 1000
            sonst:
                setze nzx auf zx2 - zy2 + cx
                setze zy auf 2 * zx * zy + cy
                setze zx auf nzx
                setze it auf it + 1
        wenn it > MB_ITER:
            setze it auf it - 1000
        # Graustufe: skaliere it ∈ [0..MB_ITER] auf 0..255
        setze grau auf boden(it * 255 / MB_ITER)
        mb.hinzufügen(grau)
        mb.hinzufügen(grau)
        mb.hinzufügen(grau)
        setze px auf px + 1
    setze py auf py + 1
png_schreiben("/tmp/mandelbrot.png", MB_B, MB_H, mb)

zeige ""
zeige "=== Fertig ==="
