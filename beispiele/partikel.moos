# ============================================================
# moo Partikel-System — 1000+ Partikel mit mehreren Emittern
#
# Kompilieren: moo-compiler compile partikel.moo -o partikel
# Starten:     ./partikel
#
# Bedienung:
#   Maus-Klick          → Explosion (50 Partikel) an Mausposition
#   F (Taste)           → Fountain-Emitter toggeln
#   S                   → Spiral-Emitter toggeln
#   C                   → Clear
# ============================================================

konstante BREITE auf 1000
konstante HOEHE auf 700
konstante GRAVITATION auf 400.0
konstante LUFT auf 0.995
konstante DT auf 0.016
konstante MAX_PARTIKEL auf 4000
konstante FOUNT_RATE auf 8
konstante SPIRAL_RATE auf 6
konstante EXPL_ANZ auf 50

setze FARBEN auf ["rot", "gruen", "blau", "gelb", "magenta", "orange", "cyan", "weiss", "hellgruen", "hellblau", "rosa", "lila"]

# --- 3x5 Mini-Font ---
setze FONT auf {}
FONT["0"] = [1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["1"] = [0,1,0, 1,1,0, 0,1,0, 0,1,0, 1,1,1]
FONT["2"] = [1,1,1, 0,0,1, 1,1,1, 1,0,0, 1,1,1]
FONT["3"] = [1,1,1, 0,0,1, 1,1,1, 0,0,1, 1,1,1]
FONT["4"] = [1,0,1, 1,0,1, 1,1,1, 0,0,1, 0,0,1]
FONT["5"] = [1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1]
FONT["6"] = [1,1,1, 1,0,0, 1,1,1, 1,0,1, 1,1,1]
FONT["7"] = [1,1,1, 0,0,1, 0,1,0, 0,1,0, 0,1,0]
FONT["8"] = [1,1,1, 1,0,1, 1,1,1, 1,0,1, 1,1,1]
FONT["9"] = [1,1,1, 1,0,1, 1,1,1, 0,0,1, 1,1,1]
FONT[" "] = [0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0]
FONT[":"] = [0,0,0, 0,1,0, 0,0,0, 0,1,0, 0,0,0]
FONT["F"] = [1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,0,0]
FONT["P"] = [1,1,1, 1,0,1, 1,1,1, 1,0,0, 1,0,0]
FONT["S"] = [1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1]
FONT["N"] = [1,0,1, 1,1,1, 1,1,1, 1,0,1, 1,0,1]
FONT["O"] = [1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["U"] = [1,0,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["E"] = [1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,1,1]
FONT["R"] = [1,1,1, 1,0,1, 1,1,0, 1,0,1, 1,0,1]
FONT["T"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 0,1,0]
FONT["A"] = [0,1,0, 1,0,1, 1,1,1, 1,0,1, 1,0,1]
FONT["I"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 1,1,1]
FONT["L"] = [1,0,0, 1,0,0, 1,0,0, 1,0,0, 1,1,1]
FONT["C"] = [1,1,1, 1,0,0, 1,0,0, 1,0,0, 1,1,1]
FONT["="] = [0,0,0, 1,1,1, 0,0,0, 1,1,1, 0,0,0]

funktion zeichne_char(fenster, bits, x, y, px, farbe):
    setze zy auf 0
    solange zy < 5:
        setze zx auf 0
        solange zx < 3:
            wenn bits[zy * 3 + zx] == 1:
                zeichne_rechteck(fenster, x + zx * px, y + zy * px, px, px, farbe)
            setze zx auf zx + 1
        setze zy auf zy + 1

funktion zeichne_text(fenster, s, x, y, px, farbe):
    setze i auf 0
    setze cx auf x
    solange i < länge(s):
        setze ch auf s[i]
        wenn FONT.enthält(ch):
            zeichne_char(fenster, FONT[ch], cx, y, px, farbe)
        setze cx auf cx + 4 * px
        setze i auf i + 1

# --- Math-Naeherungen (falls runtime kein cos/sin hat) ---
funktion mal_cos(x):
    setze pi auf 3.14159265
    solange x > pi:
        setze x auf x - 2 * pi
    solange x < -pi:
        setze x auf x + 2 * pi
    setze x2 auf x * x
    gib_zurück 1 - x2 / 2 + x2 * x2 / 24 - x2 * x2 * x2 / 720

funktion mal_sin(x):
    gib_zurück mal_cos(x - 1.5707963)

# --- Partikel: Struct-of-Arrays, flache Liste mit STRIDE=7
#   [0] x, [1] y, [2] vx, [3] vy, [4] leben, [5] size, [6] farbe_idx
konstante STRIDE auf 7

funktion partikel_anzahl(arr):
    gib_zurück boden(arr.länge() / STRIDE)

funktion push_partikel(arr, x, y, vx, vy, leben, size, farbe_idx):
    wenn partikel_anzahl(arr) >= MAX_PARTIKEL:
        gib_zurück nichts
    arr.hinzufügen(x)
    arr.hinzufügen(y)
    arr.hinzufügen(vx)
    arr.hinzufügen(vy)
    arr.hinzufügen(leben)
    arr.hinzufügen(size)
    arr.hinzufügen(farbe_idx)

# --- Emitter ---
funktion explode(arr, cx, cy, n):
    setze i auf 0
    solange i < n:
        setze winkel auf zufall() * 6.28318
        setze speed auf 80 + zufall() * 260
        setze vx auf speed * mal_cos(winkel)
        setze vy auf speed * mal_sin(winkel)
        setze leben auf 0.8 + zufall() * 1.2
        setze size auf 2 + boden(zufall() * 3)
        setze farbe auf boden(zufall() * 12)
        push_partikel(arr, cx, cy, vx, vy, leben, size, farbe)
        setze i auf i + 1

funktion fountain(arr, cx, cy, n):
    setze i auf 0
    solange i < n:
        setze winkel auf -1.57 + (zufall() - 0.5) * 0.6
        setze speed auf 250 + zufall() * 150
        setze vx auf speed * mal_cos(winkel)
        setze vy auf speed * mal_sin(winkel)
        setze leben auf 1.5 + zufall() * 1.0
        setze size auf 2 + boden(zufall() * 2)
        setze farbe auf 6 + boden(zufall() * 4)
        push_partikel(arr, cx, cy, vx, vy, leben, size, farbe)
        setze i auf i + 1

funktion spiral(arr, cx, cy, n, t):
    setze i auf 0
    solange i < n:
        setze winkel auf t * 3.5 + i * 1.2
        setze speed auf 180
        setze vx auf speed * mal_cos(winkel)
        setze vy auf speed * mal_sin(winkel)
        setze leben auf 1.6
        setze size auf 3
        setze farbe auf (boden(t * 4) + i) % 12
        push_partikel(arr, cx, cy, vx, vy, leben, size, farbe)
        setze i auf i + 1

# --- Update: In-place Kompaktierung der toten Partikel ---
funktion update(arr, dt):
    setze n auf partikel_anzahl(arr)
    setze i auf 0
    setze schreib auf 0
    solange i < n:
        setze off auf i * STRIDE
        setze leben auf arr[off + 4] - dt
        wenn leben > 0:
            setze x auf arr[off + 0] + arr[off + 2] * dt
            setze y auf arr[off + 1] + arr[off + 3] * dt
            setze vx auf arr[off + 2] * LUFT
            setze vy auf (arr[off + 3] + GRAVITATION * dt) * LUFT
            # Boden-Bounce
            wenn y > HOEHE - 5:
                setze y auf HOEHE - 5
                setze vy auf -vy * 0.5
            setze woff auf schreib * STRIDE
            arr[woff] = x
            setze w1 auf woff + 1
            arr[w1] = y
            setze w2 auf woff + 2
            arr[w2] = vx
            setze w3 auf woff + 3
            arr[w3] = vy
            setze w4 auf woff + 4
            arr[w4] = leben
            setze w5 auf woff + 5
            arr[w5] = arr[off + 5]
            setze w6 auf woff + 6
            arr[w6] = arr[off + 6]
            setze schreib auf schreib + 1
        setze i auf i + 1
    # Tote Partikel abschneiden
    setze neue_laenge auf schreib * STRIDE
    solange arr.länge() > neue_laenge:
        arr.pop()

funktion render_partikel(fenster, arr):
    setze n auf partikel_anzahl(arr)
    setze i auf 0
    solange i < n:
        setze off auf i * STRIDE
        setze x auf boden(arr[off + 0])
        setze y auf boden(arr[off + 1])
        setze sz auf boden(arr[off + 5])
        setze fi auf boden(arr[off + 6])
        wenn x >= 0 und x < BREITE und y >= 0 und y < HOEHE:
            zeichne_rechteck(fenster, x - sz, y - sz, sz * 2, sz * 2, FARBEN[fi])
        setze i auf i + 1

# --- Main ---
zeige "=== moo Partikel-System ==="
zeige "1000+ Partikel, 60 FPS Ziel"
zeige "Klick=Explosion, F=Fountain, S=Spiral, C=Clear"

setze arr auf []
setze fenster auf fenster_erstelle("moo Partikel", BREITE, HOEHE)
setze vorher_maus auf falsch
setze fountain_an auf wahr
setze spiral_an auf falsch
setze t auf 0.0

setze fps auf 0
setze frame_count auf 0
setze fps_start auf zeit_ms()

setze vorher_f auf falsch
setze vorher_s auf falsch
setze vorher_c auf falsch

solange fenster_offen(fenster):
    setze mx auf maus_x(fenster)
    setze my auf maus_y(fenster)
    setze jetzt auf maus_gedrückt(fenster)

    wenn jetzt und nicht vorher_maus:
        explode(arr, mx, my, EXPL_ANZ)
    setze vorher_maus auf jetzt

    setze f_jetzt auf taste_gedrückt("f")
    wenn f_jetzt und nicht vorher_f:
        setze fountain_an auf nicht fountain_an
    setze vorher_f auf f_jetzt

    setze s_jetzt auf taste_gedrückt("s")
    wenn s_jetzt und nicht vorher_s:
        setze spiral_an auf nicht spiral_an
    setze vorher_s auf s_jetzt

    setze c_jetzt auf taste_gedrückt("c")
    wenn c_jetzt und nicht vorher_c:
        solange arr.länge() > 0:
            arr.pop()
    setze vorher_c auf c_jetzt

    wenn fountain_an:
        fountain(arr, BREITE / 2, HOEHE - 30, FOUNT_RATE)
    wenn spiral_an:
        spiral(arr, BREITE / 2, HOEHE / 2, SPIRAL_RATE, t)

    update(arr, DT)

    fenster_löschen(fenster, "schwarz")
    render_partikel(fenster, arr)

    zeichne_text(fenster, "FPS: " + text(fps), 10, 10, 3, "weiss")
    zeichne_text(fenster, "N: " + text(partikel_anzahl(arr)), 10, 35, 3, "weiss")

    fenster_aktualisieren(fenster)

    setze frame_count auf frame_count + 1
    setze verstrichen auf zeit_ms() - fps_start
    wenn verstrichen >= 1000:
        setze fps auf boden(frame_count * 1000 / verstrichen)
        zeige "FPS=" + text(fps) + " N=" + text(partikel_anzahl(arr))
        setze frame_count auf 0
        setze fps_start auf zeit_ms()

    setze t auf t + DT

zeige "Partikel-System beendet"
