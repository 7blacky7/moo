setze db auf db_verbinde("sqlite://memory")
db_ausführen(db, "CREATE TABLE t (a TEXT, b INTEGER, c REAL, d TEXT)")
db_ausführen_mit_params(db, "INSERT INTO t VALUES (?, ?, ?, ?)", ["hi", 42, 3.14, nichts])
setze r auf db_abfrage_mit_params(db, "SELECT a, b, c, d FROM t WHERE a = ?", ["hi"])
zeige r
db_schliessen(db)
