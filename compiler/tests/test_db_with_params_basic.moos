setze db auf db_verbinde("sqlite://memory")
db_ausführen(db, "CREATE TABLE u (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)")
setze n auf db_ausführen_mit_params(db, "INSERT INTO u (name, age) VALUES (?, ?)", ["Anna", 25])
zeige n
setze n2 auf db_ausführen_mit_params(db, "INSERT INTO u (name, age) VALUES (?, ?)", ["Bob", 30])
zeige n2
setze r auf db_abfrage_mit_params(db, "SELECT id, name, age FROM u WHERE age > ?", [26])
zeige r
db_schliessen(db)
