# Kern-Builtins Test

# zahl() — String zu Zahl
setze x auf zahl("42")
zeige x + 8

setze pi auf zahl("3.14159")
zeige pi

# umgebung() — Umgebungsvariable lesen
setze home auf umgebung("HOME")
zeige home

# argumente() — CLI-Argumente
setze args auf argumente()
zeige args

# schlafe() — kurz pausieren
schlafe(0.1)
zeige "Aufgewacht!"
