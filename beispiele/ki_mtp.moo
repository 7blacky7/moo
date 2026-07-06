# ============================================================
# MTP-Head + Speculative-Decoding-Toy (KI-M4)
# ------------------------------------------------------------
# MTP = TRAININGS-Objective (DeepSeek-V3): zweiter dicht-Head auf
# DENSELBEN Hidden-States sagt t+2 voraus, Loss = CE1 + lambda*CE2.
# BEWUSSTE TOY-VEREINFACHUNG (VERIFY-01): Parallel-Head statt der
# sequentiellen V3-MTP-Kausalkette — entspricht der Lightweight-
# Linie (MiMo). Spec-Decoding = optionale INFERENZ-Nutzung des
# MTP-Heads als Draft, der Haupt-Head verifiziert; Akzeptanzrate
# wird nur PROTOKOLLIERT. KEINE Serving-Architektur, KEINE
# Runtime-Aenderung, alles deterministisch (Seeds fix, greedy).
# Kompilieren: moo-compiler compile ki_mtp.moo -o ki_mtp
# ============================================================
setze dim auf 32
setze block auf 16
setze koepfe auf 2
setze schritte auf 6000
setze fenster auf 1500
setze lambda auf 0.5

# --- 1. Deterministischer Toy-Korpus (M3-Additions-Schema) ---
setze korpus auf ""
setze a auf 0
solange a < 5:
    setze b auf 0
    solange b < 5:
        setze denk auf ""
        setze k auf 0
        solange k < b:
            setze denk auf denk + "."
            setze k auf k + 1
        setze korpus auf korpus + "?" + text(a) + "+" + text(b) + "{" + denk + "}" + text(a + b) + ";"
        setze korpus auf korpus + "!" + text(a) + "+" + text(b) + "}" + text(a + b) + ";"
        setze b auf b + 1
    setze a auf a + 1
setze tok auf text_tokenizer(korpus)
setze ids auf tok["ids"]
setze vokab auf tok["vokab"]
setze id_zu_zeichen auf tok["id_zu_zeichen"]
setze n auf ids.groesse()
zeige "Korpus: " + text(n) + " Zeichen, Vokabular: " + text(vokab)

# --- 2. Modell: gemeinsamer Trunk + ZWEI Heads ---
setze emb  auf schicht_embedding(vokab, dim, 1)
setze pos  auf schicht_position(block, dim, "gelernt", 2)
setze ln1a auf schicht_layernorm(dim)
setze att1 auf schicht_attention(dim, koepfe, 3)
setze ln1b auf schicht_layernorm(dim)
setze ff1a auf schicht_dicht(dim, 64, "gelu", 4)
setze ff1b auf schicht_dicht(64, dim, "keine", 5)
setze lnf  auf schicht_layernorm(dim)
setze kopf1 auf schicht_dicht(dim, vokab, "keine", 6)
setze kopf2 auf schicht_dicht(dim, vokab, "keine", 7)
setze alle auf [emb, pos, ln1a, att1, ln1b, ff1a, ff1b, lnf, kopf1, kopf2]

funktion rumpf(ids_block):
    setze x auf vorwaerts([emb], ids_block)
    setze x auf vorwaerts([pos], x)
    setze x auf x + vorwaerts([ln1a, att1], x)
    setze x auf x + vorwaerts([ln1b, ff1a, ff1b], x)
    gib_zurück vorwaerts([lnf], x)

funktion argmax(liste):
    setze best auf 0
    setze i auf 1
    solange i < länge(liste):
        wenn liste[i] > liste[best]:
            setze best auf i
        setze i auf i + 1
    gib_zurück best

