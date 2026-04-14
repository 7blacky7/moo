# ============================================================
# fruit_ninja.moo — Obst zerschneiden!
#
# Kompilieren: moo-compiler compile beispiele/fruit_ninja.moo -o beispiele/fruit_ninja
# Starten:     ./beispiele/fruit_ninja
#
# Steuerung: Maus bewegen = Klinge, Maus gedrueckt = Schneiden
# Ziel: Obst treffen, Bomben vermeiden, 3 Leben
# ============================================================

konstante WIN_W auf 480
konstante WIN_H auf 640
konstante MAX_OBJ auf 15
konstante SCHWERKRAFT auf 0.25

# Obst-Typen: 0=Apfel(rot), 1=Orange, 2=Melone(grün), 3=Banane(gelb), 4=Traube(lila), 5=Bombe(schwarz)
funktion obst_farbe(typ):
    wenn typ == 0:
        gib_zurück "#F44336"
    wenn typ == 1:
        gib_zurück "#FF9800"
    wenn typ == 2:
        gib_zurück "#4CAF50"
    wenn typ == 3:
        gib_zurück "#FFEB3B"
    wenn typ == 4:
        gib_zurück "#9C27B0"
    wenn typ == 5:
        gib_zurück "#37474F"
    gib_zurück "#F44336"

funktion obst_farbe2(typ):
    wenn typ == 0:
        gib_zurück "#FFCDD2"
    wenn typ == 1:
        gib_zurück "#FFE0B2"
    wenn typ == 2:
        gib_zurück "#C8E6C9"
    wenn typ == 3:
        gib_zurück "#FFF9C4"
    wenn typ == 4:
        gib_zurück "#E1BEE7"
    wenn typ == 5:
        gib_zurück "#F44336"
    gib_zurück "#FFCDD2"

funktion obst_radius(typ):
    wenn typ == 2:
        gib_zurück 28
    wenn typ == 3:
        gib_zurück 18
    gib_zurück 22

# === PRNG ===
setze fn_seed auf 99999

funktion fn_rand(max_val):
    setze fn_seed auf (fn_seed * 1103515245 + 12345) % 2147483648
    gib_zurück fn_seed % max_val

# === Objekte ===
setze obj_x auf []
setze obj_y auf []
setze obj_vx auf []
setze obj_vy auf []
setze obj_typ auf []
setze obj_aktiv auf []
setze obj_geschnitten auf []
setze obj_count auf 0

funktion spawn_obst():
    wenn obj_count < MAX_OBJ:
        setze ox auf 40 + fn_rand(WIN_W - 80)
        setze oy auf WIN_H + 20
        setze ovx auf (fn_rand(60) - 30) / 10.0
        setze ovy auf -12.0 - fn_rand(50) / 10.0
        setze typ auf fn_rand(100)
        setze obst_typ_val auf 0
        wenn typ < 20:
            setze obst_typ_val auf 0
        sonst:
            wenn typ < 40:
                setze obst_typ_val auf 1
            sonst:
                wenn typ < 55:
                    setze obst_typ_val auf 2
                sonst:
                    wenn typ < 70:
                        setze obst_typ_val auf 3
                    sonst:
                        wenn typ < 85:
                            setze obst_typ_val auf 4
                        sonst:
                            setze obst_typ_val auf 5

        obj_x.hinzufügen(ox * 1.0)
        obj_y.hinzufügen(oy * 1.0)
        obj_vx.hinzufügen(ovx)
        obj_vy.hinzufügen(ovy)
        obj_typ.hinzufügen(obst_typ_val)
        obj_aktiv.hinzufügen(wahr)
        obj_geschnitten.hinzufügen(falsch)
        setze obj_count auf obj_count + 1

# === Schnitt-Effekte ===
setze trail_x auf []
setze trail_y auf []
setze trail_timer auf []
setze trail_count auf 0

funktion add_trail(tx, ty):
    wenn trail_count < 30:
        trail_x.hinzufügen(tx * 1.0)
        trail_y.hinzufügen(ty * 1.0)
        trail_timer.hinzufügen(8)
        setze trail_count auf trail_count + 1

# === Splatter (halbe Fruechte) ===
setze splat_x auf []
setze splat_y auf []
setze splat_vx auf []
setze splat_vy auf []
setze splat_typ auf []
setze splat_timer auf []
setze splat_count auf 0

