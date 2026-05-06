# ============================================================
# moo Postgres-Client — Wire-Protocol v3 in pure moo
#
# Verbindet sich per TCP direkt mit einem Postgres-Server,
# implementiert den Startup-Handshake, sendet Simple Queries
# und parst die Binary-Responses (RowDescription + DataRow).
#
# Tests gegen lokalen Container 127.0.0.1:5433
#   user='mootest', database='mootest', trust-Auth
# ============================================================

# ------------------------------------------------------------
# Byte/Int Helfer
# ------------------------------------------------------------

funktion int32_be(n):
    # Big-Endian 4-Byte-Kodierung einer non-negativen int
    setze b0 auf boden(n / (256 * 256 * 256)) % 256
    setze b1 auf boden(n / (256 * 256)) % 256
    setze b2 auf boden(n / 256) % 256
    setze b3 auf n % 256
    gib_zurück [b0, b1, b2, b3]

funktion bytes_int16(liste, off):
    gib_zurück liste[off] * 256 + liste[off + 1]

funktion bytes_int32(liste, off):
    setze a auf liste[off] * 256 * 256 * 256
    setze b auf liste[off + 1] * 256 * 256
    setze c auf liste[off + 2] * 256
    setze d auf liste[off + 3]
    gib_zurück a + b + c + d

# Liest null-terminated C-String ab offset, gibt [text, neue_off] zurueck
funktion lies_cstr(liste, off):
    setze buchstaben auf []
    solange off < länge(liste) und liste[off] != 0:
        buchstaben.hinzufügen(liste[off])
        setze off auf off + 1
    # \0 ueberspringen
    setze off auf off + 1
    setze s auf bytes_neu(buchstaben)
    gib_zurück [s, off]

# Text in Liste<int> umwandeln
funktion text_zu_bytes(s):
    gib_zurück bytes_zu_liste(s)

# Liste anhängen (gibt neue Liste zurück fuers Fluent-Building)
funktion anhaengen_bytes(ziel, quelle):
    setze i auf 0
    solange i < länge(quelle):
        ziel.hinzufügen(quelle[i])
        setze i auf i + 1

# ------------------------------------------------------------
# Message-Klassen (Vererbung fuer unterschiedliche Request-Typen)
# ------------------------------------------------------------

klasse PgMessage:
    funktion erstelle():
        selbst.typ = "basis"

    funktion zu_bytes():
        gib_zurück []

klasse StartupMessage(PgMessage):
    funktion erstelle(user, datenbank):
        selbst.typ = "startup"
        selbst.user = user
        selbst.datenbank = datenbank

    funktion zu_bytes():
        # Payload: protocol + "user\0<user>\0database\0<db>\0\0"
        setze body auf []
        # Protocol-Version 3.0 = 196608 = 0x00030000
        anhaengen_bytes(body, int32_be(196608))
        anhaengen_bytes(body, text_zu_bytes("user"))
        body.hinzufügen(0)
        anhaengen_bytes(body, text_zu_bytes(selbst.user))
        body.hinzufügen(0)
        anhaengen_bytes(body, text_zu_bytes("database"))
        body.hinzufügen(0)
        anhaengen_bytes(body, text_zu_bytes(selbst.datenbank))
        body.hinzufügen(0)
        body.hinzufügen(0)
        # Gesamtlaenge inkl. Length-Feld selbst (4 bytes)
        setze gesamt auf 4 + länge(body)
        setze msg auf []
        anhaengen_bytes(msg, int32_be(gesamt))
        anhaengen_bytes(msg, body)
        gib_zurück msg

klasse QueryMessage(PgMessage):
    funktion erstelle(sql):
        selbst.typ = "query"
        selbst.sql = sql

    funktion zu_bytes():
        setze body auf []
        anhaengen_bytes(body, text_zu_bytes(selbst.sql))
        body.hinzufügen(0)
        setze gesamt auf 4 + länge(body)
        setze msg auf []
        msg.hinzufügen(81)  # 'Q'
        anhaengen_bytes(msg, int32_be(gesamt))
        anhaengen_bytes(msg, body)
        gib_zurück msg

klasse TerminateMessage(PgMessage):
    funktion erstelle():
        selbst.typ = "terminate"

    funktion zu_bytes():
        gib_zurück [88, 0, 0, 0, 4]  # 'X' + length=4

