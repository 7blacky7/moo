# P014-A3: Tensor-Builtins end-to-end (Operatoren + Methoden, DE/EN).
# Deterministisch — zufall nur ueber Seed-Determinismus-Vergleich.
setze a auf tensor_aus_liste([[1, 2], [3, 4]])
setze b auf tensor_einsen([2, 2])
zeige a + b
zeige a - b
zeige a * 2
zeige 10 - a
zeige a / b
zeige -a
zeige a ** 2
zeige a.matmul(tensor_aus_liste([[1, 0], [0, 1]]))
zeige a.transponieren()
zeige a.transpose()
zeige a.umformen([4])
zeige a.summe()
zeige a.summe(0)
zeige a.summe(1)
zeige a.mittel()
zeige a.max_wert()
zeige a.relu()
zeige a.sigmoid().form()
zeige a.softmax()
zeige a.logsoftmax().exp()
zeige a.form()
zeige a.größe()
zeige a.zu_liste()
zeige a.wert([1, 0])
a.wert_setzen([0, 0], 9)
zeige a.wert([0, 0])
zeige a.zeilen(0, 1)
zeige a.verketten(b).form()
setze z1 auf tensor_zufall([2, 3], 7)
setze z2 auf tensor_random([2, 3], 7)
zeige (z1 - z2).summe()
zeige tensor([2], 5).zu_liste()
zeige tensor_zeros([2]).zu_liste()
zeige tensor_ones([2]).zu_liste()
zeige tensor_from_list([1, 2]).größe()