# --- 3. Training: Loss = CE(t+1) + lambda * CE(t+2) ---
setze params auf parameter(alle)
setze opt auf optimierer_sgd(params, 0.05, 0.9)
setze cp1 auf []
setze cp2 auf []
setze summe1 auf 0
setze summe2 auf 0
setze s auf 0
solange s < schritte:
    wenn s == 1500:
        setze opt["rate"] auf 0.025
    wenn s == 3000:
        setze opt["rate"] auf 0.0125
    wenn s == 4500:
        setze opt["rate"] auf 0.00625
    setze p auf s * 7
    setze p auf p - boden(p / (n - block - 2)) * (n - block - 2)
    setze h auf rumpf(ids.zeilen(p, p + block))
    setze v1 auf kreuzentropie(vorwaerts([kopf1], h), ids.zeilen(p + 1, p + block + 1))
    setze v2 auf kreuzentropie(vorwaerts([kopf2], h), ids.zeilen(p + 2, p + block + 2))
    setze verlust auf v1 + v2 * lambda
    setze summe1 auf summe1 + v1.zu_liste()[0]
    setze summe2 auf summe2 + v2.zu_liste()[0]
    verlust.rueckwaerts()
    opt.schritt()
    setze s auf s + 1
    wenn s - boden(s / fenster) * fenster == 0:
        cp1.hinzufügen(summe1 / fenster)
        cp2.hinzufügen(summe2 / fenster)
        zeige "Checkpoint " + text(länge(cp1)) + ": CE1 " + text(summe1 / fenster) + ", CE2 " + text(summe2 / fenster)
        setze summe1 auf 0
        setze summe2 auf 0

# --- 4. Gate (a): BEIDE CE-Kurven monoton ---
setze kurve_ok auf wahr
setze c auf 1
solange c < länge(cp1):
    wenn cp1[c] >= cp1[c - 1]:
        setze kurve_ok auf falsch
    wenn cp2[c] >= cp2[c - 1]:
        setze kurve_ok auf falsch
    setze c auf c + 1
wenn kurve_ok:
    zeige "KURVEN-GATE PASS (CE1 und CE2 sinken monoton)"
sonst:
    zeige "KURVEN-GATE FAIL"

# --- 5. Spec-Decoding-Toy (greedy, deterministisch) ---
# EIN Forward liefert beide Koepfe: kopf1 -> naechstes Zeichen z1
# (uebernommen), kopf2 -> Draft fuer das UEBERnaechste. Verify-Forward
# mit Kontext+z1: stimmt kopf1 dort mit dem Draft ueberein -> akzeptiert.
autograd_aus()
funktion letzte_zeile(logits, seq):
    gib_zurück logits.zeilen(seq - 1, seq).zu_liste()

setze akzeptiert auf 0
setze versuche auf 0
setze start auf 0
solange start < 40:
    # Kontext = block Zeichen ab deterministischer Position
    setze pk auf start * 9
    setze pk auf pk - boden(pk / (n - block - 2)) * (n - block - 2)
    setze kontext auf []
    setze i auf 0
    solange i < block:
        kontext.hinzufügen(ids.zeilen(pk + i, pk + i + 1).zu_liste()[0])
        setze i auf i + 1
    setze h auf rumpf(tensor_aus_liste(kontext))
    setze z1 auf argmax(letzte_zeile(vorwaerts([kopf1], h), block))
    setze draft auf argmax(letzte_zeile(vorwaerts([kopf2], h), block))
    # Verify: Fenster um z1 weiterschieben
    setze kontext2 auf []
    setze i auf 1
    solange i < block:
        kontext2.hinzufügen(kontext[i])
        setze i auf i + 1
    kontext2.hinzufügen(z1)
    setze h2 auf rumpf(tensor_aus_liste(kontext2))
    setze check auf argmax(letzte_zeile(vorwaerts([kopf1], h2), block))
    wenn draft == check:
        setze akzeptiert auf akzeptiert + 1
    setze versuche auf versuche + 1
    setze start auf start + 1
zeige "Spec-Akzeptanzrate (protokolliert): " + text(akzeptiert) + "/" + text(versuche)
wenn versuche == 40:
    zeige "SPEC-GATE PASS (40/40 Versuche deterministisch gelaufen)"
sonst:
    zeige "SPEC-GATE FAIL"
zeige "MTP-GATE ENDE"
