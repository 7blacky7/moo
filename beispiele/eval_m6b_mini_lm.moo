# ============================================================
# Mini-Zeichen-Sprachmodell: ein Transformer lernt Grimm-Maerchen
# (Plan-014 G1-Gate).
#
# EHRLICH: Das ist ein LERN-DEMONSTRATOR, KEIN Chatbot.
# Ein winziges Modell (2 Transformer-Bloecke, dim 64, ~120k Parameter)
# lernt auf ~72.000 Zeichen, das NAECHSTE ZEICHEN vorherzusagen.
# Nach ein paar Minuten CPU-Training (~90 Sekunden auf einem
# Desktop-Prozessor) entstehen deutsche Woerter,
# Satzzeichen und Maerchen-Floskeln — aber kein zusammenhaengender
# Sinn und erst recht keine Antworten auf Fragen. Genau dieser
# Sprung von Rauschen zu Struktur ist der Beweis, DASS es lernt.
#
# Vorher einmal ausfuehren:  ./skripte/maerchen_download.sh
# Dann:  moo-compiler compile beispiele/ki_sprachmodell.moo -o ki_lm
#        ./ki_lm            (aus dem Repo-Wurzelverzeichnis)
#
# Was dieses Beispiel zeigt, was ki_xor/ki_mnist nicht zeigen:
#   * Residual-Verbindungen auf moo-Ebene: x = x + vorwaerts(...)
#     (der +-Operator auf Tensoren laeuft ueber den Autograd-Tape)
#   * Low-Level-Trainingsschleife: Sprachmodell-Ziele (um 1
#     verschobene Zeichen) passen nicht in netz.trainiere()
#   * Generierung unter autograd_aus() mit Greedy- und
#     Temperatur-Sampling
# ============================================================

setze block auf 64          # Sequenzlaenge (volle Bloecke beim Training)
setze dim auf 64            # Modellbreite
setze koepfe auf 4          # Attention-Koepfe
setze schritte auf 4000    # Trainingsschritte
setze fenster auf 1000     # Schritte pro Loss-Checkpoint

# M6b-EVAL NUR: simuliert per-output-channel int8 absmax und dequantisiert
# sofort zurück nach f32. Das ist KEIN Produktiv-Builtin und ändert kein Format.
funktion m6b_abs(x):
    wenn x < 0:
        gib_zurück 0 - x
    gib_zurück x

funktion m6b_int8_pc_dequant(t):
    setze a auf t.zu_liste()
    setze form auf t.form()
    setze zeilen auf form[0]
    setze spalten auf form[1]
    setze skalen auf []
    setze j auf 0
    solange j < spalten:
        setze mx auf 0
        setze i auf 0
        solange i < zeilen:
            setze av auf m6b_abs(a[i * spalten + j])
            wenn av > mx:
                setze mx auf av
            setze i auf i + 1
        wenn mx == 0:
            skalen.hinzufügen(1)
        sonst:
            skalen.hinzufügen(mx / 127.0)
        setze j auf j + 1
    setze ausgabe_liste auf []
    setze i auf 0
    solange i < zeilen:
        setze z auf []
        setze j auf 0
        solange j < spalten:
            setze q auf runde(a[i * spalten + j] / skalen[j])
            wenn q > 127:
                setze q auf 127
            wenn q < -127:
                setze q auf -127
            z.hinzufügen(q * skalen[j])
            setze j auf j + 1
        ausgabe_liste.hinzufügen(z)
        setze i auf i + 1
    gib_zurück tensor_aus_liste(ausgabe_liste)

setze rate auf 0.05         # SGD-Spitzen-Lernrate (mit Warmup + Cosine-Abfall)

# ------------------------------------------------------------
# 1. Korpus laden + in Zeichen-Ids uebersetzen
# ------------------------------------------------------------
zeige "Lade Korpus ..."
setze korpus auf datei_lesen("beispiele/daten/maerchen.txt")
setze tok auf text_tokenizer(korpus)
setze ids auf tok["ids"]
setze vokab auf tok["vokab"]
setze id_zu_zeichen auf tok["id_zu_zeichen"]
setze n auf ids.groesse()
zeige "Zeichen: " + text(n) + ", Vokabular: " + text(vokab)

