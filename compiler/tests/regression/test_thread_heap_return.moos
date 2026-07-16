# Regression: Ein Thread gibt einen Heap-Wert (String) zurueck und die
# Thread-Variable laeuft danach aus dem Scope. warten() MUSS eine eigene
# owning-Referenz liefern (INV-1), sonst geben Aufrufer-Scope-Exit UND
# moo_thread_free denselben String frei → heap-use-after-free.
# (ASan-bestaetigt; Fix: moo_retain(t->result) in moo_thread_wait.)

funktion mach_text(x):
    gib_zurück "ergebnis-" + x

funktion lauf(n):
    setze t auf starte(mach_text, n)
    setze r auf t.warten()
    zeige r

setze i auf 0
solange i < 5:
    lauf(text(i))
    setze i auf i + 1
