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

# CopyData: 'd' + int32(4+len) + payload  (Frontend->Backend bei COPY FROM STDIN)
klasse CopyDataMessage(PgMessage):
    funktion erstelle(payload_bytes):
        selbst.typ = "copy_data"
        selbst.payload_bytes = payload_bytes  # Liste<int>

    funktion zu_bytes():
        setze gesamt auf 4 + länge(selbst.payload_bytes)
        setze msg auf []
        msg.hinzufügen(100)  # 'd'
        anhaengen_bytes(msg, int32_be(gesamt))
        anhaengen_bytes(msg, selbst.payload_bytes)
        gib_zurück msg

# CopyDone: 'c' + int32(4) + (no body)
klasse CopyDoneMessage(PgMessage):
    funktion erstelle():
        selbst.typ = "copy_done"

    funktion zu_bytes():
        gib_zurück [99, 0, 0, 0, 4]  # 'c' + length=4

# Wie selbst.parse_data_row aber als freie Funktion (vermeidet Method-Slot-Reuse-Bug
# bei grossen Volumes). Text-Format Decode: jede Spalte raw bytes als String.
funktion parse_data_row_free(body, spalten):
    setze n auf bytes_int16(body, 0)
    setze off auf 2
    setze row auf {}
    setze i auf 0
    solange i < n:
        setze laenge auf bytes_int32(body, off)
        setze off auf off + 4
        wenn laenge == -1 oder laenge >= 2147483648:
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

# Wie parse_data_row, decodet Binary pro Spalte gemaess oids.
# Freie Funktion (NICHT Methode) wegen Method-Slot-Reuse-Bug bei grossen Volumes.
funktion parse_data_row_binary(body, spalten, oids):
    setze n auf bytes_int16(body, 0)
    setze off auf 2
    setze row auf {}
    setze i auf 0
    solange i < n:
        setze laenge auf bytes_int32(body, off)
        setze off auf off + 4
        wenn laenge == -1 oder laenge >= 2147483648:
            row[spalten[i]] = nichts
        sonst:
            setze wert_bytes auf []
            setze j auf 0
            solange j < laenge:
                wert_bytes.hinzufügen(body[off + j])
                setze j auf j + 1
            row[spalten[i]] = parse_binary_value(oids[i], wert_bytes)
            setze off auf off + laenge
        setze i auf i + 1
    gib_zurück row

# Binary-Format Decode pro OID. Freie Funktion (NICHT Methode) wegen Method-Slot-Reuse-Bug
# bei grossen Volumes (>80 rows). Welle 1: int4 (23), text (25), bool (16).
funktion parse_binary_value(oid, bytes_liste):
    wenn länge(bytes_liste) == 0:
        gib_zurück bytes_neu([])
    wenn oid == 23:
        setze v auf bytes_int32(bytes_liste, 0)
        wenn v >= 2147483648:
            setze v auf v - 4294967296
        gib_zurück v
    sonst wenn oid == 16:
        wenn bytes_liste[0] == 1:
            gib_zurück wahr
        sonst:
            gib_zurück falsch
    sonst wenn oid == 25:
        gib_zurück bytes_neu(bytes_liste)
    sonst:
        gib_zurück bytes_neu(bytes_liste)