funktion add_splat(sx_val, sy_val, typ):
    wenn splat_count < 20:
        splat_x.hinzufügen(sx_val)
        splat_y.hinzufügen(sy_val)
        splat_vx.hinzufügen((fn_rand(80) - 40) / 10.0)
        splat_vy.hinzufügen(-3.0 - fn_rand(30) / 10.0)
        splat_typ.hinzufügen(typ)
        splat_timer.hinzufügen(30)
        setze splat_count auf splat_count + 1
        # Zweite Haelfte
        splat_x.hinzufügen(sx_val)
        splat_y.hinzufügen(sy_val)
        splat_vx.hinzufügen((fn_rand(80) - 40) / 10.0)
        splat_vy.hinzufügen(-3.0 - fn_rand(30) / 10.0)
        splat_typ.hinzufügen(typ)
        splat_timer.hinzufügen(30)
        setze splat_count auf splat_count + 1

# === State ===
setze score auf 0
setze leben auf 3
setze combo auf 0
setze max_combo auf 0
setze game_over auf falsch
setze spawn_cd auf 0
setze schwierigkeit auf 60
setze letzte_mx auf 0.0
setze letzte_my auf 0.0

# === Hauptprogramm ===
zeige "=== moo Fruit Ninja ==="
zeige "Maus gedrueckt = Schneiden. Bomben vermeiden!"

