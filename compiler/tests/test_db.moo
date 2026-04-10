setze db auf db_verbinde("sqlite:///tmp/moo_test.db")
db_ausführen(db, "CREATE TABLE IF NOT EXISTS tiere (id INTEGER PRIMARY KEY, name TEXT, laut TEXT)")
db_ausführen(db, "INSERT INTO tiere VALUES (1, 'Rex', 'Wuff')")
db_ausführen(db, "INSERT INTO tiere VALUES (2, 'Mimi', 'Miau')")
setze tiere auf db_abfrage(db, "SELECT * FROM tiere")
zeige tiere
für tier in tiere:
    zeige f"{tier['name']} macht {tier['laut']}"
db_schliessen(db)
datei_löschen("/tmp/moo_test.db")
zeige "DB OK"
