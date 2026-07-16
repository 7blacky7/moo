# ============================================================
# Noise & Mathe-Bibliothek fuer 3D Welt-Ersteller
# Phase 1: PRNG, Hash, Perlin 2D, fBm
#
# HINWEIS: Globale Variablen koennen nicht direkt aus Funktionen
# modifiziert werden. Workaround: Liste als Container fuer
# mutablen globalen State.
# ============================================================

# --- 1. Globaler PRNG-State als Liste (Workaround) ---
# _prng[0] = aktueller State
setze _prng auf [123456789]

# Globaler Noise-Seed (beeinflusst hash_2d)
# _noise_seed[0] = Seed-Offset fuer hash
setze _noise_seed auf [0]

funktion seed_setzen(s):
    _prng[0] = s
    # Seed-Offset fuer Noise aus PRNG ableiten
    setze x auf s
    setze x auf x ^ (x << 13)
    setze x auf x ^ (x >> 7)
    setze x auf x ^ (x << 17)
    _noise_seed[0] = abs(x) & 2147483647

# --- 2. Seedbarer Pseudo-Zufallsgenerator (xorshift64) ---

funktion prng_naechste():
    setze x auf _prng[0]
    setze x auf x ^ (x << 13)
    setze x auf x ^ (x >> 7)
    setze x auf x ^ (x << 17)
    _prng[0] = x
    # Normalisieren auf 0..1
    setze r auf abs(x) / 9007199254740992.0
    # Sicherstellen dass wir im Bereich 0..1 bleiben
    wenn r > 1.0:
        setze r auf r - boden(r)
    gib_zurück r

# --- 3. Hash-Funktion (fuer Perlin, seed-abhaengig) ---
# Verwendet kleine Multiplikatoren + haeufiges Masking
# um Double-Precision-Overflow zu vermeiden.
# Alle Zwischenwerte bleiben unter 2^53.

funktion hash_2d(ix, iy):
    setze seed auf _noise_seed[0]
    setze n auf ((ix + 1) * 374761 + (iy + 1) * 668265 + seed) & 2147483647
    setze n auf (n ^ (n >> 11)) & 2147483647
    setze n auf (n * 45673) & 2147483647
    setze n auf (n ^ (n >> 15)) & 2147483647
    setze n auf (n * 31337) & 2147483647
    setze n auf (n ^ (n >> 13)) & 2147483647
    gib_zurück n

# --- 4. Hilfsfunktionen ---

funktion lerp(a, b, t):
    gib_zurück a + (b - a) * t

funktion fade(t):
    # Perlin Fade-Kurve: 6t^5 - 15t^4 + 10t^3
    gib_zurück t * t * t * (t * (t * 6 - 15) + 10)

funktion klammer(wert, vmin, vmax):
    gib_zurück max(vmin, min(vmax, wert))

# --- 5. Perlin Noise 2D (Value Noise) ---

funktion perlin_2d(x, y):
    setze ix auf boden(x)
    setze iy auf boden(y)
    setze fx auf x - ix
    setze fy auf y - iy
    setze u auf fade(fx)
    setze v auf fade(fy)

    # 4 Eckpunkte-Werte via hash -> normalisiert auf -1..1
    setze a auf (hash_2d(ix,     iy    ) % 1000) / 500.0 - 1.0
    setze b auf (hash_2d(ix + 1, iy    ) % 1000) / 500.0 - 1.0
    setze c auf (hash_2d(ix,     iy + 1) % 1000) / 500.0 - 1.0
    setze d auf (hash_2d(ix + 1, iy + 1) % 1000) / 500.0 - 1.0

    # Bilineare Interpolation
    gib_zurück lerp(lerp(a, b, u), lerp(c, d, u), v)

# --- 6. Fractal Brownian Motion (fBm) ---

