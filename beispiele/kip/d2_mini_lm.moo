# ============================================================
# KIP-D2 Mini-LM-Gate: Mixed-Precision bf16 vs f32.
# Winziger Transformer lernt eine periodische Zeichenfolge (deterministisch,
# KEIN Sampling/zufall). Exerziert den M-A-Op-Satz: embedding(gather) + Position
# + RMSNorm(B1) + Attention + SwiGLU(B3) + Head + Kreuzentropie.
#
# DERSELBE Binary-Lauf, Umschaltung per Umgebungsvariable (C-Runtime liest sie):
#   MOO_KI_BF16 nicht gesetzt  -> f32-Referenz
#   MOO_KI_BF16=1              -> bf16-Aktivierungen (Parameter-Master + Optimizer f32)
# Gate: beide Kurven fallen deterministisch, Endverlust in Toleranz; bf16 zweimal
# gleich (Determinismus). Nur Trainingsverlust wird gedruckt (bit-vergleichbar).
# ============================================================

setze block auf 16
setze dim auf 16
setze koepfe auf 2
setze schritte auf 160

# --- 1. Deterministischer Korpus: periodisches Alphabet ---
setze basis auf "abcdefghijklmnop"
setze korpus auf ""
setze r auf 0
solange r < 40:
    setze korpus auf korpus + basis
    setze r auf r + 1
setze tok auf text_tokenizer(korpus)
setze ids auf tok["ids"]
setze vokab auf tok["vokab"]
setze n auf ids.groesse()
zeige "Korpus: " + text(n) + " Zeichen, Vokabular: " + text(vokab)

# --- 2. Modell (feste Seeds -> reproduzierbar) ---
setze emb  auf schicht_embedding(vokab, dim, 1)
setze pos  auf schicht_position(block, dim, "gelernt", 2)
setze ln1a auf schicht_rmsnorm(dim)
setze att1 auf schicht_attention(dim, koepfe, 3)
setze ln1b auf schicht_rmsnorm(dim)
setze ff1  auf schicht_ffn_gated(dim, 32, "swiglu")
setze lnf  auf schicht_rmsnorm(dim)
setze kopf auf schicht_dicht(dim, vokab, "keine", 9)
setze alle auf [emb, pos, ln1a, att1, ln1b, ff1, lnf, kopf]

funktion modell(ids_block):
    setze x auf vorwaerts([emb], ids_block)
    setze x auf vorwaerts([pos], x)
    setze x auf x + vorwaerts([ln1a, att1], x)
    setze x auf x + vorwaerts([ln1b, ff1], x)
    gib_zurück vorwaerts([lnf, kopf], x)

# --- 3. Training: deterministisch (feste Lernrate, fester Stride) ---
setze params auf parameter(alle)
setze opt auf optimierer_adam(params, 0.01)
setze grenze auf n - block - 1
setze p auf 0
setze s auf 0
solange s < schritte:
    setze verlust auf kreuzentropie(modell(ids.zeilen(p, p + block)), ids.zeilen(p + 1, p + block + 1))
    verlust.rueckwaerts()
    opt.schritt()
    wenn s - boden(s / 40) * 40 == 0:
        zeige "step " + text(s) + " loss " + text(verlust.zu_liste()[0])
    setze p auf p + 17
    wenn p >= grenze:
        setze p auf p - grenze
    setze s auf s + 1

zeige "FINAL loss " + text(verlust.zu_liste()[0])
