funktion handler(prefix, k, c, s, a):
    zeige(prefix + " | k=" + k + " c=" + text(c) + " s=" + text(s) + " a=" + text(a))

funktion factory(prefix):
    setze cb auf (k, c, s, a) => handler(prefix, k, c, s, a)
    gib_zurück cb

setze fn1 auf factory("ALPHA")
setze fn2 auf factory("BETA")
fn1("k1", wahr, falsch, falsch)
fn1("k2", wahr, falsch, falsch)
fn1("k3", wahr, falsch, falsch)
fn2("m1", falsch, wahr, falsch)
fn2("m2", falsch, wahr, falsch)
