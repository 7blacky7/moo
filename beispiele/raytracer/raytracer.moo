# ============================================================
# moo Raytracer — Pinhole-Projektion mit Sphaeren, Lighting, Schatten, Reflektion
#
# Kompilieren: moo-compiler compile raytracer.moo -o raytracer
# Starten:     ./raytracer
# Ausgabe:     output.ppm (P3 ASCII)
# Anzeigen:    feh output.ppm  oder  eog output.ppm
# ============================================================

konstante BREITE auf 320
konstante HOEHE auf 240
konstante MAX_DEPTH auf 3
konstante EPS auf 0.0001
konstante GROSS auf 100000.0

# --- Vec3 Klasse ---
klasse Vec3:
    funktion erstelle(x, y, z):
        selbst.x = x
        selbst.y = y
        selbst.z = z

    funktion plus(andere):
        gib_zurück neu Vec3(selbst.x + andere.x, selbst.y + andere.y, selbst.z + andere.z)

    funktion minus(andere):
        gib_zurück neu Vec3(selbst.x - andere.x, selbst.y - andere.y, selbst.z - andere.z)

    funktion mal(s):
        gib_zurück neu Vec3(selbst.x * s, selbst.y * s, selbst.z * s)

    funktion mal_vec(andere):
        gib_zurück neu Vec3(selbst.x * andere.x, selbst.y * andere.y, selbst.z * andere.z)

    funktion dot(andere):
        gib_zurück selbst.x * andere.x + selbst.y * andere.y + selbst.z * andere.z

    funktion laenge():
        gib_zurück wurzel(selbst.x * selbst.x + selbst.y * selbst.y + selbst.z * selbst.z)

    funktion normiere():
        setze l auf selbst.laenge()
        wenn l == 0:
            gib_zurück neu Vec3(0, 0, 0)
        gib_zurück neu Vec3(selbst.x / l, selbst.y / l, selbst.z / l)

    funktion reflektiere(normale):
        # v - 2*(v·n)*n
        setze d auf selbst.dot(normale)
        gib_zurück selbst.minus(normale.mal(2 * d))

# --- Sphere Klasse ---
klasse Sphere:
    funktion erstelle(zentrum, radius, farbe, reflektivitaet):
        selbst.zentrum = zentrum
        selbst.radius = radius
        selbst.farbe = farbe
        selbst.reflektivitaet = reflektivitaet

    funktion schneide(ray_origin, ray_dir):
        # Returns t (Distanz entlang Ray) oder -1 bei kein Treffer
        setze oc auf ray_origin.minus(selbst.zentrum)
        setze a auf ray_dir.dot(ray_dir)
        setze b auf 2 * oc.dot(ray_dir)
        setze c auf oc.dot(oc) - selbst.radius * selbst.radius
        setze diskr auf b * b - 4 * a * c
        wenn diskr < 0:
            gib_zurück -1
        setze sqd auf wurzel(diskr)
        setze t1 auf (-b - sqd) / (2 * a)
        setze t2 auf (-b + sqd) / (2 * a)
        wenn t1 > EPS:
            gib_zurück t1
        wenn t2 > EPS:
            gib_zurück t2
        gib_zurück -1

# --- Globale Szene ---
setze sphaeren auf []
sphaeren.hinzufügen(neu Sphere(neu Vec3(0, -1, -5), 1.0, neu Vec3(0.9, 0.2, 0.2), 0.3))
sphaeren.hinzufügen(neu Sphere(neu Vec3(-2, 0, -6), 1.0, neu Vec3(0.2, 0.9, 0.2), 0.5))
sphaeren.hinzufügen(neu Sphere(neu Vec3(2, 0, -6), 1.0, neu Vec3(0.2, 0.2, 0.9), 0.7))
sphaeren.hinzufügen(neu Sphere(neu Vec3(0, -101, -5), 100.0, neu Vec3(0.5, 0.5, 0.5), 0.1))

setze licht_pos auf neu Vec3(5, 5, 0)
setze licht_intensitaet auf 1.5
setze ambient auf 0.1

# --- Finde naechste Sphere Intersection ---
funktion naechste_sphere(ray_origin, ray_dir):
    setze naechstes_t auf GROSS
    setze getroffen auf nichts
    für s in sphaeren:
        setze t auf s.schneide(ray_origin, ray_dir)
        wenn t > EPS und t < naechstes_t:
            setze naechstes_t auf t
            setze getroffen auf s
    gib_zurück {"t": naechstes_t, "sphere": getroffen}