# ------------------------------------------------------------
# 2. Modell bauen (alle Seeds fest -> reproduzierbar)
# ------------------------------------------------------------
setze emb  auf schicht_embedding(vokab, dim, 1)
setze pos  auf schicht_position(block, dim, "gelernt", 2)
setze ln1a auf schicht_layernorm(dim)
setze att1 auf schicht_attention(dim, koepfe, 3)
setze ln1b auf schicht_layernorm(dim)
setze ff1a auf schicht_dicht(dim, 256, "gelu", 4)
setze ff1b auf schicht_dicht(256, dim, "keine", 5)
setze ln2a auf schicht_layernorm(dim)
setze att2 auf schicht_attention(dim, koepfe, 6)
setze ln2b auf schicht_layernorm(dim)
setze ff2a auf schicht_dicht(dim, 256, "gelu", 7)
setze ff2b auf schicht_dicht(256, dim, "keine", 8)
setze lnf  auf schicht_layernorm(dim)
setze kopf auf schicht_dicht(dim, vokab, "keine", 9)

setze alle auf [emb, pos, ln1a, att1, ln1b, ff1a, ff1b,
                ln2a, att2, ln2b, ff2a, ff2b, lnf, kopf]

# Vorwaerts mit Residuals AUF MOO-EBENE (Pre-LayerNorm, GPT-Stil):
# jede Zeile x = x + vorwaerts(...) ist eine echte Residual-Verbindung,
# der +-Operator zeichnet auf den Autograd-Tape auf.
funktion modell(ids_block):
    setze x auf vorwaerts([emb], ids_block)
    setze x auf vorwaerts([pos], x)
    setze x auf x + vorwaerts([ln1a, att1], x)
    setze x auf x + vorwaerts([ln1b, ff1a, ff1b], x)
    setze x auf x + vorwaerts([ln2a, att2], x)
    setze x auf x + vorwaerts([ln2b, ff2a, ff2b], x)
    gib_zurück vorwaerts([lnf, kopf], x)

# ------------------------------------------------------------
# 3. Generierung (Greedy = deterministisch, Temperatur = Demo)
# ------------------------------------------------------------
funktion argmax(liste):
    setze best auf 0
    setze i auf 1
    solange i < länge(liste):
        wenn liste[i] > liste[best]:
            setze best auf i
        setze i auf i + 1
    gib_zurück best

# kontext: Liste von Zeichen-Ids. Zieht `anzahl` neue Zeichen.
# temperatur 0 = Greedy (argmax), sonst Temperatur-Sampling.
funktion generiere(kontext_start, anzahl, temperatur):
    setze kontext auf []
    für id in kontext_start:
        kontext.hinzufügen(id)
    setze erzeugt auf ""
    setze s auf 0
    solange s < anzahl:
        # Fenster = letzte hoechstens `block` Ids
        setze start auf 0
        wenn länge(kontext) > block:
            setze start auf länge(kontext) - block
        setze fenster_ids auf []
        setze i auf start
        solange i < länge(kontext):
            fenster_ids.hinzufügen(kontext[i])
            setze i auf i + 1
        setze eingabe auf tensor_aus_liste(fenster_ids)
        setze logits auf modell(eingabe)
        # nur die letzte Zeile interessiert (naechstes Zeichen)
        setze seq auf länge(fenster_ids)
        setze zeile auf logits.zeilen(seq - 1, seq)
        setze id auf 0
        wenn temperatur > 0:
            setze wahrsch auf (zeile * (1.0 / temperatur)).softmax().zu_liste()
            setze r auf zufall()
            setze summe auf 0
            setze k auf 0
            solange k < länge(wahrsch):
                setze summe auf summe + wahrsch[k]
                wenn r < summe:
                    setze id auf k
                    setze k auf länge(wahrsch)
                sonst:
                    setze k auf k + 1
        sonst:
            setze id auf argmax(zeile.zu_liste())
        kontext.hinzufügen(id)
        setze erzeugt auf erzeugt + id_zu_zeichen[id]
        setze s auf s + 1
    gib_zurück erzeugt

