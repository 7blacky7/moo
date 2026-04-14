# ============================================================
# sokoban.moo — Kisten-Schiebe-Raetsel
#
# Kompilieren: moo-compiler compile beispiele/sokoban.moo -o beispiele/sokoban
# Starten:     ./beispiele/sokoban
#
# Steuerung:
#   WASD / Pfeiltasten - Bewegen
#   R              - Level neu starten
#   N              - Naechstes Level (wenn geloest)
#   Escape         - Beenden
#
# Regeln:
#   * Schiebe alle Kisten auf die Zielfelder
#   * Du kannst nur EINE Kiste gleichzeitig schieben
#   * Kisten koennen nicht gezogen werden
# ============================================================

konstante TILE auf 48
konstante WIN_W auf 576
konstante WIN_H auf 480

# Tile-Typen im Level-String:
# '#' = Wand, ' ' = Boden, '.' = Ziel, '$' = Kiste, '@' = Spieler
# '*' = Kiste auf Ziel, '+' = Spieler auf Ziel

# === Farben ===
konstante C_WAND auf "#5D4037"
konstante C_BODEN auf "#E8E0D0"
konstante C_ZIEL auf "#FF8A65"
konstante C_KISTE auf "#FFA726"
konstante C_KISTE_OK auf "#66BB6A"
konstante C_SPIELER auf "#42A5F5"
konstante C_BG auf "#3E2723"

# === Levels (als Liste von Strings, jeder String = eine Zeile) ===
funktion lade_levels():
    setze levels auf []

    # Level 1 (einfach)
    setze lv1 auf []
    lv1.hinzufügen("  ####  ")
    lv1.hinzufügen("  #  #  ")
    lv1.hinzufügen("  #$ #  ")
    lv1.hinzufügen("###  ###")
    lv1.hinzufügen("#  $.  #")
    lv1.hinzufügen("# @  . #")
    lv1.hinzufügen("#  #####")
    lv1.hinzufügen("####    ")
    levels.hinzufügen(lv1)

    # Level 2
    setze lv2 auf []
    lv2.hinzufügen("#####   ")
    lv2.hinzufügen("#   #   ")
    lv2.hinzufügen("# $ # ##")
    lv2.hinzufügen("# $ #.  #")
    lv2.hinzufügen("#   . . #")
    lv2.hinzufügen("##$## . #")
    lv2.hinzufügen(" # @  ###")
    lv2.hinzufügen(" ######  ")
    levels.hinzufügen(lv2)

    # Level 3
    setze lv3 auf []
    lv3.hinzufügen("  ##### ")
    lv3.hinzufügen("###   # ")
    lv3.hinzufügen("# $ # ##")
    lv3.hinzufügen("# #  $ #")
    lv3.hinzufügen("# . .# #")
    lv3.hinzufügen("##.# @ #")
    lv3.hinzufügen(" #   ###")
    lv3.hinzufügen(" #####  ")
    levels.hinzufügen(lv3)

    # Level 4
    setze lv4 auf []
    lv4.hinzufügen("########")
    lv4.hinzufügen("#      #")
    lv4.hinzufügen("# $$$  #")
    lv4.hinzufügen("# # #  #")
    lv4.hinzufügen("# @  . #")
    lv4.hinzufügen("#  #.#.#")
    lv4.hinzufügen("#    . #")
    lv4.hinzufügen("########")
    levels.hinzufügen(lv4)

    # Level 5
    setze lv5 auf []
    lv5.hinzufügen(" ###### ")
    lv5.hinzufügen("##    ##")
    lv5.hinzufügen("#  ## .#")
    lv5.hinzufügen("# $  $.#")
    lv5.hinzufügen("#  $@  #")
    lv5.hinzufügen("# . # ##")
    lv5.hinzufügen("##    # ")
    lv5.hinzufügen(" ###### ")
    levels.hinzufügen(lv5)

    gib_zurück levels

# === Level parsen → Spielfeld, Kisten, Ziele, Spieler ===
funktion parse_level(lv):
    setze daten auf {}
    setze waende auf []
    setze kisten auf []
    setze ziele auf []
    setze spieler_x auf 0
    setze spieler_y auf 0
    setze hoehe auf länge(lv)
    setze breite auf 0

    setze y auf 0
    solange y < hoehe:
        setze zeile auf lv[y]
        wenn länge(zeile) > breite:
            setze breite auf länge(zeile)
        setze x auf 0
        solange x < länge(zeile):
            setze ch auf zeile[x]
            wenn ch == "#":
                waende.hinzufügen([x, y])
            wenn ch == "$":
                kisten.hinzufügen([x, y])
            wenn ch == "*":
                kisten.hinzufügen([x, y])
                ziele.hinzufügen([x, y])
            wenn ch == ".":
                ziele.hinzufügen([x, y])
            wenn ch == "+":
                ziele.hinzufügen([x, y])
                setze spieler_x auf x
                setze spieler_y auf y
            wenn ch == "@":
                setze spieler_x auf x
                setze spieler_y auf y
            setze x auf x + 1
        setze y auf y + 1

    daten["waende"] = waende
    daten["kisten"] = kisten
    daten["ziele"] = ziele
    daten["px"] = spieler_x
    daten["py"] = spieler_y
    daten["breite"] = breite
    daten["hoehe"] = hoehe
    daten["zuege"] = 0
    gib_zurück daten

