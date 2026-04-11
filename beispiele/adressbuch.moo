# moo Adressbuch — Kontakte in SQLite
# Starten: moo-compiler run beispiele/adressbuch.moo

zeige "=== moo Adressbuch ==="

# Datenbank oeffnen/erstellen
setze db auf db_verbinde("adressbuch.db")
db_ausführen(db, "CREATE TABLE IF NOT EXISTS kontakte (id INTEGER PRIMARY KEY, name TEXT, email TEXT, telefon TEXT)")

funktion kontakt_hinzufuegen(db):
    setze name auf eingabe("Name: ")
    setze email auf eingabe("Email: ")
    setze telefon auf eingabe("Telefon: ")
    db_ausführen(db, "INSERT INTO kontakte (name, email, telefon) VALUES ('" + name + "', '" + email + "', '" + telefon + "')")
    zeige "Kontakt hinzugefuegt!"

funktion kontakte_auflisten(db):
    setze ergebnis auf db_abfrage(db, "SELECT id, name, email, telefon FROM kontakte ORDER BY name")
    für kontakt in ergebnis:
        zeige kontakt

funktion kontakt_suchen(db):
    setze suchbegriff auf eingabe("Suche: ")
    setze ergebnis auf db_abfrage(db, "SELECT id, name, email FROM kontakte WHERE name LIKE '%" + suchbegriff + "%'")
    für kontakt in ergebnis:
        zeige kontakt

funktion kontakt_loeschen(db):
    setze id auf eingabe("ID: ")
    db_ausführen(db, "DELETE FROM kontakte WHERE id = " + id)
    zeige "Geloescht."

# Hauptmenue
solange wahr:
    zeige ""
    zeige "[1] Hinzufuegen [2] Auflisten [3] Suchen [4] Loeschen [5] Ende"
    setze wahl auf eingabe("> ")
    prüfe wahl:
        fall "1":
            kontakt_hinzufuegen(db)
        fall "2":
            kontakte_auflisten(db)
        fall "3":
            kontakt_suchen(db)
        fall "4":
            kontakt_loeschen(db)
        fall "5":
            db_schliessen(db)
            zeige "Tschuess!"
            stopp
        standard:
            zeige "1-5 eingeben."
