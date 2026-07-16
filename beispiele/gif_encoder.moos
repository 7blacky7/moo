# ============================================================
# moo GIF89a-Encoder mit LZW und Animation
#
# Kompilieren: moo-compiler compile gif_encoder.moo -o gif_encoder
# Starten:     ./gif_encoder
#
# Erzeugt:
#   /tmp/verlauf.gif  256x256 statisch, Farbverlauf
#   /tmp/life.gif     64x64 Game of Life, 50 Frames, 100 ms Delay
# ============================================================

# ============================================================
# Bit-Stream Writer (LSB first)
# ============================================================
# GIF packt LZW-Codes bitweise ins Byte, niedrigste Bits zuerst.
# Wir akkumulieren in (akk, bits) und pushen fertige Bytes in out.

funktion bs_neu():
    setze b auf {}
    b["out"] = []
    b["akk"] = 0
    b["bits"] = 0
    gib_zurück b

funktion bs_push_code(b, code, code_size):
    setze akk auf b["akk"] + code * power2(b["bits"])
    setze bits auf b["bits"] + code_size
    solange bits >= 8:
        b["out"].hinzufügen(akk % 256)
        setze akk auf boden(akk / 256)
        setze bits auf bits - 8
    b["akk"] = akk
    b["bits"] = bits

funktion bs_flush(b):
    wenn b["bits"] > 0:
        b["out"].hinzufügen(b["akk"] % 256)
        b["akk"] = 0
        b["bits"] = 0

# Cached Potenzen
setze POW2 auf [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536]
funktion power2(n):
    gib_zurück POW2[n]

# ============================================================
# LZW-Encoder (fuer Bilddaten mit 8-bit color table)
# ============================================================
# Initial: clear=256, eoi=257, naechster freier Code = 258
# Dictionary als Map "prefix:byte" → code

funktion lzw_encode(pixels):
    setze min_code_size auf 8
    setze clear_code auf 256
    setze eoi_code auf 257

    setze bs auf bs_neu()
    setze code_size auf 9
    # Initial CLEAR
    bs_push_code(bs, clear_code, code_size)

    setze dict auf {}
    setze next_code auf 258

    setze n auf pixels.länge()
    wenn n == 0:
        bs_push_code(bs, eoi_code, code_size)
        bs_flush(bs)
        gib_zurück [min_code_size, bs["out"]]

    setze prefix auf pixels[0]
    setze i auf 1
    solange i < n:
        setze p auf pixels[i]
        setze schluessel auf text(prefix) + ":" + text(p)
        wenn dict.enthält(schluessel):
            setze prefix auf dict[schluessel]
        sonst:
            bs_push_code(bs, prefix, code_size)
            dict[schluessel] = next_code
            setze next_code auf next_code + 1
            wenn next_code == power2(code_size) + 1:
                wenn code_size < 12:
                    setze code_size auf code_size + 1
            wenn next_code >= 4096:
                bs_push_code(bs, clear_code, code_size)
                setze dict auf {}
                setze next_code auf 258
                setze code_size auf 9
            setze prefix auf p
        setze i auf i + 1

    bs_push_code(bs, prefix, code_size)
    bs_push_code(bs, eoi_code, code_size)
    bs_flush(bs)
    gib_zurück [min_code_size, bs["out"]]

# Packt die LZW-Byte-Stream in GIF Sub-Blocks (max 255 pro Block) + 0x00
funktion zu_sub_blocks(raw):
    setze out auf []
    setze i auf 0
    setze n auf raw.länge()
    solange i < n:
        setze chunk auf n - i
        wenn chunk > 255:
            setze chunk auf 255
        out.hinzufügen(chunk)
        setze k auf 0
        solange k < chunk:
            out.hinzufügen(raw[i + k])
            setze k auf k + 1
        setze i auf i + chunk
    out.hinzufügen(0)
    gib_zurück out

# ============================================================
# Byte-Helper
# ============================================================
funktion push_le16(liste, v):
    liste.hinzufügen(v % 256)
    liste.hinzufügen(boden(v / 256) % 256)

funktion push_ascii(liste, s):
    setze bs auf bytes_zu_liste(s)
    setze i auf 0
    solange i < bs.länge():
        liste.hinzufügen(bs[i])
        setze i auf i + 1

