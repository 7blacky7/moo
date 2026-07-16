# ============================================================
# Satz-Embeddings mit BPE + Mini-Transformer + InfoNCE
# (KI-MULTI-L1, deterministischer Toy-Demonstrator)
#
# Variable Satzlaengen werden in eine gemeinsame Token-Sequenz gelegt.
# Eine konstante Pooling-Matrix bildet danach den Mittelwert je Satz:
#     [batch, alle_tokens] @ [alle_tokens, dim] -> [batch, dim]
# Dadurch bleibt der komplette Pfad bis zum BPE-Embedding differenzierbar.
# ============================================================

setze links auf [
    "kleines haus",
    "schnelles auto",
    "froher mensch",
    "kalter morgen"
]
setze rechts auf [
    "winziges gebaeude",
    "flinker wagen",
    "glueckliche person",
    "kuehler tag"
]
setze batch auf länge(links)

# Byte-level BPE ist deterministisch und braucht keinen Seed.
setze korpus auf ""
für s in links:
    setze korpus auf korpus + s + " "
für s in rechts:
    setze korpus auf korpus + s + " "
setze tok auf tokenizer_trainiere(korpus, 280)
setze vokab auf tokenizer_info(tok)["vokab"]

# Baut Token-Ids und eine Mean-Pooling-Matrix fuer eine Satzliste.
funktion stapel_bauen(saetze):
    setze kodiert auf tokenizer_kodiere_stapel(tok, saetze)
    setze alle_ids auf []
    setze starts auf []
    setze laengen auf []

    für ids_t in kodiert:
        setze ids auf ids_t.zu_liste()
        starts.hinzufügen(länge(alle_ids))
        laengen.hinzufügen(länge(ids))
        für id in ids:
            alle_ids.hinzufügen(id)

    setze pool auf []
    setze i auf 0
    solange i < länge(saetze):
        setze zeile auf []
        setze j auf 0
        solange j < länge(alle_ids):
            wenn j >= starts[i] und j < starts[i] + laengen[i]:
                zeile.hinzufügen(1.0 / laengen[i])
            sonst:
                zeile.hinzufügen(0.0)
            setze j auf j + 1
        pool.hinzufügen(zeile)
        setze i auf i + 1

    gib_zurück [tensor_aus_liste(alle_ids), tensor_aus_liste(pool)]

setze a_stapel auf stapel_bauen(links)
setze b_stapel auf stapel_bauen(rechts)

# Ein kleiner geteilter Transformer-Encoder.
setze dim auf 16
setze emb auf schicht_embedding(vokab, dim, 11)
setze ln1 auf schicht_layernorm(dim)
setze att auf schicht_attention(dim, 2, 13)
setze ln2 auf schicht_layernorm(dim)
setze ff1 auf schicht_dicht(dim, 32, "gelu", 14)
setze ff2 auf schicht_dicht(32, dim, "keine", 15)
setze lnf auf schicht_layernorm(dim)
setze alle auf [emb, ln1, att, ln2, ff1, ff2, lnf]

funktion satz_embeddings(stapel):
    setze x auf vorwaerts([emb], stapel[0])
    setze x auf x + vorwaerts([ln1, att], x)
    setze x auf x + vorwaerts([ln2, ff1, ff2], x)
    setze x auf vorwaerts([lnf], x)
    gib_zurück stapel[1].matmul(x)

funktion genauigkeit_eins(a, b):
    setze richtig auf 0
    setze i auf 0
    solange i < batch:
        setze best auf -1
        setze best_wert auf -2.0
        setze j auf 0
        solange j < batch:
            setze ai auf a.zeilen(i, i + 1)
            setze bj auf b.zeilen(j, j + 1)
            setze wert auf kosinus(ai, bj).zu_liste()[0]
            wenn wert > best_wert:
                setze best_wert auf wert
                setze best auf j
            setze j auf j + 1
        wenn best == i:
            setze richtig auf richtig + 1
        setze i auf i + 1
    gib_zurück richtig / batch

setze opt auf optimierer_adam(parameter(alle), 0.02)
setze start auf 0.0
setze ende auf 0.0
setze schritt auf 0
solange schritt < 250:
    setze a auf satz_embeddings(a_stapel)
    setze b auf satz_embeddings(b_stapel)
    setze verlust auf kontrastiv(a, b, 0.12)
    wenn schritt == 0:
        setze start auf verlust.zu_liste()[0]
    wenn schritt == 249:
        setze ende auf verlust.zu_liste()[0]
    verlust.rueckwaerts()
    opt.schritt()
    setze schritt auf schritt + 1

autograd_aus()
setze a_eval auf satz_embeddings(a_stapel)
setze b_eval auf satz_embeddings(b_stapel)
setze acc auf genauigkeit_eins(a_eval, b_eval)
zeige "InfoNCE: " + text(start) + " -> " + text(ende)
zeige "Retrieval accuracy@1: " + text(acc) + " (Zufall: " + text(1.0 / batch) + ")"
wenn acc > 1.0 / batch:
    zeige "KI-EMBEDDINGS PASS"
sonst:
    zeige "KI-EMBEDDINGS FAIL"