# Text-Format COPY-Escape: \\, \t, \n, \r — manual byte-loop (vermeidet Method-Chain-Heap-Druck)
funktion copy_text_escape(s):
    setze bs auf bytes_zu_liste(s)
    setze out auf []
    setze i auf 0
    solange i < länge(bs):
        setze c auf bs[i]
        wenn c == 92:
            out.hinzufügen(92)
            out.hinzufügen(92)
        sonst wenn c == 9:
            out.hinzufügen(92)
            out.hinzufügen(116)
        sonst wenn c == 10:
            out.hinzufügen(92)
            out.hinzufügen(110)
        sonst wenn c == 13:
            out.hinzufügen(92)
            out.hinzufügen(114)
        sonst:
            out.hinzufügen(c)
        setze i auf i + 1
    gib_zurück bytes_neu(out)

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
        # Prepared-Statement-Cache: stmt_name -> spalten-Liste
        # (Parse+Describe in prepare() liefert RowDescription, wird hier abgelegt
        # damit execute() die DataRows korrekt mit Spaltennamen mappen kann.)
        selbst.prepared_cols = {}
        # Type-OIDs pro Statement fuer Binary-Format-Decoding (int4/int8/text/bool/...)
        # Wenn vorhanden, fordert execute()/batch_prepared() Binary-Result-Format an.
        selbst.prepared_oids = {}

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

    # Wie parse_row_description, liefert zusaetzlich type-OIDs.
    # Returns [namen, oids] gleicher Laenge.
    funktion parse_row_full(body):
        setze namen auf []
        setze oids auf []
        setze n auf bytes_int16(body, 0)
        setze off auf 2
        setze i auf 0
        solange i < n:
            setze erg auf lies_cstr(body, off)
            namen.hinzufügen(erg[0])
            setze off auf erg[1]
            # off+0..3 table_oid (4), off+4..5 col_attr (2), off+6..9 type_oid (4)
            setze oid auf bytes_int32(body, off + 6)
            oids.hinzufügen(oid)
            setze off auf off + 18
            setze i auf i + 1
        gib_zurück [namen, oids]


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

    # Bulk-Insert via COPY FROM STDIN. spalten = Liste<String>, zeilen = Liste<Liste<Wert>>.
    # Liefert Anzahl tatsaechlich eingefuegter Zeilen (parsed aus CommandComplete-Tag "COPY n").
    funktion copy_in(tabelle, spalten, zeilen):
        setze sql auf "COPY " + tabelle + " ("
        setze i auf 0
        solange i < länge(spalten):
            wenn i > 0:
                setze sql auf sql + ", "
            setze sql auf sql + spalten[i]
            setze i auf i + 1
        setze sql auf sql + ") FROM STDIN"

        # Inline Query: 'Q' (81) + int32_be(4 + len(sql) + 1) + sql + \0
        setze sql_bytes auf bytes_zu_liste(sql)
        setze q_frame auf [81]
        anhaengen_bytes(q_frame, int32_be(4 + länge(sql_bytes) + 1))
        anhaengen_bytes(q_frame, sql_bytes)
        q_frame.hinzufügen(0)
        selbst.sock.schreibe_bytes(q_frame)

        # Erste Antwort: 'G' CopyInResponse oder 'E' Error
        setze m auf selbst.empfange_eine_message()
        wenn m == nichts:
            selbst.letzte_fehler = "COPY: keine Antwort"
            gib_zurück 0
        wenn m["typ"] == 69:
            selbst.letzte_fehler = selbst.parse_fehler(m["body"])
            gib_zurück 0
        wenn m["typ"] != 71:
            selbst.letzte_fehler = "COPY: erwartete 'G', bekam " + text(m["typ"])
            gib_zurück 0

        # Pro Zeile: kleiner chunk-Liste bauen + SOFORT als CopyDataMessage senden.
        # Vermeidet grosse Liste-Akkumulation die in moo-Runtime non-determini-
        # stische Heap-Korruption ausloest (Memory: moo-runtime-corrupted-
        # double-linked-list-2026-05-06). Postgres erlaubt beliebig viele
        # CopyData-Frames zwischen CopyInResponse und CopyDone.
        setze i auf 0
        solange i < länge(zeilen):
            setze row auf zeilen[i]
            setze chunk auf []
            setze j auf 0
            solange j < länge(row):
                wenn j > 0:
                    chunk.hinzufügen(9)
                setze v auf row[j]
                wenn v == nichts:
                    chunk.hinzufügen(92)
                    chunk.hinzufügen(78)
                sonst:
                    setze bs auf bytes_zu_liste(text(v))
                    setze k auf 0
                    solange k < länge(bs):
                        setze c auf bs[k]
                        wenn c == 92:
                            chunk.hinzufügen(92)
                            chunk.hinzufügen(92)
                        sonst wenn c == 9:
                            chunk.hinzufügen(92)
                            chunk.hinzufügen(116)
                        sonst wenn c == 10:
                            chunk.hinzufügen(92)
                            chunk.hinzufügen(110)
                        sonst wenn c == 13:
                            chunk.hinzufügen(92)
                            chunk.hinzufügen(114)
                        sonst:
                            chunk.hinzufügen(c)
                        setze k auf k + 1
                setze j auf j + 1
            chunk.hinzufügen(10)
            # Inline CopyData-Frame senden (kein Object-Wrapper, vermeidet
            # Slot-Reuse-Bug bei moo_object_new in der hot loop):
            # 'd' (100) + int32_be(4 + len(chunk)) + chunk
            setze frame auf [100]
            anhaengen_bytes(frame, int32_be(4 + länge(chunk)))
            anhaengen_bytes(frame, chunk)
            selbst.sock.schreibe_bytes(frame)
            setze i auf i + 1

        # Inline CopyDone: 'c' (99) + int32_be(4) (no body)
        selbst.sock.schreibe_bytes([99, 0, 0, 0, 4])

        # Nachfolgend: CommandComplete + ReadyForQuery (oder Error dazwischen)
        setze cnt auf 0
        setze laeuft auf wahr
        solange laeuft:
            setze m auf selbst.empfange_eine_message()
            wenn m == nichts:
                setze laeuft auf falsch
            sonst wenn m["typ"] == 67:
                # 'C' CommandComplete: Tag "COPY n"
                setze erg auf lies_cstr(m["body"], 0)
                setze parts auf erg[0].teilen(" ")
                wenn länge(parts) >= 2:
                    setze cnt auf zahl(parts[länge(parts) - 1])
            sonst wenn m["typ"] == 90:
                # 'Z' ReadyForQuery
                setze laeuft auf falsch
            sonst wenn m["typ"] == 69:
                selbst.letzte_fehler = selbst.parse_fehler(m["body"])
                setze laeuft auf falsch
        gib_zurück cnt

    # Extended Query Protocol: Statement preparieren.
    # Sendet Parse + Describe + Sync inline (kein PgMessage-Subklassen-Wrapper
    # wegen moo-codegen-subclass-instance-double-release-Bug, siehe Memory).
    # param_oids: Liste<Zahl> mit OIDs (0 = server soll inferieren).
    # Spalten der zurueckgegebenen Rows landen in selbst.prepared_cols[stmt_name].
    funktion prepare(stmt_name, sql, param_oids):
        # Parse-Frame: 'P' (80) + len + stmt_name\0 + sql\0 + int16 num_oids + int32[oids]
        setze stmt_bytes auf bytes_zu_liste(stmt_name)
        setze sql_bytes auf bytes_zu_liste(sql)
        setze p_body auf []
        anhaengen_bytes(p_body, stmt_bytes)
        p_body.hinzufügen(0)
        anhaengen_bytes(p_body, sql_bytes)
        p_body.hinzufügen(0)
        # int16 num_oids (BE)
        setze n_oids auf länge(param_oids)
        p_body.hinzufügen(boden(n_oids / 256) % 256)
        p_body.hinzufügen(n_oids % 256)
        setze i auf 0
        solange i < n_oids:
            anhaengen_bytes(p_body, int32_be(param_oids[i]))
            setze i auf i + 1
        setze parse_frame auf [80]
        anhaengen_bytes(parse_frame, int32_be(4 + länge(p_body)))
        anhaengen_bytes(parse_frame, p_body)

        # Describe-Frame: 'D' (68) + len + 'S' (83) + stmt_name\0
        setze d_body auf [83]
        anhaengen_bytes(d_body, stmt_bytes)
        d_body.hinzufügen(0)
        setze desc_frame auf [68]
        anhaengen_bytes(desc_frame, int32_be(4 + länge(d_body)))
        anhaengen_bytes(desc_frame, d_body)

        # Sync-Frame: 'S' (83) + int32 length=4
        setze sync_frame auf [83, 0, 0, 0, 4]

        selbst.sock.schreibe_bytes(parse_frame)
        selbst.sock.schreibe_bytes(desc_frame)
        selbst.sock.schreibe_bytes(sync_frame)

        # Empfangen: ParseComplete '1' (49) + ParameterDescription 't' (116, ignoriert)
        # + RowDescription 'T' (84) ODER NoData 'n' (110) + ReadyForQuery 'Z' (90)
        setze ok auf falsch
        setze spalten auf []
        setze oids auf []
        setze laeuft auf wahr
        solange laeuft:
            setze m auf selbst.empfange_eine_message()
            wenn m == nichts:
                setze laeuft auf falsch
            sonst wenn m["typ"] == 49:
                setze ok auf wahr
            sonst wenn m["typ"] == 116:
                setze ignore auf 0
            sonst wenn m["typ"] == 84:
                setze full auf selbst.parse_row_full(m["body"])
                setze spalten auf full[0]
                setze oids auf full[1]
            sonst wenn m["typ"] == 110:
                setze spalten auf []
                setze oids auf []
            sonst wenn m["typ"] == 90:
                setze laeuft auf falsch
            sonst wenn m["typ"] == 69:
                selbst.letzte_fehler = selbst.parse_fehler(m["body"])
                setze laeuft auf falsch
        wenn ok:
            selbst.prepared_cols[stmt_name] = spalten
            selbst.prepared_oids[stmt_name] = oids
        gib_zurück ok

    # Extended Query Protocol: prepared statement ausfuehren mit Bind+Execute+Sync.
    # params: Liste<Wert> mit Werten fuer $1, $2, ... (alle als Text-Format).
    # nichts -> NULL (length=-1 im Bind-Frame).
    # Zurueck: Dict mit "spalten", "rows", "anzahl", "fehler", "tag".
    funktion execute(stmt_name, params):
        setze stmt_bytes auf bytes_zu_liste(stmt_name)

        # Bind-Frame: 'B' (66)
        # portal-name "" + \0 + stmt-name + \0
        # int16 num-param-format-codes = 0 (default text fuer alle)
        # int16 num-params + per param: int32 len + bytes (oder len=-1 = NULL)
        # int16 num-result-format-codes = 0 (default text)
        setze b_body auf [0]
        anhaengen_bytes(b_body, stmt_bytes)
        b_body.hinzufügen(0)
        b_body.hinzufügen(0)
        b_body.hinzufügen(0)
        setze np auf länge(params)
        b_body.hinzufügen(boden(np / 256) % 256)
        b_body.hinzufügen(np % 256)
        setze i auf 0
        solange i < np:
            setze p auf params[i]
            wenn p == nichts:
                # length = -1 (signed int32, all-ones)
                b_body.hinzufügen(255)
                b_body.hinzufügen(255)
                b_body.hinzufügen(255)
                b_body.hinzufügen(255)
            sonst:
                setze ps auf bytes_zu_liste(text(p))
                anhaengen_bytes(b_body, int32_be(länge(ps)))
                anhaengen_bytes(b_body, ps)
            setze i auf i + 1
        # Result-Format-Codes: wenn prepared_oids vorhanden, alle Spalten binary (1)
        wenn selbst.prepared_oids.enthält(stmt_name):
            setze use_oids auf selbst.prepared_oids[stmt_name]
            setze nrc auf länge(use_oids)
            b_body.hinzufügen(boden(nrc / 256) % 256)
            b_body.hinzufügen(nrc % 256)
            setze k auf 0
            solange k < nrc:
                b_body.hinzufügen(0)
                b_body.hinzufügen(1)
                setze k auf k + 1
        sonst:
            b_body.hinzufügen(0)
            b_body.hinzufügen(0)
        setze bind_frame auf [66]
        anhaengen_bytes(bind_frame, int32_be(4 + länge(b_body)))
        anhaengen_bytes(bind_frame, b_body)

        # Execute-Frame: 'E' (69) + portal "" + \0 + int32 max_rows=0
        setze e_body auf [0]
        anhaengen_bytes(e_body, int32_be(0))
        setze exec_frame auf [69]
        anhaengen_bytes(exec_frame, int32_be(4 + länge(e_body)))
        anhaengen_bytes(exec_frame, e_body)

        # Alle 3 Frames in einem Send: Bind + Execute + Sync.
        # 3 separate schreibe_bytes-Calls wuerden 3x mehr Syscalls + TCP-Sends
        # erzeugen (auch mit TCP_NODELAY). Ein Send mit gebuendeltem Buffer
        # reduziert Latenz signifikant fuer Pipelined-Pattern.
        anhaengen_bytes(bind_frame, exec_frame)
        bind_frame.hinzufügen(83)
        bind_frame.hinzufügen(0)
        bind_frame.hinzufügen(0)
        bind_frame.hinzufügen(0)
        bind_frame.hinzufügen(4)
        selbst.sock.schreibe_bytes(bind_frame)

        setze ergebnis auf {}
        ergebnis["spalten"] = []
        ergebnis["rows"] = []
        ergebnis["anzahl"] = 0
        ergebnis["fehler"] = nichts
        ergebnis["tag"] = ""
        setze spalten auf []
        setze oids auf []
        wenn selbst.prepared_cols.enthält(stmt_name):
            setze spalten auf selbst.prepared_cols[stmt_name]
            ergebnis["spalten"] = spalten
        wenn selbst.prepared_oids.enthält(stmt_name):
            setze oids auf selbst.prepared_oids[stmt_name]

        setze laeuft auf wahr
        solange laeuft:
            setze m auf selbst.empfange_eine_message()
            wenn m == nichts:
                setze laeuft auf falsch
            sonst wenn m["typ"] == 50:
                setze ignore auf 0
            sonst wenn m["typ"] == 84:
                setze full auf selbst.parse_row_full(m["body"])
                setze spalten auf full[0]
                setze oids auf full[1]
                ergebnis["spalten"] = spalten
            sonst wenn m["typ"] == 68:
                wenn länge(oids) > 0:
                    setze row auf parse_data_row_binary(m["body"], spalten, oids)
                sonst:
                    setze row auf selbst.parse_data_row(m["body"], spalten)
                ergebnis["rows"].hinzufügen(row)
            sonst wenn m["typ"] == 67:
                setze erg auf lies_cstr(m["body"], 0)
                ergebnis["tag"] = erg[0]
            sonst wenn m["typ"] == 90:
                setze laeuft auf falsch
            sonst wenn m["typ"] == 69:
                ergebnis["fehler"] = selbst.parse_fehler(m["body"])
        ergebnis["anzahl"] = länge(ergebnis["rows"])
        gib_zurück ergebnis

    # Pipelining: n prepared-statement-Aufrufe in einem TCP-Send mit einem
    # einzigen Sync am Ende. Postgres-Spec erlaubt das; bei Error stoppt der
    # Server alle nachfolgenden Queries (saubere Recovery via Sync). Fuer
    # n=1000 SELECTs spart das ~999 Roundtrips.
    # spec_liste: Liste<Dict> mit "stmt" und "params" Schluesseln.
    # Returns: Liste<Ergebnis-Dict> 1:1 zu spec_liste, gleiche Felder wie execute().
    funktion batch_prepared(spec_liste):
        setze gesamt auf []
        setze i auf 0
        solange i < länge(spec_liste):
            setze spec auf spec_liste[i]
            setze stmt_name auf spec["stmt"]
            setze params auf spec["params"]
            setze stmt_bytes auf bytes_zu_liste(stmt_name)
            # Bind-Frame
            setze b_body auf [0]
            anhaengen_bytes(b_body, stmt_bytes)
            b_body.hinzufügen(0)
            b_body.hinzufügen(0)
            b_body.hinzufügen(0)
            setze np auf länge(params)
            b_body.hinzufügen(boden(np / 256) % 256)
            b_body.hinzufügen(np % 256)
            setze j auf 0
            solange j < np:
                setze p auf params[j]
                wenn p == nichts:
                    b_body.hinzufügen(255)
                    b_body.hinzufügen(255)
                    b_body.hinzufügen(255)
                    b_body.hinzufügen(255)
                sonst:
                    setze ps auf bytes_zu_liste(text(p))
                    anhaengen_bytes(b_body, int32_be(länge(ps)))
                    anhaengen_bytes(b_body, ps)
                setze j auf j + 1
            # batch_prepared bleibt vorerst TEXT-Mode (binary-format wuerde mit
            # parse_data_row_binary funktionieren, aber bei 1000+ specs gibt es
            # einen moo-Runtime-Receive-Limit-Bug; nur erste ~471 Antworten kommen
            # an. Reconciling waere Phase-X-Arbeit. Text-Mode lief solide bei 1000.
            b_body.hinzufügen(0)
            b_body.hinzufügen(0)
            gesamt.hinzufügen(66)
            anhaengen_bytes(gesamt, int32_be(4 + länge(b_body)))
            anhaengen_bytes(gesamt, b_body)
            # Execute-Frame
            setze e_body auf [0]
            anhaengen_bytes(e_body, int32_be(0))
            gesamt.hinzufügen(69)
            anhaengen_bytes(gesamt, int32_be(4 + länge(e_body)))
            anhaengen_bytes(gesamt, e_body)
            setze i auf i + 1
        # Single Sync am Ende
        gesamt.hinzufügen(83)
        gesamt.hinzufügen(0)
        gesamt.hinzufügen(0)
        gesamt.hinzufügen(0)
        gesamt.hinzufügen(4)
        selbst.sock.schreibe_bytes(gesamt)

        # Empfangen: pro Query BindComplete + DataRow* + CommandComplete.
        # Am Ende: ReadyForQuery (nach dem einen Sync).
        setze ergebnisse auf []
        setze idx auf 0
        setze cur auf {}
        cur["spalten"] = []
        cur["rows"] = []
        cur["anzahl"] = 0
        cur["fehler"] = nichts
        cur["tag"] = ""
        setze spalten auf []
        setze cur_oids auf []
        setze laeuft auf wahr
        solange laeuft:
            setze m auf selbst.empfange_eine_message()
            wenn m == nichts:
                setze laeuft auf falsch
            sonst wenn m["typ"] == 50:
                # 'B' BindComplete: starte neue Query
                setze cur auf {}
                cur["spalten"] = []
                cur["rows"] = []
                cur["anzahl"] = 0
                cur["fehler"] = nichts
                cur["tag"] = ""
                wenn idx < länge(spec_liste):
                    setze stmt_name auf spec_liste[idx]["stmt"]
                    wenn selbst.prepared_cols.enthält(stmt_name):
                        setze spalten auf selbst.prepared_cols[stmt_name]
                        cur["spalten"] = spalten
                    sonst:
                        setze spalten auf []
                    setze cur_oids auf []
            sonst wenn m["typ"] == 84:
                setze full auf selbst.parse_row_full(m["body"])
                setze spalten auf full[0]
                setze cur_oids auf full[1]
                cur["spalten"] = spalten
            sonst wenn m["typ"] == 68:
                wenn länge(cur_oids) > 0:
                    cur["rows"].hinzufügen(parse_data_row_binary(m["body"], spalten, cur_oids))
                sonst:
                    cur["rows"].hinzufügen(parse_data_row_free(m["body"], spalten))
            sonst wenn m["typ"] == 67:
                setze erg auf lies_cstr(m["body"], 0)
                cur["tag"] = erg[0]
                cur["anzahl"] = länge(cur["rows"])
                ergebnisse.hinzufügen(cur)
                setze idx auf idx + 1
            sonst wenn m["typ"] == 90:
                setze laeuft auf falsch
            sonst wenn m["typ"] == 69:
                cur["fehler"] = selbst.parse_fehler(m["body"])
        gib_zurück ergebnisse

    funktion schliesse():
        wenn selbst.sock != nichts:
            setze tm auf neu TerminateMessage()
            selbst.sock.schreibe_bytes(tm.zu_bytes())
            selbst.sock.schliessen()
            setze selbst.sock auf nichts

