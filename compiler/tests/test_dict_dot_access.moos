# Regressionstest: Dot-Access auf Dicts ist aequivalent zu Bracket-Access.
# Vorher (Bug): g.key segfaultete, wenn das Dict pointer-tagged MooValues
# enthielt (zB UI-Handles). moo_object_get castete MooDict* zu MooObject*.
# Fix: moo_object_get/set dispatcht jetzt bei MOO_DICT an moo_dict_get/set.

funktion verdopple(x):
    gib_zurück x * 2

setze g auf {}

# 1) set via bracket, get via dot
g["a"] = 42
zeige g.a

# 2) set via dot, get via bracket
g.b = 99
zeige g["b"]

# 3) set via dot, get via dot (round-trip)
g.c = "hallo"
zeige g.c

# 4) String-MooValue per Dot
g.name = "moo"
zeige g.name

# 5) Liste (heap-ptr MooValue) per Dot set, Bracket get
g.items = [1, 2, 3]
zeige g["items"]

# 6) Funktionswert (MOO_FUNC, pointer-tagged) per Dot set/get
g.callback = verdopple
setze f auf g.callback
zeige f(21)

# 7) Ueberschreiben via Dot
g.a = 100
zeige g.a

# 8) Mehrere Round-Trips, keine Crashes
setze i auf 0
solange i < 5:
    g["counter"] = i
    zeige g.counter
    setze i auf i + 1
