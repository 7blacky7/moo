# ============================================================
# moo Postgres-Client — Wire-Protocol v3 in pure moo
#
# Verbindet sich per TCP direkt mit einem Postgres-Server,
# implementiert den Startup-Handshake, sendet Simple Queries
# und parst die Binary-Responses (RowDescription + DataRow).
#
# Auth: trust (passwort="") oder SCRAM-SHA-256 (RFC 5802 / RFC 7677).
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

funktion text_zu_bytes(s):
    gib_zurück bytes_zu_liste(s)

funktion anhaengen_bytes(ziel, quelle):
    setze i auf 0
    solange i < länge(quelle):
        ziel.hinzufügen(quelle[i])
        setze i auf i + 1

# ------------------------------------------------------------
# Message-Klassen
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
        setze body auf []
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
# SCRAM-SHA-256 Helpers
# ------------------------------------------------------------

# Slice einer Byte-Liste [von, bis) -> neue Liste
funktion bytes_slice(liste, start, ende):
    setze out auf []
    setze i auf start
    solange i < ende:
        out.hinzufügen(liste[i])
        setze i auf i + 1
    gib_zurück out

# Parst "key=val,key=val,..." in Dict
funktion scram_parse_attrs(s):
    setze attrs auf {}
    setze teile auf s.teilen(",")
    setze i auf 0
    solange i < länge(teile):
        setze paar auf teile[i]
        setze pl auf bytes_zu_liste(paar)
        setze pos auf 0 - 1
        setze j auf 0
        solange j < länge(pl) und pos < 0:
            wenn pl[j] == 61:
                setze pos auf j
            setze j auf j + 1
        wenn pos > 0:
            setze k auf paar.teilstring(0, pos)
            setze v auf paar.teilstring(pos + 1, länge(paar))
            attrs[k] = v
        setze i auf i + 1
    gib_zurück attrs

# XOR zweier gleichlanger Raw-Byte-Strings
funktion scram_bytes_xor(a, b):
    setze al auf bytes_zu_liste(a)
    setze bl auf bytes_zu_liste(b)
    setze out auf []
    setze i auf 0
    solange i < länge(al):
        out.hinzufügen(al[i] ^ bl[i])
        setze i auf i + 1
    gib_zurück bytes_neu(out)

# SASLInitialResponse: 'p' + len + mechanism + \0 + int32(len(client_first)) + client_first
klasse SaslInitialMessage(PgMessage):
    funktion erstelle(mechanism, client_first):
        selbst.typ = "sasl_init"
        selbst.mechanism = mechanism
        selbst.client_first = client_first

    funktion zu_bytes():
        setze body auf []
        anhaengen_bytes(body, text_zu_bytes(selbst.mechanism))
        body.hinzufügen(0)
        setze cf_bytes auf text_zu_bytes(selbst.client_first)
        anhaengen_bytes(body, int32_be(länge(cf_bytes)))
        anhaengen_bytes(body, cf_bytes)
        setze gesamt auf 4 + länge(body)
        setze msg auf []
        msg.hinzufügen(112)
        anhaengen_bytes(msg, int32_be(gesamt))
        anhaengen_bytes(msg, body)
        gib_zurück msg

# SASLResponse: 'p' + len + client_final
klasse SaslResponseMessage(PgMessage):
    funktion erstelle(client_final):
        selbst.typ = "sasl_response"
        selbst.client_final = client_final

    funktion zu_bytes():
        setze body auf text_zu_bytes(selbst.client_final)
        setze gesamt auf 4 + länge(body)
        setze msg auf []
        msg.hinzufügen(112)
        anhaengen_bytes(msg, int32_be(gesamt))
        anhaengen_bytes(msg, body)
        gib_zurück msg

# ------------------------------------------------------------
# PgClient
# ------------------------------------------------------------

