setze db auf db_verbinde("sqlite://memory")
db_ausführen(db, "CREATE TABLE u (name TEXT)")
db_ausführen_mit_params(db, "INSERT INTO u (name) VALUES (?)", ["Anna"])
# Injection-Versuch als Parameter — darf KEINEN Effekt haben:
setze evil auf "'; DROP TABLE u; --"
db_ausführen_mit_params(db, "INSERT INTO u (name) VALUES (?)", [evil])
# Tabelle sollte noch existieren + beide Zeilen enthalten:
setze r auf db_abfrage(db, "SELECT name FROM u")
zeige r
db_schliessen(db)
