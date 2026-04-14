# ============================================================
# moo Chat-Server — HTTP Long-Polling + SQLite
# Multi-Room Chat mit persistenter History
#
# Kompilieren: moo-compiler compile chat_server.moo -o chat_server
# Starten:     ./chat_server
# Oeffnen:     http://localhost:4000
# ============================================================

# --- Datenbank initialisieren ---
setze db auf db_verbinde("sqlite:///tmp/moo_chat.db")
db_ausführen(db, "CREATE TABLE IF NOT EXISTS nachrichten (id INTEGER PRIMARY KEY AUTOINCREMENT, raum TEXT, nick TEXT, text TEXT, zeit REAL)")
db_ausführen(db, "CREATE INDEX IF NOT EXISTS idx_raum ON nachrichten(raum, id)")

# --- HTML laden ---
setze html auf datei_lesen("beispiele/chat/index.html")

# --- Aktive User pro Raum (im Speicher) ---
setze raeume auf {"allgemein": [], "moo-dev": [], "offtopic": []}

# --- Hilfsfunktionen (nutzen globale db) ---
funktion nachricht_speichern(raum, nick, nachricht_text):
    setze jetzt auf zeit()
    setze escaped_nick auf sql_bereinigen(nick)
    setze escaped_text auf sql_bereinigen(nachricht_text)
    setze escaped_raum auf sql_bereinigen(raum)
    setze sql auf "INSERT INTO nachrichten (raum, nick, text, zeit) VALUES ('" + escaped_raum + "', '" + escaped_nick + "', '" + escaped_text + "', " + text(jetzt) + ")"
    db_ausführen(db, sql)

funktion history_holen(raum, seit_id):
    setze escaped_raum auf sql_bereinigen(raum)
    setze sql auf "SELECT id, nick, text, zeit FROM nachrichten WHERE raum = '" + escaped_raum + "' AND id > " + text(seit_id) + " ORDER BY id LIMIT 50"
    setze zeilen auf db_abfrage(db, sql)
    gib_zurück zeilen

funktion json_antwort_liste(req, zeilen):
    setze liste auf []
    für zeile in zeilen:
        setze eintrag auf {"id": zeile["id"], "nick": zeile["nick"], "text": zeile["text"], "zeit": zeile["zeit"]}
        liste.hinzufügen(eintrag)
    web_json(req, {"messages": liste, "ok": wahr})

# --- Server starten ---
setze server auf web_server(4000)
zeige "=== moo Chat-Server ==="
zeige "http://localhost:4000"
zeige "Raeume: #allgemein, #moo-dev, #offtopic"
zeige ""

# --- Request Loop ---
solange wahr:
    setze req auf server.web_annehmen()
    wenn req == nichts:
        weiter

    setze pfad auf req["pfad"]
    setze methode auf req["methode"]
    setze body auf req["body"]

    # Startseite → HTML
    wenn pfad == "/":
        web_antworten(req, html, 200)
        weiter

    # Liste aller Raeume
    wenn pfad == "/api/rooms":
        web_json(req, {"rooms": ["allgemein", "moo-dev", "offtopic"]})
        weiter

    # Raum beitreten
    wenn pfad == "/api/join" und methode == "POST":
        setze body_json auf json_lesen(body)
        setze nick auf body_json["nick"]
        setze raum auf body_json["raum"]
        nachricht_speichern(raum, "*", nick + " ist dem Raum beigetreten")
        zeige nick + " → #" + raum
        web_json(req, {"ok": wahr, "nick": nick, "raum": raum})
        weiter

    # Nachricht senden
    wenn pfad == "/api/send" und methode == "POST":
        setze body_json auf json_lesen(body)
        setze nick auf body_json["nick"]
        setze raum auf body_json["raum"]
        setze nachricht auf body_json["text"]
        nachricht_speichern(raum, nick, nachricht)
        zeige "[" + raum + "] " + nick + ": " + nachricht
        web_json(req, {"ok": wahr})
        weiter

    # Long-Polling: Neue Nachrichten seit ID abfragen
    wenn pfad == "/api/poll":
        setze query auf req["query"]
        setze raum auf "allgemein"
        setze seit_id auf 0
        # Query-String parsen (primitiv: raum=X&seit=N)
        wenn query != nichts:
            setze teile auf query.teilen("&")
            für teil in teile:
                setze kv auf teil.teilen("=")
                wenn länge(kv) == 2:
                    wenn kv[0] == "raum":
                        setze raum auf kv[1]
                    wenn kv[0] == "seit":
                        setze seit_id auf zahl(kv[1])
        setze zeilen auf history_holen(raum, seit_id)
        json_antwort_liste(req, zeilen)
        weiter

    # History eines Raums: /api/history/RAUM → teilen liefert ["api","history","RAUM"]
    setze pfad_teile auf pfad.teilen("/")
    wenn länge(pfad_teile) >= 3 und pfad_teile[0] == "api" und pfad_teile[1] == "history":
        setze raum auf pfad_teile[2]
        setze zeilen auf history_holen(raum, 0)
        json_antwort_liste(req, zeilen)
        weiter

    # Statische Dateien
    wenn pfad == "/favicon.ico":
        web_antworten(req, "", 404)
        weiter

    # 404
    web_antworten(req, "Nicht gefunden", 404)
