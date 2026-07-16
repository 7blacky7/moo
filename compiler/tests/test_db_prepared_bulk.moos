# Bulk-Insert 1000 rows — Statement bleibt geparst, jede Iteration nur bind+step+reset
setze db auf db_verbinde("sqlite://memory")
db_ausführen(db, "CREATE TABLE bulk (id INTEGER, v TEXT)")
db_ausführen(db, "BEGIN")
setze s auf db_vorbereite(db, "INSERT INTO bulk (id, v) VALUES (?, ?)")
setze i auf 0
solange i < 1000:
    s.binde(1, i)
    s.binde(2, "row_" + text(i))
    s.ausfuehren()
    i += 1
db_ausführen(db, "COMMIT")
s.schliessen()
setze cnt auf db_abfrage(db, "SELECT COUNT(*) AS n FROM bulk")
zeige cnt
setze first auf db_abfrage(db, "SELECT id, v FROM bulk WHERE id = 0")
zeige first
setze last auf db_abfrage(db, "SELECT id, v FROM bulk WHERE id = 999")
zeige last
db_schliessen(db)