# ============================================================
# GIF Struktur-Writer
# ============================================================

funktion gif_header(out, breite, hoehe):
    push_ascii(out, "GIF89a")
    # Logical Screen Descriptor
    push_le16(out, breite)
    push_le16(out, hoehe)
    # packed: GCT=1, color_res=7, sort=0, size=7 (256 Farben)
    out.hinzufügen(0xF7)
    out.hinzufügen(0)  # bg color index
    out.hinzufügen(0)  # pixel aspect ratio

funktion gif_color_table(out, palette):
    # palette = Liste von 256 * 3 bytes
    setze i auf 0
    solange i < palette.länge():
        out.hinzufügen(palette[i])
        setze i auf i + 1

funktion gif_netscape_loop(out):
    # 0x21 0xFF 0x0B "NETSCAPE2.0" 0x03 0x01 loop_count(le16) 0x00
    out.hinzufügen(0x21)
    out.hinzufügen(0xFF)
    out.hinzufügen(0x0B)
    push_ascii(out, "NETSCAPE2.0")
    out.hinzufügen(0x03)
    out.hinzufügen(0x01)
    push_le16(out, 0)  # loop forever
    out.hinzufügen(0x00)

funktion gif_gce(out, delay_centis):
    # Graphics Control Extension
    out.hinzufügen(0x21)
    out.hinzufügen(0xF9)
    out.hinzufügen(0x04)  # block size
    out.hinzufügen(0x00)  # packed: disposal=0, no user input, no transparent
    push_le16(out, delay_centis)
    out.hinzufügen(0x00)  # transparent color index
    out.hinzufügen(0x00)  # terminator

funktion gif_image_descriptor(out, left, top, w, h):
    out.hinzufügen(0x2C)
    push_le16(out, left)
    push_le16(out, top)
    push_le16(out, w)
    push_le16(out, h)
    out.hinzufügen(0x00)  # packed: no local table, not interlaced

funktion gif_frame(out, breite, hoehe, pixels, delay_centis):
    gif_gce(out, delay_centis)
    gif_image_descriptor(out, 0, 0, breite, hoehe)
    setze lzw auf lzw_encode(pixels)
    out.hinzufügen(lzw[0])  # min code size
    setze sub auf zu_sub_blocks(lzw[1])
    setze i auf 0
    solange i < sub.länge():
        out.hinzufügen(sub[i])
        setze i auf i + 1

funktion gif_trailer(out):
    out.hinzufügen(0x3B)

# ============================================================
# Palette: 256 RGB-Eintraege
# ============================================================
funktion graustufen_palette():
    setze p auf []
    setze i auf 0
    solange i < 256:
        p.hinzufügen(i)
        p.hinzufügen(i)
        p.hinzufügen(i)
        setze i auf i + 1
    gib_zurück p

# Verlauf: Rot → Gelb → Gruen → Cyan → Blau → Magenta
funktion bunt_palette():
    setze p auf []
    setze i auf 0
    solange i < 256:
        wenn i < 43:
            p.hinzufügen(255)
            p.hinzufügen(boden(i * 6))
            p.hinzufügen(0)
        sonst:
            wenn i < 85:
                p.hinzufügen(255 - boden((i - 43) * 6))
                p.hinzufügen(255)
                p.hinzufügen(0)
            sonst:
                wenn i < 128:
                    p.hinzufügen(0)
                    p.hinzufügen(255)
                    p.hinzufügen(boden((i - 85) * 6))
                sonst:
                    wenn i < 170:
                        p.hinzufügen(0)
                        p.hinzufügen(255 - boden((i - 128) * 6))
                        p.hinzufügen(255)
                    sonst:
                        wenn i < 213:
                            p.hinzufügen(boden((i - 170) * 6))
                            p.hinzufügen(0)
                            p.hinzufügen(255)
                        sonst:
                            p.hinzufügen(255)
                            p.hinzufügen(0)
                            p.hinzufügen(255 - boden((i - 213) * 6))
        setze i auf i + 1
    gib_zurück p

# ============================================================
# Bild 1: 256x256 Farbverlauf (statisches GIF)
# ============================================================
zeige "=== moo GIF-Encoder ==="
zeige ""
zeige "Bild 1: 256x256 Farbverlauf → /tmp/verlauf.gif"

