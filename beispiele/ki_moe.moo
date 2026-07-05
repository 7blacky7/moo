# ============================================================
# Mini-MoE Toy-Gate (KI-M1, VERIFY-01) — 2 Experten, top-1
# Prueft:
#   (a) Verlust sinkt deterministisch (Seed 7)
#   (b) Auslastung nicht-degeneriert (beide Experten > 10%)
#   (c) MoE-vs-dense-Vergleichslauf (gleiche Toy-Regression)
#   (d) .mook-Roundtrip: /tmp/moe1.mook + /tmp/moe2.mook werden
#       geschrieben — Bit-Identitaet prueft das Gate-Skript per cmp;
#       hier zusaetzlich: geladenes Netz sagt exakt dasselbe.
# Kompilieren: moo-compiler compile ki_moe.moo -o ki_moe
# ============================================================

# --- Deterministische Toy-Regression: 16 Zeilen, dim 4 ---
setze xl auf []
setze zl auf []
setze i auf 0
solange i < 16:
    setze zeile auf []
    setze zzeile auf []
    setze j auf 0
    solange j < 4:
        zeile.hinzufügen(((i * 7 + j * 3) - boden((i * 7 + j * 3) / 11) * 11) / 11.0 - 0.5)
        zzeile.hinzufügen(((i * (j + 2)) - boden((i * (j + 2)) / 5) * 5) / 5.0 - 0.4)
        setze j auf j + 1
    xl.hinzufügen(zeile)
    zl.hinzufügen(zzeile)
    setze i auf i + 1
setze x auf tensor_aus_liste(xl)
setze ziel auf tensor_aus_liste(zl)

# --- (a) MoE trainieren: dim 4, versteckt 16, n=2 Experten, k=1 ---
setze moe auf schicht_moe(4, 16, 2, 1, 7)
setze netz auf ki_netz([moe])
setze opt auf optimierer_adam(parameter(netz), 0.01)
setze start auf 0
setze ende auf 0
setze i auf 0
solange i < 200:
    setze ausgabe auf vorwaerts(netz, x)
    setze fehler auf mse(ausgabe, ziel) + moe_balance(netz) * 0.01
    wenn i == 0:
        setze start auf fehler.zu_liste()[0]
    wenn i == 199:
        setze ende auf fehler.zu_liste()[0]
    fehler.rueckwaerts()
    opt.schritt()
    setze i auf i + 1
zeige "MoE-Verlust: " + text(start) + " -> " + text(ende)
wenn ende < start:
    zeige "CHECK-A PASS (Verlust sinkt)"
sonst:
    zeige "CHECK-A FAIL"

# --- (b) Auslastung: beide Experten > 10% der Zuweisungen ---
setze a0 auf moe["auslastung_e0"]
setze a1 auf moe["auslastung_e1"]
setze anteil0 auf a0 / (a0 + a1)
zeige "Auslastung e0/e1: " + text(a0) + "/" + text(a1)
wenn anteil0 > 0.1 und anteil0 < 0.9:
    zeige "CHECK-B PASS (Auslastung nicht-degeneriert)"
sonst:
    zeige "CHECK-B FAIL"

# --- (c) Dense-Vergleichslauf (gleiche Daten, gleiche Iterationen) ---
setze dnetz auf [schicht_dicht(4, 16, "relu", 7), schicht_dicht(16, 4, "keine", 8)]
setze dopt auf optimierer_adam(parameter(dnetz), 0.01)
setze dende auf 0
setze i auf 0
solange i < 200:
    setze dfehler auf mse(vorwaerts(dnetz, x), ziel)
    wenn i == 199:
        setze dende auf dfehler.zu_liste()[0]
    dfehler.rueckwaerts()
    dopt.schritt()
    setze i auf i + 1
zeige "Dense-Endverlust (Vergleich): " + text(dende)
zeige "CHECK-C PASS (Vergleichslauf gelaufen)"

# --- (d) .mook-Roundtrip ---
autograd_aus()
netz.speichern("/tmp/moe1.mook")
setze kopie auf ki_laden("/tmp/moe1.mook")
kopie.speichern("/tmp/moe2.mook")
setze v1 auf text(vorwaerts(netz, x).zu_liste())
setze v2 auf text(vorwaerts(kopie, x).zu_liste())
wenn v1 == v2:
    zeige "CHECK-D PASS (geladenes Netz identische Vorhersage)"
sonst:
    zeige "CHECK-D FAIL"
zeige "MOE-GATE ENDE"