klasse PgClient:
    funktion erstelle(host, port, user, datenbank, passwort):
        selbst.host = host
        selbst.port = port
        selbst.user = user
        selbst.datenbank = datenbank
        selbst.passwort = passwort
        selbst.sock = nichts
        selbst.ready = falsch
        selbst.letzte_fehler = nichts
        selbst.server_version = ""
        selbst.backend_pid = 0
        # SCRAM-State waehrend SASL-Handshake
        selbst.scram_client_nonce = ""
        selbst.scram_client_first_bare = ""
        selbst.scram_auth_msg = ""
        selbst.scram_expected_server_sig = ""
        selbst.scram_server_verified = falsch

    funktion scram_send_initial():
        setze nonce auf sichere_zufall(18)
        setze bare auf "n=" + selbst.user + ",r=" + nonce
        setze cf_msg auf "n,," + bare
        selbst.scram_client_nonce = nonce
        selbst.scram_client_first_bare = bare
        setze m auf neu SaslInitialMessage("SCRAM-SHA-256", cf_msg)
        selbst.sock.schreibe_bytes(m.zu_bytes())

    funktion scram_send_final(server_first):
        setze attrs auf scram_parse_attrs(server_first)
        wenn nicht attrs.enthält("r") oder nicht attrs.enthält("s") oder nicht attrs.enthält("i"):
            selbst.letzte_fehler = "SCRAM: server-first unvollstaendig"
            gib_zurück falsch
        setze full_nonce auf attrs["r"]
        setze salt_b64 auf attrs["s"]
        setze iter auf zahl(attrs["i"])
        setze cn auf selbst.scram_client_nonce
        wenn länge(full_nonce) < länge(cn) oder full_nonce.teilstring(0, länge(cn)) != cn:
            selbst.letzte_fehler = "SCRAM: server nonce mismatch"
            gib_zurück falsch
        setze salt auf base64_decode(salt_b64)
        setze salted auf pbkdf2_sha256(selbst.passwort, salt, iter, 32)
        setze client_key auf hmac_sha256(salted, "Client Key")
        setze stored_key auf sha256_bytes(client_key)
        setze cf_no_proof auf "c=biws,r=" + full_nonce
        setze auth_msg auf selbst.scram_client_first_bare + "," + server_first + "," + cf_no_proof
        selbst.scram_auth_msg = auth_msg
        setze client_sig auf hmac_sha256(stored_key, auth_msg)
        setze proof auf scram_bytes_xor(client_key, client_sig)
        setze proof_b64 auf base64_encode(proof)
        setze client_final auf cf_no_proof + ",p=" + proof_b64
        setze server_key auf hmac_sha256(salted, "Server Key")
        selbst.scram_expected_server_sig = hmac_sha256(server_key, auth_msg)
        setze m auf neu SaslResponseMessage(client_final)
        selbst.sock.schreibe_bytes(m.zu_bytes())
        gib_zurück wahr

    funktion scram_verify_final(server_final):
        setze attrs auf scram_parse_attrs(server_final)
        wenn attrs.enthält("e"):
            selbst.letzte_fehler = "SCRAM server-error: " + attrs["e"]
            gib_zurück falsch
        wenn nicht attrs.enthält("v"):
            selbst.letzte_fehler = "SCRAM server-final ohne v="
            gib_zurück falsch
        setze got auf base64_decode(attrs["v"])
        wenn got != selbst.scram_expected_server_sig:
            selbst.letzte_fehler = "SCRAM: ServerSignature mismatch"
            gib_zurück falsch
        selbst.scram_server_verified = wahr
        gib_zurück wahr

    funktion verbinde():
        selbst.sock = tcp_verbinde(selbst.host, selbst.port)
        setze sm auf neu StartupMessage(selbst.user, selbst.datenbank)
        selbst.sock.schreibe_bytes(sm.zu_bytes())
        selbst.empfange_bis_ready()
        gib_zurück selbst.ready

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
            setze rest auf body_len
            solange rest > 0:
                setze chunk auf selbst.sock.lesen_bytes(rest)
                wenn länge(chunk) == 0:
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
                    wenn code == 0:
                        setze ignore auf 0
                    sonst wenn code == 10:
                        # AuthenticationSASL
                        wenn selbst.passwort == "":
                            selbst.letzte_fehler = "Server verlangt SASL aber kein Passwort gesetzt"
                            setze laeuft auf falsch
                        sonst:
                            selbst.scram_send_initial()
                    sonst wenn code == 11:
                        # AuthenticationSASLContinue
                        setze sf auf bytes_neu(bytes_slice(m["body"], 4, länge(m["body"])))
                        wenn nicht selbst.scram_send_final(sf):
                            setze laeuft auf falsch
                    sonst wenn code == 12:
                        # AuthenticationSASLFinal
                        setze sfin auf bytes_neu(bytes_slice(m["body"], 4, länge(m["body"])))
                        wenn nicht selbst.scram_verify_final(sfin):
                            setze laeuft auf falsch
                    sonst:
                        selbst.letzte_fehler = "Authentication nicht unterstuetzt: " + text(code)
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
                    setze spalten auf selbst.parse_row_description(m["body"])
                    ergebnis["spalten"] = spalten
                sonst wenn t == 68:
                    setze row auf selbst.parse_data_row(m["body"], spalten)
                    ergebnis["rows"].hinzufügen(row)
                sonst wenn t == 67:
                    setze erg auf lies_cstr(m["body"], 0)
                    ergebnis["tag"] = erg[0]
                sonst wenn t == 73:
                    setze ignore auf 0
                sonst wenn t == 69:
                    ergebnis["fehler"] = selbst.parse_fehler(m["body"])
                sonst wenn t == 90:
                    setze laeuft auf falsch
                sonst wenn t == 78:
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
# Smoketest: trust + SCRAM gegen Doppel-User-Setup
# ------------------------------------------------------------

