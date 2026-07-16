# ============================================================
# KIP-E2: CPU-Voll-Checkpoint — Kill+Resume bit-identisch (moo-Ebene)
# Beweist die codegen-Verdrahtung der checkpoint_*-Builtins UND die
# Bit-Identitaet MIT Dropout im Netz (mutierender Zaehler wird restauriert).
# ============================================================
setze x auf tensor_aus_liste([[0.5,-1.0,2.0],[0.25,-0.75,1.5],[-2.0,3.0,0.1],[1.0,-0.2,0.4]])
setze ziel auf tensor_aus_liste([[1,0],[0,1],[1,0],[0,1]])

funktion baue():
    gib_zurück ki_netz([schicht_dicht(3, 8, "tanh", 1), schicht_dropout(0.3), schicht_dicht(8, 2, "keine", 2)])

# --- Referenz: 12 Schritte ununterbrochen ---
setze netzR auf baue()
setze optR auf optimierer_adam(parameter(netzR), 0.02)
setze ref auf []
setze i auf 0
solange i < 12:
    setze f auf mse(vorwaerts(netzR, x), ziel)
    ref.hinzufügen(f.zu_liste()[0])
    f.rueckwaerts()
    optR.schritt()
    setze i auf i + 1

# --- Resume: 6 Schritte, Checkpoint, Kill, Reload, weiter bis 12 ---
setze netz auf baue()
setze opt auf optimierer_adam(parameter(netz), 0.02)
setze res auf []
setze i auf 0
solange i < 6:
    setze f auf mse(vorwaerts(netz, x), ziel)
    res.hinzufügen(f.zu_liste()[0])
    f.rueckwaerts()
    opt.schritt()
    setze i auf i + 1

setze zustand auf {}
zustand["netz"] = netz
zustand["opt"] = opt
zustand["schritt"] = 6
zustand["tokenizer_version"] = "tok-v1"
checkpoint_speichern(zustand, "/tmp/moo_e2e_ckpt.mook")

setze erw auf {}
erw["tokenizer_version"] = "tok-v1"
setze z2 auf checkpoint_laden("/tmp/moo_e2e_ckpt.mook", erw)
setze netz2 auf z2["netz"]
setze opt2 auf z2["opt"]
setze i auf 6
solange i < 12:
    setze f auf mse(vorwaerts(netz2, x), ziel)
    res.hinzufügen(f.zu_liste()[0])
    f.rueckwaerts()
    opt2.schritt()
    setze i auf i + 1

# --- Vergleich: Kurve muss bit-identisch sein ---
setze gleich auf wahr
setze i auf 0
solange i < 12:
    wenn res[i] != ref[i]:
        setze gleich auf falsch
    setze i auf i + 1

setze status auf "nein"
wenn gleich:
    setze status auf "ja"
zeige "resume bit-identisch: " + status

setze sstat auf "nein"
wenn z2["schritt"] == 6:
    setze sstat auf "ja"
zeige "schritt restauriert: " + sstat