# Startkontext: die ersten 16 Zeichen des Korpus (deterministisch)
setze anfang auf ids.zeilen(0, 16).zu_liste()

autograd_aus()
zeige ""
zeige "=== VORHER (untrainiert, Greedy): ==="
zeige generiere(anfang, 150, 0)
autograd_an()

# ------------------------------------------------------------
# 4. Training: Low-Level-Schleife
#    Ziel jedes Blocks = derselbe Block, um 1 Zeichen verschoben.
# ------------------------------------------------------------
setze params auf parameter(alle)
# SGD mit Momentum statt Adam — BEWUSST. DIAGNOSTIZIERT (ADAM-B1):
# Bei batch=1 sind die Gradienten pro Koordinate am Unigramm-Plateau
# winzig (sqrt(v) ~ 1e-3); Adams eps=1e-8 ist dagegen bedeutungslos,
# also bekommt jede Rausch-Richtung die volle Schrittgroesse ~rate
# (Adam degeneriert Richtung sign-SGD) -> reproduzierbarer Verlust-
# Buckel ab dem Plateau, schlimmer je NIEDRIGER die Rate. Abhilfe,
# falls Adam gewuenscht: opt["eps"] auf 0.01 setzen (Dosis-Wirkung
# bewiesen: 1e-5 wirkungslos, 1e-3 gedaempft, 1e-2 Buckel weg).
# Fuer XOR/MNIST (trainiere-API mit echten Batches) bleibt Adam mit
# Standard-eps die richtige Wahl.
setze opt auf optimierer_sgd(params, rate, 0.9)

# Lernraten-Plan wie in der Kinderleicht-API: linearer Warmup,
# dann Cosine-Abfall — ohne Warmup springt der Verlust bei kleinen
# Transformern gern mitten im Training nach oben (selbst beobachtet).
funktion lernrate(schritt):
    setze warm auf 300
    wenn schritt < warm:
        gib_zurück rate * (schritt + 1) / warm
    setze rest auf (schritt - warm) / (schritte - warm)
    gib_zurück rate * (0.1 + 0.9 * 0.5 * (1.0 + cos(3.14159265358979 * rest)))
setze grenze auf n - block - 1
setze p auf 0
setze sprung auf 631        # Positions-Schrittweite (durchmischt den Korpus)
setze fenster_summe auf 0
setze checkpoints auf []

zeige ""
zeige "Trainiere " + text(schritte) + " Schritte ..."
setze s auf 0
solange s < schritte:
    # Zwei Bloecke pro Schritt (versetzt um die halbe Korpuslaenge) und
    # den Verlust mitteln — halbiert das Gradienten-Rauschen, die
    # Checkpoint-Kurve wird glatt. Beide Vorwaertslaeufe landen auf dem
    # Autograd-Tape, rueckwaerts() geht durch beide.
    setze p2 auf p + 36071
    wenn p2 >= grenze:
        setze p2 auf p2 - grenze
    setze v1 auf kreuzentropie(modell(ids.zeilen(p, p + block)), ids.zeilen(p + 1, p + block + 1))
    setze v2 auf kreuzentropie(modell(ids.zeilen(p2, p2 + block)), ids.zeilen(p2 + 1, p2 + block + 1))
    setze verlust auf (v1 + v2) * 0.5
    verlust.rueckwaerts()
    gradienten_kappen(params, 1.0)   # gegen Loss-Spitzen (Attention!)
    setze opt["rate"] auf lernrate(s)
    opt.schritt()
    setze fenster_summe auf fenster_summe + verlust.zu_liste()[0]
    setze p auf p + sprung
    wenn p >= grenze:
        setze p auf p - grenze
    setze s auf s + 1
    wenn s - boden(s / fenster) * fenster == 0:
        setze mittel auf fenster_summe / fenster
        checkpoints.hinzufügen(mittel)
        zeige "Checkpoint " + text(länge(checkpoints)) + " (Schritt " + text(s) + "): mittlerer Verlust " + text(mittel)
        setze fenster_summe auf 0

