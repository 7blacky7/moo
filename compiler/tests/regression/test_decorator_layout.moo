# P011-A1 Regressionstest: Layout-Decorators mit Argumenten.
# @benutzt -> llvm.used, @sektion -> ELF-Section, @ausrichten -> Alignment.
# Funktional gepruefte Eigenschaft hier: Programm kompiliert + laeuft korrekt.
# (nm/readelf-Asserts laufen im manuellen A1-Check, nicht in run_all.)

@benutzt
@sektion(".moo_a1_sek")
@ausrichten(64)
funktion a1_marker():
    gib_zurück 42

zeige a1_marker()