# ------------------------------------------------------------
# PgPool: Connection-Pooling
# ------------------------------------------------------------
# Spart wiederholten Connect+SCRAM-Auth-Roundtrip bei vielen kurzen DB-Aufrufen.
# Single-threaded moo: Pool ist primaer Reuse-Optimierung + Multi-Thread-Bereit-
# schaft, keine Concurrency-Notwendigkeit.

klasse PgPool:
    funktion erstelle(host, port, user, datenbank, passwort, max_conns):
        selbst.host = host
        selbst.port = port
        selbst.user = user
        selbst.datenbank = datenbank
        selbst.passwort = passwort
        selbst.max_conns = max_conns
        selbst.frei = []
        selbst.belegt = 0

    # Holt eine Verbindung: aus dem frei-Pool oder neu erstellt wenn unter Limit.
    # Liefert nichts wenn Pool voll und keine frei.
    funktion checkout():
        wenn länge(selbst.frei) > 0:
            setze c auf selbst.frei.pop()
            selbst.belegt = selbst.belegt + 1
            gib_zurück c
        wenn selbst.belegt < selbst.max_conns:
            setze c auf neu PgClient(selbst.host, selbst.port, selbst.user, selbst.datenbank, selbst.passwort)
            c.verbinde()
            selbst.belegt = selbst.belegt + 1
            gib_zurück c
        gib_zurück nichts

    # Gibt eine Verbindung in den Pool zurueck.
    funktion checkin(c):
        selbst.frei.hinzufügen(c)
        selbst.belegt = selbst.belegt - 1

    # Schliesst alle frei-Verbindungen. Belegt-Connections sollten vom Caller
    # selbst geschlossen werden (oder via spaeteren checkin).
    funktion schliesse_alle():
        setze i auf 0
        solange i < länge(selbst.frei):
            selbst.frei[i].schliesse()
            setze i auf i + 1
        selbst.frei = []

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