# ------------------------------------------------------------
# 5. GATE: Verlust sinkt monoton ueber die Checkpoints
# ------------------------------------------------------------
setze monoton auf 1
setze c auf 1
solange c < länge(checkpoints):
    wenn checkpoints[c] >= checkpoints[c - 1]:
        setze monoton auf 0
    setze c auf c + 1

autograd_aus()
zeige ""
zeige "=== NACHHER (trainiert, Greedy — vergleichbar mit VORHER): ==="
zeige generiere(anfang, 150, 0)
zeige ""
zeige "=== NACHHER (Temperatur 0.8 — abwechslungsreicher): ==="
zeige generiere(anfang, 150, 0.8)

zeige ""
wenn monoton == 1:
    zeige "GATE BESTANDEN: Verlust sinkt monoton ueber alle Checkpoints."
sonst:
    zeige "GATE VERFEHLT: Verlust sinkt nicht monoton — Checkpoints oben pruefen."


funktion m6b_q_attention(a, h):
    setze i auf 0
    solange i < h:
        setze qk auf "wq" + text(i)
        setze kk auf "wk" + text(i)
        setze vk auf "wv" + text(i)
        a[qk] = m6b_int8_pc_dequant(a[qk])
        a[kk] = m6b_int8_pc_dequant(a[kk])
        a[vk] = m6b_int8_pc_dequant(a[vk])
        setze i auf i + 1
    a["wo"] = m6b_int8_pc_dequant(a["wo"])

# Feste, vom Training getrennte Zwei-Block-Evaluation.
setze ep2 auf 36071
wenn ep2 >= grenze:
    setze ep2 auf ep2 - grenze
setze eval_basis auf (kreuzentropie(modell(ids.zeilen(0, block)), ids.zeilen(1, block + 1)).zu_liste()[0] + kreuzentropie(modell(ids.zeilen(ep2, ep2 + block)), ids.zeilen(ep2 + 1, ep2 + block + 1)).zu_liste()[0]) * 0.5
# Perplexity = e^loss wird aus dem protokollierten Loss extern berechnet.

# Nur dicht/matmul-Gewichte: Attention-Projektionen, FFN und LM-Kopf.
m6b_q_attention(att1, koepfe)
m6b_q_attention(att2, koepfe)
ff1a["w"] = m6b_int8_pc_dequant(ff1a["w"])
ff1b["w"] = m6b_int8_pc_dequant(ff1b["w"])
ff2a["w"] = m6b_int8_pc_dequant(ff2a["w"])
ff2b["w"] = m6b_int8_pc_dequant(ff2b["w"])
kopf["w"] = m6b_int8_pc_dequant(kopf["w"])

setze eval_q auf (kreuzentropie(modell(ids.zeilen(0, block)), ids.zeilen(1, block + 1)).zu_liste()[0] + kreuzentropie(modell(ids.zeilen(ep2, ep2 + block)), ids.zeilen(ep2 + 1, ep2 + block + 1)).zu_liste()[0]) * 0.5
zeige "M6B_EVAL_LM_LOSS_BASELINE=" + text(eval_basis)
zeige "M6B_EVAL_LM_LOSS_INT8_PC=" + text(eval_q)
zeige "M6B_EVAL_LM_LOSS_DELTA=" + text(eval_q - eval_basis)
setze m6b_werte auf 98304 + 64 * vokab
setze m6b_skalen auf 1152 + vokab
zeige "M6B_EVAL_LM_WEIGHT_BYTES_F32=" + text(m6b_werte * 4)
zeige "M6B_EVAL_LM_WEIGHT_BYTES_INT8_PC=" + text(m6b_werte + m6b_skalen * 4)