# ------------------------------------------------------------
# PgClient: Verbindung + Query-Loop
# ------------------------------------------------------------

klasse PgClient:
    funktion erstelle(host, port, user, datenbank):
        selbst.host = host
        selbst.port = port
        selbst.user = user
        selbst.datenbank = datenbank
        selbst.sock = nichts
        selbst.ready = falsch
        selbst.letzte_fehler = nichts
        selbst.server_version = ""
        selbst.backend_pid = 0

    funktion verbinde():
        selbst.sock = tcp_verbinde(selbst.host, selbst.port)
        setze sm auf neu StartupMessage(selbst.user, selbst.datenbank)
        selbst.sock.schreibe_bytes(sm.zu_bytes())
        selbst.empfange_bis_ready()
        gib_zurück selbst.ready

    # Liest 1 Message: typ(1) + length(4, inkl. self) + body(length-4)
    funktion empfange_eine_message():
        setze typ_b auf selbst.sock.lesen_bytes(1)
        wenn länge(typ_b) == 0:
            gib_zurück nichts
        setze typ auf typ_b[0]
        setze laenge_b auf selbst.sock.lesen_bytes(4)
        wenn länge(laenge_b) < 4:
            gib_zurück nichts
        setze laenge auf bytes_int32(laenge_b, 0)
        setze body_len auf laenge - 4
        setze body auf []
        wenn body_len > 0:
            # Moeglicherweise in mehreren Reads
            setze rest auf body_len
            solange rest > 0:
                setze chunk auf selbst.sock.lesen_bytes(rest)
                wenn länge(chunk) == 0:
                    # EOF
                    setze rest auf 0
                sonst:
                    anhaengen_bytes(body, chunk)
                    setze rest auf rest - länge(chunk)
        setze m auf {}
        m["typ"] = typ
        m["body"] = body
        gib_zurück m

    funktion empfange_bis_ready():
        setze laeuft auf wahr
        solange laeuft:
            setze m auf selbst.empfange_eine_message()
            wenn m == nichts:
                setze laeuft auf falsch
            sonst:
                setze t auf m["typ"]
                wenn t == 82:
                    # 'R' Authentication
                    setze code auf bytes_int32(m["body"], 0)
                    wenn code != 0:
                        selbst.letzte_fehler = "Authentication nicht trivial: " + text(code)
                        setze laeuft auf falsch
                sonst wenn t == 83:
                    # 'S' ParameterStatus
                    setze erg auf lies_cstr(m["body"], 0)
                    setze name auf erg[0]
                    setze erg2 auf lies_cstr(m["body"], erg[1])
                    setze wert auf erg2[0]
                    wenn name == "server_version":
                        setze selbst.server_version auf wert
                sonst wenn t == 75:
                    # 'K' BackendKeyData
                    selbst.backend_pid = bytes_int32(m["body"], 0)
                sonst wenn t == 90:
                    # 'Z' ReadyForQuery
                    selbst.ready = wahr
                    setze laeuft auf falsch
                sonst wenn t == 69:
                    # 'E' ErrorResponse
                    setze selbst.letzte_fehler auf selbst.parse_fehler(m["body"])
                    setze laeuft auf falsch
                sonst wenn t == 78:
                    # 'N' NoticeResponse — ignorieren
                    setze ignore auf 0

    funktion parse_fehler(body):
        setze off auf 0
        setze meldung auf ""
        solange off < länge(body) und body[off] != 0:
            setze feld_typ auf body[off]
            setze off auf off + 1
            setze erg auf lies_cstr(body, off)
            setze inhalt auf erg[0]
            setze off auf erg[1]
            wenn feld_typ == 77:
                # 'M' Message
                setze meldung auf inhalt
        gib_zurück meldung

    funktion parse_row_description(body):
        setze felder auf []
        setze n auf bytes_int16(body, 0)
        setze off auf 2
        setze i auf 0
        solange i < n:
            setze erg auf lies_cstr(body, off)
            setze name auf erg[0]
            setze off auf erg[1]
            # table_oid(4) + col_attr(2) + type_oid(4) + type_size(2) + type_mod(4) + format(2) = 18 Bytes
            setze off auf off + 18
            felder.hinzufügen(name)
            setze i auf i + 1
        gib_zurück felder

    funktion parse_data_row(body, spalten):
        setze n auf bytes_int16(body, 0)
        setze off auf 2
        setze row auf {}
        setze i auf 0
        solange i < n:
            setze laenge auf bytes_int32(body, off)
            setze off auf off + 4
            wenn laenge == -1:
                row[spalten[i]] = nichts
            sonst:
                setze wert_bytes auf []
                setze j auf 0
                solange j < laenge:
                    wert_bytes.hinzufügen(body[off + j])
                    setze j auf j + 1
                row[spalten[i]] = bytes_neu(wert_bytes)
                setze off auf off + laenge
            setze i auf i + 1
        gib_zurück row

    # Simple Query — liefert Dict mit "rows", "spalten", "anzahl", "fehler"
    funktion query(sql):
        setze q auf neu QueryMessage(sql)
        selbst.sock.schreibe_bytes(q.zu_bytes())
        setze ergebnis auf {}
        ergebnis["spalten"] = []
        ergebnis["rows"] = []
        ergebnis["anzahl"] = 0
        ergebnis["fehler"] = nichts
        ergebnis["tag"] = ""
        setze spalten auf []
        setze laeuft auf wahr
        solange laeuft:
            setze m auf selbst.empfange_eine_message()
            wenn m == nichts:
                setze laeuft auf falsch
            sonst:
                setze t auf m["typ"]
                wenn t == 84:
                    # 'T' RowDescription
                    setze spalten auf selbst.parse_row_description(m["body"])
                    ergebnis["spalten"] = spalten
                sonst wenn t == 68:
                    # 'D' DataRow
                    setze row auf selbst.parse_data_row(m["body"], spalten)
                    ergebnis["rows"].hinzufügen(row)
                sonst wenn t == 67:
                    # 'C' CommandComplete
                    setze erg auf lies_cstr(m["body"], 0)
                    ergebnis["tag"] = erg[0]
                sonst wenn t == 73:
                    # 'I' EmptyQueryResponse — ignorieren
                    setze ignore auf 0
                sonst wenn t == 69:
                    # 'E' ErrorResponse
                    ergebnis["fehler"] = selbst.parse_fehler(m["body"])
                sonst wenn t == 90:
                    # 'Z' ReadyForQuery
                    setze laeuft auf falsch
                sonst wenn t == 78:
                    # 'N' NoticeResponse ignorieren
                    setze ignore auf 0
        ergebnis["anzahl"] = länge(ergebnis["rows"])
        gib_zurück ergebnis

    funktion schliesse():
        wenn selbst.sock != nichts:
            setze tm auf neu TerminateMessage()
            selbst.sock.schreibe_bytes(tm.zu_bytes())
            selbst.sock.schliessen()
            setze selbst.sock auf nichts