# === Helfer ===
funktion ist_wand(waende, x, y):
    setze i auf 0
    solange i < länge(waende):
        setze w auf waende[i]
        wenn w[0] == x und w[1] == y:
            gib_zurück wahr
        setze i auf i + 1
    gib_zurück falsch

funktion finde_kiste(kisten, x, y):
    setze i auf 0
    solange i < länge(kisten):
        setze k auf kisten[i]
        wenn k[0] == x und k[1] == y:
            gib_zurück i
        setze i auf i + 1
    gib_zurück -1

funktion ist_ziel(ziele, x, y):
    setze i auf 0
    solange i < länge(ziele):
        setze z auf ziele[i]
        wenn z[0] == x und z[1] == y:
            gib_zurück wahr
        setze i auf i + 1
    gib_zurück falsch

funktion alle_geloest(kisten, ziele):
    setze i auf 0
    solange i < länge(ziele):
        setze z auf ziele[i]
        wenn finde_kiste(kisten, z[0], z[1]) == -1:
            gib_zurück falsch
        setze i auf i + 1
    gib_zurück wahr

# === Zeichnen ===
funktion zeichne_feld(win, daten, offset_x, offset_y):
    setze waende auf daten["waende"]
    setze kisten auf daten["kisten"]
    setze ziele auf daten["ziele"]
    setze player_x auf daten["px"]
    setze player_y auf daten["py"]

    # Ziele zuerst (unter Kisten)
    setze i auf 0
    solange i < länge(ziele):
        setze z auf ziele[i]
        setze dx auf offset_x + z[0] * TILE
        setze dy auf offset_y + z[1] * TILE
        zeichne_rechteck(win, dx, dy, TILE, TILE, C_BODEN)
        zeichne_kreis(win, dx + TILE / 2, dy + TILE / 2, TILE / 4, C_ZIEL)
        setze i auf i + 1

    # Boden (alles was nicht Wand ist)
    setze y auf 0
    solange y < daten["hoehe"]:
        setze x auf 0
        solange x < daten["breite"]:
            setze dx auf offset_x + x * TILE
            setze dy auf offset_y + y * TILE
            wenn ist_wand(waende, x, y) == falsch:
                wenn ist_ziel(ziele, x, y) == falsch:
                    zeichne_rechteck(win, dx, dy, TILE, TILE, C_BODEN)
            setze x auf x + 1
        setze y auf y + 1

    # Waende
    setze i auf 0
    solange i < länge(waende):
        setze w auf waende[i]
        setze dx auf offset_x + w[0] * TILE
        setze dy auf offset_y + w[1] * TILE
        zeichne_rechteck(win, dx, dy, TILE, TILE, C_WAND)
        # Wand-Detail
        zeichne_rechteck(win, dx + 2, dy + 2, TILE - 4, TILE - 4, "#795548")
        zeichne_linie(win, dx + 4, dy + TILE / 2, dx + TILE - 4, dy + TILE / 2, "#4E342E")
        setze i auf i + 1

    # Ziel-Markierungen nochmal (fuer Sichtbarkeit)
    setze i auf 0
    solange i < länge(ziele):
        setze z auf ziele[i]
        setze dx auf offset_x + z[0] * TILE
        setze dy auf offset_y + z[1] * TILE
        zeichne_kreis(win, dx + TILE / 2, dy + TILE / 2, 6, C_ZIEL)
        setze i auf i + 1

    # Kisten
    setze i auf 0
    solange i < länge(kisten):
        setze k auf kisten[i]
        setze dx auf offset_x + k[0] * TILE
        setze dy auf offset_y + k[1] * TILE
        setze farbe auf C_KISTE
        wenn ist_ziel(ziele, k[0], k[1]):
            setze farbe auf C_KISTE_OK
        zeichne_rechteck(win, dx + 4, dy + 4, TILE - 8, TILE - 8, farbe)
        # Kisten-X
        zeichne_linie(win, dx + 10, dy + 10, dx + TILE - 10, dy + TILE - 10, "#E65100")
        zeichne_linie(win, dx + TILE - 10, dy + 10, dx + 10, dy + TILE - 10, "#E65100")
        setze i auf i + 1

    # Spieler
    setze dx auf offset_x + player_x * TILE
    setze dy auf offset_y + player_y * TILE
    zeichne_kreis(win, dx + TILE / 2, dy + TILE / 2, TILE / 2 - 4, C_SPIELER)
    # Augen
    zeichne_kreis(win, dx + TILE / 3, dy + TILE / 3, 4, "#FFFFFF")
    zeichne_kreis(win, dx + TILE * 2 / 3, dy + TILE / 3, 4, "#FFFFFF")
    zeichne_kreis(win, dx + TILE / 3, dy + TILE / 3, 2, "#1A237E")
    zeichne_kreis(win, dx + TILE * 2 / 3, dy + TILE / 3, 2, "#1A237E")