funktion fbm(x, y, oktaven):
    setze wert auf 0.0
    setze amplitude auf 1.0
    setze frequenz auf 1.0
    setze max_wert auf 0.0
    setze i auf 0
    solange i < oktaven:
        setze wert auf wert + perlin_2d(x * frequenz, y * frequenz) * amplitude
        setze max_wert auf max_wert + amplitude
        setze amplitude auf amplitude * 0.5
        setze frequenz auf frequenz * 2.0
        setze i auf i + 1
    gib_zurück wert / max_wert

# ============================================================
# Test-Hauptprogramm
# ============================================================

# Test 1: PRNG Determinismus
zeige "=== Test 1: PRNG Determinismus ==="
seed_setzen(42)
zeige "PRNG mit Seed 42:"
setze prng_werte_1 auf []
setze i auf 0
solange i < 6:
    setze w auf prng_naechste()
    zeige text(w)
    prng_werte_1.hinzufügen(w)
    setze i auf i + 1

# Wiederholung mit gleichem Seed -> gleiche Werte
zeige ""
zeige "Wiederholung mit Seed 42 (muss identisch sein):"
seed_setzen(42)
setze prng_ok auf wahr
setze i auf 0
solange i < 6:
    setze w auf prng_naechste()
    zeige text(w)
    wenn w != prng_werte_1[i]:
        setze prng_ok auf falsch
    setze i auf i + 1

wenn prng_ok:
    zeige "Test 1: OK - PRNG ist deterministisch"
sonst:
    zeige "Test 1: FEHLER - PRNG ist NICHT deterministisch!"

# Test 2: Noise-Visualisierung als ASCII
zeige ""
zeige "=== Test 2: Noise Map (30x15) ==="
seed_setzen(100)
setze y auf 0
solange y < 15:
    setze zeile auf ""
    setze x auf 0
    solange x < 30:
        setze n auf fbm(x * 0.15, y * 0.15, 4)
        wenn n > 0.3:
            setze zeile auf zeile + "#"
        sonst wenn n > 0.1:
            setze zeile auf zeile + "+"
        sonst wenn n > -0.1:
            setze zeile auf zeile + "~"
        sonst wenn n > -0.3:
            setze zeile auf zeile + "."
        sonst:
            setze zeile auf zeile + " "
        setze x auf x + 1
    zeige zeile
    setze y auf y + 1

# Test 3: Verschiedene Seeds -> verschiedene Welten
zeige ""
zeige "=== Test 3: Verschiedene Seeds ==="
seed_setzen(1)
setze v1 auf fbm(5.5, 3.5, 4)
zeige "Seed 1, fbm(5.5, 3.5, 4) = " + text(v1)

seed_setzen(2)
setze v2 auf fbm(5.5, 3.5, 4)
zeige "Seed 2, fbm(5.5, 3.5, 4) = " + text(v2)

seed_setzen(999)
setze v3 auf fbm(5.5, 3.5, 4)
zeige "Seed 999, fbm(5.5, 3.5, 4) = " + text(v3)

wenn v1 != v2 und v2 != v3:
    zeige "Test 3: OK - Verschiedene Seeds liefern verschiedene Werte"
sonst:
    zeige "Test 3: FEHLER - Seeds erzeugen identische Werte!"

# Test 4: Wertebereich-Pruefung
zeige ""
zeige "=== Test 4: Wertebereich ==="
seed_setzen(42)
setze min_val auf 999.0
setze max_val auf -999.0
setze i auf 0
solange i < 100:
    setze j auf 0
    solange j < 100:
        setze n auf perlin_2d(i * 0.3, j * 0.3)
        wenn n < min_val:
            setze min_val auf n
        wenn n > max_val:
            setze max_val auf n
        setze j auf j + 1
    setze i auf i + 1
zeige "perlin_2d Min: " + text(min_val)
zeige "perlin_2d Max: " + text(max_val)
wenn min_val >= -1.1 und max_val <= 1.1:
    zeige "Test 4: OK - Werte im erwarteten Bereich"
sonst:
    zeige "Test 4: WARNUNG - Werte ausserhalb [-1.1, 1.1]"

zeige ""
zeige "=== Alle Tests abgeschlossen ==="