# ------------------------------------------------------------
# Test-Framework
# ------------------------------------------------------------

setze zaehler auf {}
zaehler["gesamt"] = 0
zaehler["ok"] = 0

funktion check(name, bedingung):
    zaehler["gesamt"] = zaehler["gesamt"] + 1
    wenn bedingung:
        zaehler["ok"] = zaehler["ok"] + 1
        zeige "  OK    " + name
    sonst:
        zeige "  FAIL  " + name

# ------------------------------------------------------------
# Tests
# ------------------------------------------------------------

zeige "================================================"
zeige "  moo Postgres-Client — Wire-Protocol Tests"
zeige "================================================"

zeige ""
zeige "--- Test 1: Verbinden ---"
setze db auf neu PgClient("192.168.50.65", 5433, "moo_bench", "moo_bench")
db.verbinde()
check("ready nach Handshake", db.ready)
check("backend_pid > 0", db.backend_pid > 0)
zeige "  server_version: " + db.server_version
zeige "  backend_pid: " + text(db.backend_pid)

zeige ""
zeige "--- Test 2: SELECT 1 ---"
setze r auf db.query("SELECT 1 AS eins")
check("SELECT 1: kein fehler", r["fehler"] == nichts)
check("SELECT 1: 1 spalte", länge(r["spalten"]) == 1)
check("SELECT 1: spaltenname=eins", r["spalten"][0] == "eins")
check("SELECT 1: 1 row", r["anzahl"] == 1)
check("SELECT 1: wert=1", r["rows"][0]["eins"] == "1")

