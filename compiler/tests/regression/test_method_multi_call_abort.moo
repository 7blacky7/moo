# Regression: 3+ Aufrufe einer User-Klassen-Methode auf derselben Instanz
# in der gleichen Funktion crashen mit 'malloc(): unaligned tcache chunk
# detected' (Exit 134, SIGABRT).
#
# Root-Cause-Verdacht: codegen Method-Call-Pfad released das obj (Receiver)
# zu oft - zum einen via call_args[0]-Lifecycle, zum anderen via obj_slot-
# Cleanup. Bei wiederholten Aufrufen in derselben Funktion akkumuliert sich
# der Refcount-Abbau auf der Instanz, der MooObject wird freed, naechster
# Aufruf hit corrupted heap.
#
# Bestaetigt 2026-05-04 als die Quelle des PgClient.frage()-Crashes - PgClient
# ruft selbst.empfange_eine_message() in einer Schleife.
#
# Erwartet: 3x 'A:hi'/'A:ho'/'A:hu', exit 0.
# Aktuell: SIGABRT nach Methoden-Aufruf 1-2.

klasse Foo:
    funktion erstelle():
        setze ignore auf 0
    funktion bla(s):
        gib_zurück "A:" + s

setze f auf neu Foo()
zeige(f.bla("hi"))
zeige(f.bla("ho"))
zeige(f.bla("hu"))