setze pixels auf []
setze y auf 0
solange y < 256:
    setze x auf 0
    solange x < 256:
        pixels.hinzufügen((x + y) % 256)
        setze x auf x + 1
    setze y auf y + 1

setze out auf []
gif_header(out, 256, 256)
gif_color_table(out, bunt_palette())
gif_frame(out, 256, 256, pixels, 0)
gif_trailer(out)
zeige "  GIF-Groesse: " + text(out.länge()) + " bytes"
datei_schreiben_bytes("/tmp/verlauf.gif", out)
zeige "  geschrieben"

# ============================================================
# Bild 2: Game of Life Animation 64x64, 50 Frames
# ============================================================
zeige ""
zeige "Bild 2: Game of Life 64x64 x 50 Frames → /tmp/life.gif"

konstante LW auf 64
konstante LH auf 64
konstante FRAMES auf 50

# Initialzustand: klassisches Gosper-Glider-Gun (Startpattern)
funktion leeres_grid():
    setze g auf []
    setze i auf 0
    solange i < LW * LH:
        g.hinzufügen(0)
        setze i auf i + 1
    gib_zurück g

funktion glider_gun(g):
    # Positionen des Gosper Glider Gun
    setze punkte auf [1, 5, 1, 6, 2, 5, 2, 6, 11, 5, 11, 6, 11, 7, 12, 4, 12, 8, 13, 3, 13, 9, 14, 3, 14, 9, 15, 6, 16, 4, 16, 8, 17, 5, 17, 6, 17, 7, 18, 6, 21, 3, 21, 4, 21, 5, 22, 3, 22, 4, 22, 5, 23, 2, 23, 6, 25, 1, 25, 2, 25, 6, 25, 7, 35, 3, 35, 4, 36, 3, 36, 4]
    setze i auf 0
    solange i < punkte.länge():
        setze x auf punkte[i]
        setze y auf punkte[i + 1]
        wenn x < LW und y < LH:
            g[y * LW + x] = 1
        setze i auf i + 2

funktion life_step(g):
    setze nxt auf leeres_grid()
    setze y auf 0
    solange y < LH:
        setze x auf 0
        solange x < LW:
            setze n auf 0
            setze dy auf -1
            solange dy <= 1:
                setze dx auf -1
                solange dx <= 1:
                    wenn nicht (dx == 0 und dy == 0):
                        setze nx auf x + dx
                        setze ny auf y + dy
                        wenn nx >= 0 und nx < LW und ny >= 0 und ny < LH:
                            setze n auf n + g[ny * LW + nx]
                    setze dx auf dx + 1
                setze dy auf dy + 1
            setze alive auf g[y * LW + x]
            setze neues_alive auf 0
            wenn alive == 1 und (n == 2 oder n == 3):
                setze neues_alive auf 1
            wenn alive == 0 und n == 3:
                setze neues_alive auf 1
            nxt[y * LW + x] = neues_alive
            setze x auf x + 1
        setze y auf y + 1
    gib_zurück nxt

# Life-Palette: 0=schwarz, 1..255=gruen-scale, hier nur 2 Farben
funktion life_palette():
    setze p auf []
    # 0 = schwarz
    p.hinzufügen(0)
    p.hinzufügen(0)
    p.hinzufügen(0)
    # 1 = hellgruen
    p.hinzufügen(40)
    p.hinzufügen(240)
    p.hinzufügen(60)
    # 2..255 = duster
    setze i auf 2
    solange i < 256:
        p.hinzufügen(20)
        p.hinzufügen(20)
        p.hinzufügen(20)
        setze i auf i + 1
    gib_zurück p

setze gif auf []
gif_header(gif, LW, LH)
gif_color_table(gif, life_palette())
gif_netscape_loop(gif)

setze grid auf leeres_grid()
glider_gun(grid)

setze frame_nr auf 0
solange frame_nr < FRAMES:
    zeige "  Frame " + text(frame_nr + 1) + "/" + text(FRAMES)
    gif_frame(gif, LW, LH, grid, 10)
    setze grid auf life_step(grid)
    setze frame_nr auf frame_nr + 1

gif_trailer(gif)
zeige "  GIF-Groesse: " + text(gif.länge()) + " bytes"
datei_schreiben_bytes("/tmp/life.gif", gif)
zeige "  geschrieben"

zeige ""
zeige "=== Fertig ==="