# === HUD ===
funktion zeichne_hud(win, level_nr, zuege, geloest):
    zeichne_rechteck(win, 0, WIN_H - 36, WIN_W, 36, "#1A1A2E")
    # Level-Nummer als Kreise
    setze i auf 0
    solange i <= level_nr:
        setze farbe auf "#FFD700"
        wenn i == level_nr:
            setze farbe auf "#42A5F5"
        zeichne_kreis(win, 16 + i * 24, WIN_H - 18, 8, farbe)
        setze i auf i + 1
    # Zuege als Punkte (max 30 sichtbar)
    setze z auf 0
    solange z < zuege und z < 30:
        zeichne_kreis(win, WIN_W - 12 - z * 8, WIN_H - 18, 2, "#90A4AE")
        setze z auf z + 1
    wenn geloest:
        zeichne_rechteck(win, WIN_W / 2 - 40, WIN_H - 32, 80, 24, "#66BB6A")

# === Hauptprogramm ===
zeige "=== moo Sokoban ==="
zeige "WASD=Bewegen, R=Restart, N=Naechstes Level, Escape=Beenden"

setze levels auf lade_levels()
setze level_nr auf 0
setze daten auf parse_level(levels[level_nr])
setze geloest auf falsch
setze taste_cooldown auf 0

setze win auf fenster_erstelle("moo Sokoban", WIN_W, WIN_H)

# Offset fuer Zentrierung
setze off_x auf (WIN_W - daten["breite"] * TILE) / 2
setze off_y auf (WIN_H - 36 - daten["hoehe"] * TILE) / 2

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # Taste mit Cooldown (sonst zu schnell)
    wenn taste_cooldown > 0:
        setze taste_cooldown auf taste_cooldown - 1

    setze dx auf 0
    setze dy auf 0
    wenn taste_cooldown == 0:
        wenn taste_gedrückt("w") oder taste_gedrückt("oben"):
            setze dy auf -1
            setze taste_cooldown auf 8
        wenn taste_gedrückt("s") oder taste_gedrückt("unten"):
            setze dy auf 1
            setze taste_cooldown auf 8
        wenn taste_gedrückt("a") oder taste_gedrückt("links"):
            setze dx auf -1
            setze taste_cooldown auf 8
        wenn taste_gedrückt("d") oder taste_gedrückt("rechts"):
            setze dx auf 1
            setze taste_cooldown auf 8

    # Restart
    wenn taste_gedrückt("r") und taste_cooldown == 0:
        setze daten auf parse_level(levels[level_nr])
        setze geloest auf falsch
        setze off_x auf (WIN_W - daten["breite"] * TILE) / 2
        setze off_y auf (WIN_H - 36 - daten["hoehe"] * TILE) / 2
        setze taste_cooldown auf 15

    # Naechstes Level
    wenn taste_gedrückt("n") und geloest und taste_cooldown == 0:
        wenn level_nr < länge(levels) - 1:
            setze level_nr auf level_nr + 1
            setze daten auf parse_level(levels[level_nr])
            setze geloest auf falsch
            setze off_x auf (WIN_W - daten["breite"] * TILE) / 2
            setze off_y auf (WIN_H - 36 - daten["hoehe"] * TILE) / 2
            setze taste_cooldown auf 15

    # Bewegung
    wenn (dx != 0 oder dy != 0) und geloest == falsch:
        setze nx auf daten["px"] + dx
        setze ny auf daten["py"] + dy
        setze waende auf daten["waende"]
        setze kisten auf daten["kisten"]

        wenn ist_wand(waende, nx, ny) == falsch:
            setze ki auf finde_kiste(kisten, nx, ny)
            wenn ki >= 0:
                # Kiste schieben
                setze kx auf nx + dx
                setze ky auf ny + dy
                wenn ist_wand(waende, kx, ky) == falsch und finde_kiste(kisten, kx, ky) == -1:
                    kisten[ki] = [kx, ky]
                    daten["px"] = nx
                    daten["py"] = ny
                    daten["zuege"] = daten["zuege"] + 1
            sonst:
                # Freies Feld
                daten["px"] = nx
                daten["py"] = ny
                daten["zuege"] = daten["zuege"] + 1

        # Geloest?
        wenn alle_geloest(kisten, daten["ziele"]):
            setze geloest auf wahr
            zeige "Level " + text(level_nr + 1) + " geloest in " + text(daten["zuege"]) + " Zuegen!"

    # === Zeichnen ===
    fenster_löschen(win, C_BG)
    zeichne_feld(win, daten, off_x, off_y)
    zeichne_hud(win, level_nr, daten["zuege"], geloest)
    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Sokoban beendet."