# --- Im Schatten? ---
funktion im_schatten(punkt, licht_dir, licht_dist):
    für s in sphaeren:
        setze t auf s.schneide(punkt, licht_dir)
        wenn t > EPS und t < licht_dist:
            gib_zurück wahr
    gib_zurück falsch

# --- Haupt-Trace-Funktion (rekursiv) ---
funktion trace(ray_origin, ray_dir, depth):
    wenn depth <= 0:
        gib_zurück neu Vec3(0.05, 0.05, 0.1)

    setze hit auf naechste_sphere(ray_origin, ray_dir)
    setze t auf hit["t"]
    setze sphere auf hit["sphere"]

    wenn sphere == nichts:
        # Himmel (Gradient)
        setze y_norm auf ray_dir.y * 0.5 + 0.5
        gib_zurück neu Vec3(0.5 + y_norm * 0.3, 0.7 + y_norm * 0.2, 1.0)

    # Trefferpunkt
    setze treffer auf ray_origin.plus(ray_dir.mal(t))
    setze normale auf treffer.minus(sphere.zentrum).normiere()

    # Licht-Richtung
    setze licht_vec auf licht_pos.minus(treffer)
    setze licht_dist auf licht_vec.laenge()
    setze licht_dir auf licht_vec.normiere()

    # Ambient
    setze farbe auf sphere.farbe.mal(ambient)

    # Diffuse (Lambertian) + Schatten-Check
    setze sp auf treffer.plus(normale.mal(EPS))
    wenn nicht im_schatten(sp, licht_dir, licht_dist):
        setze diff auf normale.dot(licht_dir)
        wenn diff > 0:
            setze diff_farbe auf sphere.farbe.mal(diff * licht_intensitaet)
            setze farbe auf farbe.plus(diff_farbe)

    # Reflektion (rekursiv)
    wenn sphere.reflektivitaet > 0 und depth > 0:
        setze refl_dir auf ray_dir.reflektiere(normale).normiere()
        setze refl_origin auf treffer.plus(normale.mal(EPS))
        setze refl_farbe auf trace(refl_origin, refl_dir, depth - 1)
        setze farbe auf farbe.mal(1 - sphere.reflektivitaet).plus(refl_farbe.mal(sphere.reflektivitaet))

    gib_zurück farbe

# --- Pixel-Wert zu 0-255 clampen ---
funktion clamp_farbe(c):
    setze r auf c * 255
    wenn r < 0:
        gib_zurück 0
    wenn r > 255:
        gib_zurück 255
    gib_zurück boden(r)

# --- Render Loop ---
setze kamera auf neu Vec3(0, 0, 0)
setze aspect auf BREITE / HOEHE
setze fov auf 60
setze scale auf 1.0  # tan(30°) ≈ 0.577, aber 1.0 wirkt okay bei FOV 90

zeige "=== moo Raytracer ==="
zeige "Aufloesung: 320x240"
zeige "Sphaeren: 4 (3 farbige + Boden)"
zeige "Licht: 1 Punktlicht"
zeige "Max Reflektions-Tiefe: 3"
zeige "Rendere..."

# Sammel-Liste statt String-Concat (O(n) statt O(n²))
setze teile auf []
teile.hinzufügen("P3\n" + text(BREITE) + " " + text(HOEHE) + "\n255\n")

setze y auf 0
solange y < HOEHE:
    setze x auf 0
    solange x < BREITE:
        # Normalisiere Pixel zu [-1, 1] mit Aspect-Ratio
        setze px auf (2 * (x + 0.5) / BREITE - 1) * aspect * scale
        setze py auf (1 - 2 * (y + 0.5) / HOEHE) * scale

        setze dir auf neu Vec3(px, py, -1).normiere()
        setze farbe auf trace(kamera, dir, MAX_DEPTH)

        setze r auf clamp_farbe(farbe.x)
        setze g auf clamp_farbe(farbe.y)
        setze b auf clamp_farbe(farbe.z)

        teile.hinzufügen(text(r) + " " + text(g) + " " + text(b) + "\n")
        setze x auf x + 1

    wenn y % 20 == 0:
        zeige "Zeile: " + text(y)
    setze y auf y + 1

zeige "Joine Liste..."
setze ppm auf teile.verbinden("")
zeige "Schreibe output.ppm..."
datei_schreiben("output.ppm", ppm)
zeige "Fertig! feh output.ppm zum Anzeigen"
