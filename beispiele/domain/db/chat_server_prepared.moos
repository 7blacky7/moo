# ============================================================
# moo Chat-Server — Prepared-Statement-Variante (neue API).
#
# Zeigt die additive Prio3c-API gegenueber chat_server.moo:
#   - db_abfrage_mit_params / db_ausführen_mit_params statt sql_bereinigen +
#     String-Konkatenation
#   - db_vorbereite fuer wiederholte Inserts (nachricht_speichern)
#
# Kompilieren: moo-compiler compile chat_server_prepared.moo -o chat_server_p
# Starten:     ./chat_server_p   (Port 4001 — parallel zu Original moeglich)
# ============================================================

setze db auf db_verbinde("sqlite:///tmp/moo_chat_prepared.db")
db_ausführen(db, "CREATE TABLE IF NOT EXISTS nachrichten (id INTEGER PRIMARY KEY AUTOINCREMENT, raum TEXT, nick TEXT, text TEXT, zeit REAL)")
db_ausführen(db, "CREATE INDEX IF NOT EXISTS idx_raum ON nachrichten(raum, id)")

# Prepared Statement fuer Insert (1x kompiliert, beliebig oft binden).
setze INSERT_STMT auf db_vorbereite(db, "INSERT INTO nachrichten (raum, nick, text, zeit) VALUES (:raum, :nick, :text, :zeit)")

setze html auf datei_lesen("beispiele/chat/index.html")

funktion nachricht_speichern(raum, nick, nachricht_text):
    INSERT_STMT.binde(":raum", raum)
    INSERT_STMT.binde(":nick", nick)
    INSERT_STMT.binde(":text", nachricht_text)
    INSERT_STMT.binde(":zeit", zeit())
    INSERT_STMT.ausfuehren()

funktion history_holen(raum, seit_id):
    gib_zurück db_abfrage_mit_params(db, "SELECT id, nick, text, zeit FROM nachrichten WHERE raum = ? AND id > ? ORDER BY id LIMIT 50", [raum, seit_id])

funktion json_antwort_liste(req, zeilen):
    setze liste auf []
    für zeile in zeilen:
        setze eintrag auf {"id": zeile["id"], "nick": zeile["nick"], "text": zeile["text"], "zeit": zeile["zeit"]}
        liste.hinzufügen(eintrag)
    web_json(req, {"messages": liste, "ok": wahr})

setze server auf web_server(4001)
zeige "=== moo Chat-Server (prepared) auf Port 4001 ==="

solange wahr:
    setze req auf server.web_annehmen()
    wenn req == nichts:
        weiter

    setze pfad auf req["pfad"]
    setze methode auf req["methode"]
    setze body auf req["body"]

    wenn pfad == "/":
        web_antworten(req, html, 200)
        weiter

    wenn pfad == "/api/rooms":
        web_json(req, {"rooms": ["allgemein", "moo-dev", "offtopic"]})
        weiter

    wenn pfad == "/api/join" und methode == "POST":
        setze body_json auf json_lesen(body)
        nachricht_speichern(body_json["raum"], "*", body_json["nick"] + " ist dem Raum beigetreten")
        web_json(req, {"ok": wahr, "nick": body_json["nick"], "raum": body_json["raum"]})
        weiter

    wenn pfad == "/api/send" und methode == "POST":
        setze body_json auf json_lesen(body)
        nachricht_speichern(body_json["raum"], body_json["nick"], body_json["text"])
        web_json(req, {"ok": wahr})
        weiter

    wenn pfad == "/api/poll":
        setze query auf req["query"]
        setze raum auf "allgemein"
        setze seit_id auf 0
        wenn query != nichts:
            setze teile auf query.teilen("&")
            für teil in teile:
                setze kv auf teil.teilen("=")
                wenn länge(kv) == 2:
                    wenn kv[0] == "raum":
                        setze raum auf kv[1]
                    wenn kv[0] == "seit":
                        setze seit_id auf zahl(kv[1])
        json_antwort_liste(req, history_holen(raum, seit_id))
        weiter

    setze pfad_teile auf pfad.teilen("/")
    wenn länge(pfad_teile) >= 3 und pfad_teile[0] == "api" und pfad_teile[1] == "history":
        json_antwort_liste(req, history_holen(pfad_teile[2], 0))
        weiter

    wenn pfad == "/favicon.ico":
        web_antworten(req, "", 404)
        weiter

    web_antworten(req, "Nicht gefunden", 404)
