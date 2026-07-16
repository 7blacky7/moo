# KI-MULTI-V2: kleines, deterministisches MNIST-artiges Conv-Gate.
# Lokale Striche werden an verschiedenen Positionen erkannt.
# Erwartet: SELFTEST_RESULT: PASS ki_mnist_conv

funktion bild(art, versatz):
    setze p auf []
    setze y auf 0
    solange y < 4:
        setze x auf 0
        solange x < 4:
            setze an auf 0
            wenn art == 0 und x == versatz:
                setze an auf 1
            wenn art == 1 und y == versatz:
                setze an auf 1
            p.hinzufügen(an)
            setze x auf x + 1
        setze y auf y + 1
    gib_zurück p

setze train_x auf []
setze train_y auf []
setze test_x auf []
setze test_y auf []
setze art auf 0
solange art < 2:
    setze v auf 0
    solange v < 3:
        train_x.hinzufügen(bild(art, v))
        wenn art == 0:
            train_y.hinzufügen([1, 0])
        sonst:
            train_y.hinzufügen([0, 1])
        setze v auf v + 1
    test_x.hinzufügen(bild(art, 3))
    wenn art == 0:
        test_y.hinzufügen([1, 0])
    sonst:
        test_y.hinzufügen([0, 1])
    setze art auf art + 1

setze tx_flach auf tensor_aus_liste(train_x)
setze px_flach auf tensor_aus_liste(test_x)
setze tx auf tx_flach.umformen([6, 4, 4, 1])
setze px auf px_flach.umformen([2, 4, 4, 1])
setze ty auf tensor_aus_liste(train_y)
setze py auf tensor_aus_liste(test_y)
setze epochen auf 80

setze conv auf ki_netz([
    schicht_faltung(1, 4, 3, 1, 1, "relu", 17),
    schicht_pooling("max", 2, 2),
    schicht_flach(),
    schicht_dicht(16, 2, "softmax", 18)
])
setze dicht auf ki_netz([
    schicht_dicht(16, 2, "softmax", 18)
])

conv.trainiere(tx, ty, {"epochen": epochen, "rate": 0.03, "batch": 6, "seed": 7, "ausgabe": 0})
dicht.trainiere(tx_flach, ty, {"epochen": epochen, "rate": 0.03, "batch": 6, "seed": 7, "ausgabe": 0})
setze conv_genau auf conv.genauigkeit(px, py)
setze dicht_genau auf dicht.genauigkeit(px_flach, py)
zeige "Conv: " + text(conv_genau) + " Dense: " + text(dicht_genau)
wenn conv_genau > dicht_genau:
    zeige "SELFTEST_RESULT: PASS ki_mnist_conv"
sonst:
    zeige "SELFTEST_RESULT: FAIL ki_mnist_conv"