zeige "================================================"
zeige "  moo Postgres-Client — trust + SCRAM E2E"
zeige "================================================"

zeige ""
zeige "--- moo_trust (trust, kein PW) ---"
setze t auf neu PgClient(HOST, PORT, "moo_trust", "moo_trust", "")
t.verbinde()
check("trust: ready", t.ready)
check("trust: backend_pid > 0", t.backend_pid > 0)
wenn t.ready:
    setze r auf t.query("SELECT 1 AS eins")
    check("trust: SELECT 1 wert", r["rows"][0]["eins"] == "1")
sonst:
    zeige "  letzte_fehler: " + text(t.letzte_fehler)
t.schliesse()

zeige ""
zeige "--- moo_scram (SCRAM-SHA-256) ---"
setze s auf neu PgClient(HOST, PORT, "moo_scram", "moo_scram", "xjJ6FnbwV9KX43nnBT9PoOHTuJVw")
s.verbinde()
check("scram: ready", s.ready)
check("scram: backend_pid > 0", s.backend_pid > 0)
check("scram: server_signature verifiziert", s.scram_server_verified)
wenn s.ready:
    setze r auf s.query("SELECT current_user AS u")
    check("scram: current_user=moo_scram", r["rows"][0]["u"] == "moo_scram")
    setze r auf s.query("SELECT 42 AS antwort")
    check("scram: SELECT 42", r["rows"][0]["antwort"] == "42")
sonst:
    zeige "  letzte_fehler: " + text(s.letzte_fehler)
s.schliesse()

zeige ""
zeige "--- moo_scram mit FALSCHEM Passwort ---"
setze bad auf neu PgClient(HOST, PORT, "moo_scram", "moo_scram", "FALSCH-PW-12345")
bad.verbinde()
check("falsch-pw: NICHT ready", nicht bad.ready)
check("falsch-pw: fehler gesetzt", bad.letzte_fehler != nichts)
wenn bad.letzte_fehler != nichts:
    zeige "  letzte_fehler: " + text(bad.letzte_fehler)
bad.schliesse()

zeige ""
zeige "================================================"
zeige "  Ergebnis: " + text(ok) + " ok / " + text(fail) + " fail"
zeige "================================================"
