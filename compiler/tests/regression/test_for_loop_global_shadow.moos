# Regression P016-M1: Ein For-Binder in einer Funktion darf ein
# gleichnamiges globales Dict nicht ueberschreiben oder releasen.
setze k auf {}
k["marker"] = "global"

funktion pruefe_loop_scope():
    setze werte auf [1, 2, 3]
    setze letzter auf 0
    für k in werte:
        setze letzter auf k
    gib_zurück letzter

zeige pruefe_loop_scope()
zeige k["marker"]