setze win auf fenster_erstelle("moo Fruit Ninja", WIN_W, WIN_H)
setze fn_seed auf zeit_ms() % 99991

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn taste_gedrückt("r") und game_over:
        setze score auf 0
        setze leben auf 3
        setze game_over auf falsch
        setze obj_count auf 0
        setze obj_x auf []
        setze obj_y auf []
        setze obj_vx auf []
        setze obj_vy auf []
        setze obj_typ auf []
        setze obj_aktiv auf []
        setze obj_geschnitten auf []
        setze splat_count auf 0
        setze splat_x auf []
        setze splat_y auf []
        setze splat_vx auf []
        setze splat_vy auf []
        setze splat_typ auf []
        setze splat_timer auf []

    wenn game_over == falsch:
        # Spawnen
        setze spawn_cd auf spawn_cd + 1
        wenn spawn_cd >= schwierigkeit:
            setze spawn_cd auf 0
            setze wave auf 1 + fn_rand(3)
            setze wi_idx auf 0
            solange wi_idx < wave:
                spawn_obst()
                setze wi_idx auf wi_idx + 1
            wenn schwierigkeit > 20:
                setze schwierigkeit auf schwierigkeit - 1

        # Maus = Klinge
        setze mx auf maus_x(win) * 1.0
        setze my auf maus_y(win) * 1.0
        setze schneidet auf maus_gedrückt(win)

        wenn schneidet:
            add_trail(mx, my)

        # Objekte bewegen
        setze oi auf 0
        solange oi < obj_count:
            wenn obj_aktiv[oi]:
                obj_x[oi] = obj_x[oi] + obj_vx[oi]
                obj_y[oi] = obj_y[oi] + obj_vy[oi]
                obj_vy[oi] = obj_vy[oi] + SCHWERKRAFT

                # Maus-Schnitt pruefen
                wenn schneidet und obj_geschnitten[oi] == falsch:
                    setze dist_x auf mx - obj_x[oi]
                    setze dist_y auf my - obj_y[oi]
                    setze dist_sq auf dist_x * dist_x + dist_y * dist_y
                    setze rad auf obst_radius(obj_typ[oi])
                    wenn dist_sq < rad * rad * 4:
                        obj_geschnitten[oi] = wahr
                        wenn obj_typ[oi] == 5:
                            # Bombe!
                            setze leben auf leben - 1
                            wenn leben <= 0:
                                setze game_over auf wahr
                        sonst:
                            setze score auf score + 1
                            setze combo auf combo + 1
                            wenn combo > max_combo:
                                setze max_combo auf combo
                            add_splat(obj_x[oi], obj_y[oi], obj_typ[oi])

                # Unter Bildschirm = verpasst
                wenn obj_y[oi] > WIN_H + 50:
                    wenn obj_geschnitten[oi] == falsch und obj_typ[oi] != 5:
                        setze combo auf 0
                    obj_aktiv[oi] = falsch
            setze oi auf oi + 1

        # Splatter bewegen
        setze si auf 0
        solange si < splat_count:
            wenn splat_timer[si] > 0:
                splat_x[si] = splat_x[si] + splat_vx[si]
                splat_y[si] = splat_y[si] + splat_vy[si]
                splat_vy[si] = splat_vy[si] + 0.3
                splat_timer[si] = splat_timer[si] - 1
            setze si auf si + 1

        setze letzte_mx auf mx
        setze letzte_my auf my

    # === Zeichnen ===
    # Hintergrund (Holz-artig)
    zeichne_rechteck(win, 0, 0, WIN_W, WIN_H, "#3E2723")
    zeichne_rechteck(win, 0, 0, WIN_W, WIN_H / 3, "#4E342E")
    zeichne_rechteck(win, 0, WIN_H * 2 / 3, WIN_W, WIN_H / 3, "#5D4037")

    # Splatter
    setze si auf 0
    solange si < splat_count:
        wenn splat_timer[si] > 0:
            setze rad auf 10 + (30 - splat_timer[si])
            zeichne_kreis(win, splat_x[si], splat_y[si], rad / 2, obst_farbe2(splat_typ[si]))
            zeichne_kreis(win, splat_x[si], splat_y[si], rad / 3, obst_farbe(splat_typ[si]))
        setze si auf si + 1

    # Objekte
    setze oi auf 0
    solange oi < obj_count:
        wenn obj_aktiv[oi] und obj_geschnitten[oi] == falsch:
            setze ox auf obj_x[oi]
            setze oy auf obj_y[oi]
            setze typ auf obj_typ[oi]
            setze rad auf obst_radius(typ)
            # Frucht
            zeichne_kreis(win, ox, oy, rad, obst_farbe(typ))
            zeichne_kreis(win, ox - 3, oy - 3, rad - 6, obst_farbe2(typ))
            # Glanz
            zeichne_kreis(win, ox - rad / 3, oy - rad / 3, 4, "#FFFFFF")
            # Stiel (kein Stiel bei Bombe)
            wenn typ != 5:
                zeichne_rechteck(win, ox - 1, oy - rad - 6, 3, 8, "#5D4037")
            sonst:
                # Bombe: Zuender
                zeichne_rechteck(win, ox - 1, oy - rad - 4, 3, 6, "#FFD700")
                zeichne_kreis(win, ox, oy - rad - 6, 4, "#FF5722")
        setze oi auf oi + 1

    # Klingen-Trail
    setze ti auf 0
    solange ti < trail_count:
        wenn trail_timer[ti] > 0:
            setze alpha auf trail_timer[ti]
            zeichne_kreis(win, trail_x[ti], trail_y[ti], alpha, "#FFFFFF")
            trail_timer[ti] = trail_timer[ti] - 1
        setze ti auf ti + 1

    # HUD
    zeichne_rechteck(win, 0, 0, WIN_W, 36, "#1A1A2E")
    # Score
    setze si auf 0
    solange si < score und si < 40:
        zeichne_kreis(win, 12 + si * 8, 18, 3, "#FFD700")
        setze si auf si + 1
    # Leben
    setze li auf 0
    solange li < leben:
        zeichne_kreis(win, WIN_W - 20 - li * 24, 18, 8, "#E91E63")
        setze li auf li + 1
    # Combo
    wenn combo >= 3:
        setze ci auf 0
        solange ci < combo und ci < 10:
            zeichne_kreis(win, WIN_W / 2 - 40 + ci * 8, 18, 3, "#FF5722")
            setze ci auf ci + 1

    # Game Over
    wenn game_over:
        zeichne_rechteck(win, WIN_W / 2 - 100, WIN_H / 2 - 40, 200, 80, "#D32F2F")
        zeichne_rechteck(win, WIN_W / 2 - 98, WIN_H / 2 - 38, 196, 76, "#F44336")
        # X
        zeichne_linie(win, WIN_W / 2 - 15, WIN_H / 2 - 15, WIN_W / 2 + 15, WIN_H / 2 + 15, "#FFFFFF")
        zeichne_linie(win, WIN_W / 2 + 15, WIN_H / 2 - 15, WIN_W / 2 - 15, WIN_H / 2 + 15, "#FFFFFF")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Fruit Ninja beendet. Score: " + text(score) + " Max Combo: " + text(max_combo)
