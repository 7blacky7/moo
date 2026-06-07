# ============================================================
# voxel_sandbox_selftest.moo — Referenz-Selftest Voxel (Plan-008 A4)
#
# Gleiches Schema wie die 2D/3D-Selftests:
#   starten -> test_sim_*-Eingaben -> Zustands-/Pixel-Asserts
#   (test_pixel) -> test_screenshot -> sauberer Exit.
#
# Getestetes Spiel: voxel_sandbox.moo (Hybrid-Fenster, echte Voxel-Engine
# moo_voxel.c). Der Selftest nutzt dieselben Engine-Aufrufe: voxel_welt_neu,
# voxel_generieren, voxel_mesh_bauen + pro Frame chunk_zeichne pro Chunk
# (gl33-Render-Gotcha: gl33/gl21 zeichnen Chunks NICHT automatisch).
#
# KERN-ASSERT (User-Anforderung "Blickmitte ist Terrain, nicht Himmel"):
#   Der Himmel (raum_löschen) hat eine bekannte Farbe. Die Bildmitte muss
#   sich davon unterscheiden -> dort steht echtes gerendertes Terrain.
#   Zusaetzlich wird eine Himmels-Ecke positiv gegen die Klar-Farbe geprueft.
#
# WICHTIG (verifiziert): test_pixel/test_frame_grab/test_screenshot lesen
# den GL-Backbuffer und MUESSEN VOR hybrid_aktualisieren aufgerufen werden.
#
# Headless: xvfb-run -a -s "-screen 0 1280x800x24" env MOO_3D_BACKEND=gl33 \
#   moo-compiler run <datei>
# Artefakt-Ausgabeordner via env SELFTEST_OUT (Default /tmp).
# ============================================================

konstante WIN_B auf 1280
konstante WIN_H auf 800
konstante GEN_RADIUS auf 2

setze out auf umgebung("SELFTEST_OUT")
wenn out == nichts:
    setze out auf "/tmp"

setze fehler auf 0

# Himmel-Klarfarbe (wie voxel_sandbox): (0.53,0.81,0.92) -> (135,206,234)
konstante SKY_R auf 135
konstante SKY_G auf 206
konstante SKY_B auf 234

zeige "=== voxel_sandbox Voxel Selftest ==="

# --- Welt generieren ---
setze welt auf voxel_welt_neu(1337)
setze chunks_neu auf 0
für gcx in (0 - GEN_RADIUS)..(GEN_RADIUS + 1):
    für gcy in (0 - GEN_RADIUS)..(GEN_RADIUS + 1):
        setze chunks_neu auf chunks_neu + voxel_generieren(welt, gcx, gcy)
zeige "Terrain generiert: " + text(chunks_neu) + " Chunks"
wenn chunks_neu <= 0:
    zeige "ASSERT FAIL kein Terrain generiert"
    setze fehler auf fehler + 1

# --- Fenster + Kamera ---
setze win auf fenster_unified("voxel selftest", WIN_B, WIN_H)
raum_perspektive(win, 70.0, 0.1, 300.0)

setze info auf test_fenster_info(win)
zeige "fenster: backend=" + info["backend"] + " offen=" + text(info["offen"])
wenn info["backend"] != "hybrid-gl33":
    zeige "ASSERT FAIL backend " + info["backend"] + " != hybrid-gl33"
    setze fehler auf fehler + 1

# Kamera ueber der Mitte mit Blick nach unten auf das Terrain.
setze cam_x auf 16.0
setze cam_y auf 16.0
setze cam_z auf 40.0
setze yaw auf 3.926
setze pitch auf (0.0 - 0.6)

funktion blick_dx(yaw, pitch):
    gib_zurück cosinus(pitch) * cosinus(yaw)
funktion blick_dy(yaw, pitch):
    gib_zurück cosinus(pitch) * sinus(yaw)
funktion blick_dz(yaw, pitch):
    gib_zurück sinus(pitch)

# --- Mesh bauen + Render-IDs sammeln (gl33-Gotcha: explizit pro Chunk) ---
funktion mesh_alle(welt, radius):
    setze ids auf []
    für mcx in (0 - radius)..(radius + 1):
        für mcy in (0 - radius)..(radius + 1):
            für mcz in 0..2:
                setze rid auf voxel_mesh_bauen(welt, mcx, mcy, mcz)
                wenn rid >= 0:
                    ids.hinzufügen(rid)
    gib_zurück ids

setze chunk_ids auf mesh_alle(welt, GEN_RADIUS)
zeige "Render-Chunks gemesht: " + text(länge(chunk_ids))
wenn länge(chunk_ids) <= 0:
    zeige "ASSERT FAIL keine gemeshten Chunks"
    setze fehler auf fehler + 1

