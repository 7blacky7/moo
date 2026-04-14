# BUG 2 TEST: Diamond-Import darf keine doppelten Funktionsdefs erzeugen
# VORHER: Hängt oder crashed bei forward_declare (doppelte LLVM-Funktionen)
# NACHHER: Korrekte Ausgabe
importiere test_import_diamond_a
importiere test_import_diamond_b

zeige nutze_a(5)
zeige nutze_b(5)