zeige ""
zeige "--- Test 3: DROP TABLE IF EXISTS + CREATE ---"
setze r auf db.query("DROP TABLE IF EXISTS moo_users")
check("DROP TABLE: kein fehler", r["fehler"] == nichts)

setze r auf db.query("CREATE TABLE moo_users (id SERIAL PRIMARY KEY, name TEXT, age INT)")
check("CREATE TABLE: kein fehler", r["fehler"] == nichts)

zeige ""
zeige "--- Test 4: INSERT 5 rows ---"
setze r auf db.query("INSERT INTO moo_users (name, age) VALUES ('Alice', 30)")
check("INSERT Alice", r["fehler"] == nichts)
setze r auf db.query("INSERT INTO moo_users (name, age) VALUES ('Bob', 25)")
check("INSERT Bob", r["fehler"] == nichts)
setze r auf db.query("INSERT INTO moo_users (name, age) VALUES ('Claire', 40)")
check("INSERT Claire", r["fehler"] == nichts)
setze r auf db.query("INSERT INTO moo_users (name, age) VALUES ('Dave', 22)")
check("INSERT Dave", r["fehler"] == nichts)
setze r auf db.query("INSERT INTO moo_users (name, age) VALUES ('Eve', 35)")
check("INSERT Eve", r["fehler"] == nichts)

zeige ""
zeige "--- Test 5: SELECT all ---"
setze r auf db.query("SELECT id, name, age FROM moo_users ORDER BY id")
check("SELECT all: kein fehler", r["fehler"] == nichts)
check("SELECT all: 5 rows", r["anzahl"] == 5)
check("SELECT all: 3 spalten", länge(r["spalten"]) == 3)
check("SELECT all: erste row Alice", r["rows"][0]["name"] == "Alice")
check("SELECT all: age Bob = 25", r["rows"][1]["age"] == "25")
check("SELECT all: letzte row Eve", r["rows"][4]["name"] == "Eve")

zeige ""
zeige "--- Test 6: SELECT WHERE ---"
setze r auf db.query("SELECT name FROM moo_users WHERE age > 25 ORDER BY name")
check("WHERE: kein fehler", r["fehler"] == nichts)
check("WHERE: 3 rows", r["anzahl"] == 3)
check("WHERE: erster=Alice", r["rows"][0]["name"] == "Alice")
check("WHERE: letzter=Eve", r["rows"][2]["name"] == "Eve")

zeige ""
zeige "--- Test 7: UPDATE ---"
setze r auf db.query("UPDATE moo_users SET age = 31 WHERE name = 'Alice'")
check("UPDATE: kein fehler", r["fehler"] == nichts)
setze r auf db.query("SELECT age FROM moo_users WHERE name = 'Alice'")
check("UPDATE check: Alice age=31", r["rows"][0]["age"] == "31")

zeige ""
zeige "--- Test 8: DELETE ---"
setze r auf db.query("DELETE FROM moo_users WHERE name = 'Dave'")
check("DELETE: kein fehler", r["fehler"] == nichts)
setze r auf db.query("SELECT COUNT(*) AS n FROM moo_users")
check("DELETE check: count=4", r["rows"][0]["n"] == "4")

zeige ""
zeige "--- Test 9: Fehler-Handling (invalid SQL) ---"
setze r auf db.query("SELECT * FROM tabelle_existiert_nicht")
check("Invalid SQL: fehler gesetzt", r["fehler"] != nichts)
zeige "  Fehlermeldung: " + text(r["fehler"])
# Client muss nach Fehler weiter funktionieren (ReadyForQuery kam)
setze r auf db.query("SELECT 42 AS antwort")
check("Nach Fehler: naechster Query geht", r["rows"][0]["antwort"] == "42")

zeige ""
zeige "--- Test 10: Cleanup ---"
setze r auf db.query("DROP TABLE moo_users")
check("DROP: kein fehler", r["fehler"] == nichts)

db.schliesse()
zeige "  Verbindung geschlossen."

zeige ""
zeige "================================================"
zeige "  Ergebnis: " + text(zaehler["ok"]) + "/" + text(zaehler["gesamt"]) + " bestanden"
zeige "================================================"