# --- Eingabe-Simulation: Hotbar via Mausrad + Bewegung via Taste ---
test_sim_maus_rad(win, 1.0)
setze rad auf raum_maus_rad(win)
wenn rad == 1.0:
    zeige "sim: raum_maus_rad konsumiert = " + text(rad)
sonst:
    zeige "ASSERT FAIL raum_maus_rad = " + text(rad) + " erwartet 1.0"
    setze fehler auf fehler + 1

test_sim_taste(win, "w", wahr)
wenn raum_taste(win, "w"):
    zeige "sim: raum_taste(w) -> wahr"
sonst:
    zeige "ASSERT FAIL raum_taste(w) trotz test_sim_taste nicht gedrueckt"
    setze fehler auf fehler + 1
test_sim_reset(win)

# --- Einen deterministischen Frame rendern ---
setze dx auf blick_dx(yaw, pitch)
setze dy auf blick_dy(yaw, pitch)
setze dz auf blick_dz(yaw, pitch)

# Mehrere Frames rendern, damit der Mesher/GPU-Upload sicher steht.
setze fr auf 0
solange fr < 3:
    raum_löschen(win, 0.53, 0.81, 0.92)
    raum_kamera(win, cam_x, cam_y, cam_z, cam_x + dx, cam_y + dy, cam_z + dz)
    für ci in chunk_ids:
        chunk_zeichne(ci)
    wenn fr < 2:
        hybrid_aktualisieren(win)
    setze fr auf fr + 1

# Letzter Frame: NICHT presenten bis gegrabbt. Re-render in den Backbuffer.
raum_löschen(win, 0.53, 0.81, 0.92)
raum_kamera(win, cam_x, cam_y, cam_z, cam_x + dx, cam_y + dy, cam_z + dz)
für ci in chunk_ids:
    chunk_zeichne(ci)

# --- Pixel-Asserts VOR present ---
setze f auf test_frame_grab(win)

# Himmel-Ecke oben muss die Klarfarbe sein.
setze p_sky auf test_pixel(f, 40, 40)
setze ds auf p_sky["rot"] - SKY_R
wenn ds < 0:
    setze ds auf 0 - ds
setze dsg auf p_sky["gruen"] - SKY_G
wenn dsg < 0:
    setze dsg auf 0 - dsg
wenn ds <= 12 und dsg <= 12:
    zeige "ASSERT OK   himmel-ecke = (" + text(p_sky["rot"]) + "," + text(p_sky["gruen"]) + "," + text(p_sky["blau"]) + ")"
sonst:
    zeige "ASSERT FAIL himmel-ecke = (" + text(p_sky["rot"]) + "," + text(p_sky["gruen"]) + "," + text(p_sky["blau"]) + ") erwartet ~(" + text(SKY_R) + "," + text(SKY_G) + "," + text(SKY_B) + ")"
    setze fehler auf fehler + 1

# KERN: Bildmitte muss Terrain sein, NICHT Himmel.
setze p_mitte auf test_pixel(f, WIN_B / 2, WIN_H / 2)
setze diff_r auf p_mitte["rot"] - SKY_R
wenn diff_r < 0:
    setze diff_r auf 0 - diff_r
setze diff_g auf p_mitte["gruen"] - SKY_G
wenn diff_g < 0:
    setze diff_g auf 0 - diff_g
setze diff_b auf p_mitte["blau"] - SKY_B
wenn diff_b < 0:
    setze diff_b auf 0 - diff_b
setze gesamt_diff auf diff_r + diff_g + diff_b
zeige "blickmitte = (" + text(p_mitte["rot"]) + "," + text(p_mitte["gruen"]) + "," + text(p_mitte["blau"]) + ") diff_zu_himmel=" + text(gesamt_diff)
wenn gesamt_diff > 40:
    zeige "ASSERT OK   blickmitte ist Terrain (nicht Himmel)"
sonst:
    zeige "ASSERT FAIL blickmitte sieht aus wie Himmel -> kein Terrain gerendert"
    setze fehler auf fehler + 1

# --- Screenshot-Artefakt (vor present) ---
setze shot auf out + "/voxel_sandbox_selftest.bmp"
test_frame_save_bmp(f, shot)
zeige "screenshot: " + shot

hybrid_aktualisieren(win)
warte(30)
hybrid_schliessen(win)

wenn fehler == 0:
    zeige "SELFTEST_RESULT: PASS voxel_sandbox"
sonst:
    zeige "SELFTEST_RESULT: FAIL voxel_sandbox (" + text(fehler) + " Fehler)"
