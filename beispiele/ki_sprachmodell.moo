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
# SGD mit Momentum statt Adam — BEWUSST: Adam zeigt bei diesem Aufbau
# (batch=1-Bloecke, winziges Modell) einen reproduzierbaren Verlust-Buckel
# mitten im Training, SGD nicht. Fuer XOR/MNIST (trainiere-API mit echten
# Batches) bleibt Adam die richtige Wahl.
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