setze HOST auf "192.168.50.65"
setze PORT auf 5433

setze ok auf 0
setze fail auf 0

funktion check(label, bedingung):
    wenn bedingung:
        zeige "  OK    " + label
        setze ok auf ok + 1
    sonst:
        zeige "  FAIL  " + label
        setze fail auf fail + 1


# ============================================================
# Benchmark — SCRAM-Version (moo_scram)
# ============================================================

zeige "================================================"
zeige "  moo Postgres Bench (SCRAM)"
zeige "================================================"

setze db auf neu PgClient("192.168.50.65", 5433, "moo_scram", "moo_scram", "xjJ6FnbwV9KX43nnBT9PoOHTuJVw")
setze t0 auf zeit_ms()
db.verbinde()
setze t_connect auf zeit_ms() - t0
zeige "Connect (SCRAM-Handshake): " + text(t_connect) + " ms"

setze r auf db.query("DROP TABLE IF EXISTS bench_t")
setze r auf db.query("CREATE TABLE bench_t (id SERIAL PRIMARY KEY, name TEXT, n INT)")

# Bench 1: 1000x SELECT 1
setze t0 auf zeit_ms()
setze i auf 0
solange i < 1000:
    setze r auf db.query("SELECT 1")
    setze i auf i + 1
setze t1 auf zeit_ms() - t0
zeige "1000x SELECT 1: " + text(t1) + " ms (" + text(t1 / 1000) + " ms/query)"

# Bench 2: 1000x INSERT
setze t0 auf zeit_ms()
setze i auf 0
solange i < 1000:
    setze r auf db.query("INSERT INTO bench_t (name, n) VALUES ('row', " + text(i) + ")")
    setze i auf i + 1
setze t2 auf zeit_ms() - t0
zeige "1000x INSERT:    " + text(t2) + " ms (" + text(t2 / 1000) + " ms/insert)"

# Bench 3: SELECT all 1000 rows
setze t0 auf zeit_ms()
setze r auf db.query("SELECT * FROM bench_t")
setze t3 auf zeit_ms() - t0
zeige "SELECT 1000:     " + text(t3) + " ms (rows=" + text(r["anzahl"]) + ")"

setze r auf db.query("DROP TABLE bench_t")
zeige "================================================"
