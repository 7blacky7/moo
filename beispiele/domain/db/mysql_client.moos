# ============================================================
# moo MySQL-Client — MySQL Wire-Protocol in pure moo
#
# Kompilieren: moo-compiler compile mysql_client.moo -o mysql_client
# Starten:     ./mysql_client
#
# Stresst das Binary-Protokoll mit Little-Endian-Integern und
# das Handshake + Query + ResultSet Flow.
#
# NOTE: Authentication braucht SHA1 (mysql_native_password) oder
# SHA256 (caching_sha2_password). Dieser Client erwartet aktuell
# einen User mit mysql_native_password UND moo_sha1() als Runtime-
# Builtin (siehe thoughts tag 'bug'). Bis das da ist parsen wir
# das Handshake komplett aber ueberspringen die Auth-Scramble.
# ============================================================

konstante HOST auf "127.0.0.1"
konstante PORT auf 3307
konstante USER auf "moouser"
konstante PASS auf "mootest"
konstante DB auf "mootest"

# --- Byte/Int Helper (Little-Endian) ---
funktion le_u16(bs, off):
    gib_zurück bs[off] + bs[off + 1] * 256

funktion le_u24(bs, off):
    gib_zurück bs[off] + bs[off + 1] * 256 + bs[off + 2] * 65536

funktion le_u32(bs, off):
    gib_zurück bs[off] + bs[off + 1] * 256 + bs[off + 2] * 65536 + bs[off + 3] * 16777216

funktion to_le(n, anzahl_bytes):
    setze result auf []
    setze i auf 0
    setze v auf n
    solange i < anzahl_bytes:
        result.hinzufügen(v % 256)
        setze v auf boden(v / 256)
        setze i auf i + 1
    gib_zurück result

# Sub-Liste von Index-Range
funktion slice_bytes(bs, off, len):
    setze r auf []
    setze i auf 0
    solange i < len:
        r.hinzufügen(bs[off + i])
        setze i auf i + 1
    gib_zurück r

# Findet NUL-terminierten String ab off, gibt [str, neuer_off]
funktion read_cstring(bs, off):
    setze ende auf off
    setze n auf bs.länge()
    solange ende < n und bs[ende] != 0:
        setze ende auf ende + 1
    setze sub auf slice_bytes(bs, off, ende - off)
    gib_zurück [bytes_neu(sub), ende + 1]

# Liest "len"-bytes und konvertiert zu String
funktion bytes_to_str(bs, off, len):
    gib_zurück bytes_neu(slice_bytes(bs, off, len))

funktion str_to_bytes(s):
    gib_zurück bytes_zu_liste(s)

# --- MySQL Length-Encoded Integer ---
# 0xFB          → NULL
# 0xFC + u16    → 2-byte value
# 0xFD + u24    → 3-byte value
# 0xFE + u64    → 8-byte value
# sonst         → 1-byte value
funktion read_lenenc_int(bs, off):
    setze b auf bs[off]
    wenn b < 251:
        gib_zurück [b, off + 1]
    wenn b == 252:
        gib_zurück [le_u16(bs, off + 1), off + 3]
    wenn b == 253:
        gib_zurück [le_u24(bs, off + 1), off + 4]
    wenn b == 254:
        gib_zurück [le_u32(bs, off + 1), off + 9]
    gib_zurück [nichts, off + 1]

funktion read_lenenc_str(bs, off):
    setze r auf read_lenenc_int(bs, off)
    setze len auf r[0]
    setze off2 auf r[1]
    wenn len == nichts:
        gib_zurück [nichts, off2]
    setze s auf bytes_to_str(bs, off2, len)
    gib_zurück [s, off2 + len]

# --- Packet I/O ---
# MySQL-Packet: 3-byte payload length (LE) + 1-byte sequence + payload
klasse MyConn:
    funktion erstelle(sock):
        selbst.sock = sock
        selbst.seq = 0
        selbst.buf = []

    funktion fuellen():
        setze chunk auf selbst.sock.lesen_bytes(65536)
        setze i auf 0
        setze n auf chunk.länge()
        solange i < n:
            selbst.buf.hinzufügen(chunk[i])
            setze i auf i + 1

    funktion ensure(n):
        solange selbst.buf.länge() < n:
            selbst.fuellen()

    funktion read_packet():
        selbst.ensure(4)
        setze plen auf selbst.buf[0] + selbst.buf[1] * 256 + selbst.buf[2] * 65536
        setze seq auf selbst.buf[3]
        selbst.ensure(4 + plen)
        setze payload auf []
        setze i auf 0
        solange i < plen:
            payload.hinzufügen(selbst.buf[4 + i])
            setze i auf i + 1
        # Rest-Buffer behalten
        setze rest auf []
        setze i auf 4 + plen
        setze blen auf selbst.buf.länge()
        solange i < blen:
            rest.hinzufügen(selbst.buf[i])
            setze i auf i + 1
        selbst.buf = rest
        selbst.seq = seq + 1
        gib_zurück payload

    funktion write_packet(payload):
        setze head auf to_le(payload.länge(), 3)
        head.hinzufügen(selbst.seq)
        setze alles auf []
        setze i auf 0
        solange i < 4:
            alles.hinzufügen(head[i])
            setze i auf i + 1
        setze i auf 0
        solange i < payload.länge():
            alles.hinzufügen(payload[i])
            setze i auf i + 1
        selbst.sock.schreiben_bytes(alles)
        selbst.seq = selbst.seq + 1

# --- Main: Protocol-Testbed ---
zeige "=== moo MySQL-Client ==="
zeige "Verbinde mit " + HOST + ":" + text(PORT)

setze sock auf tcp_verbinde(HOST, PORT)
setze conn auf neu MyConn(sock)

# 1. Handshake lesen
setze greet auf conn.read_packet()
zeige "Greeting-Paket: " + text(greet.länge()) + " bytes"

# Parse v10 handshake:
#   1 byte protocol version (10)
#   NUL-term server version
#   4 bytes connection id
#   8 bytes auth-plugin-data-part-1
#   1 byte filler (0x00)
#   2 bytes capability flags lower
#   1 byte charset
#   2 bytes status flags
#   2 bytes capability flags upper
#   1 byte auth-plugin-data-length (oder 0)
#   10 bytes reserved
#   auth-plugin-data-part-2 (max(13, length-8))
#   NUL-term auth-plugin-name
setze proto auf greet[0]
zeige "Protocol version: " + text(proto)

setze sv_res auf read_cstring(greet, 1)
setze server_version auf sv_res[0]
setze off auf sv_res[1]
zeige "Server version: " + server_version

setze conn_id auf le_u32(greet, off)
setze off auf off + 4
zeige "Connection ID: " + text(conn_id)

setze salt1 auf slice_bytes(greet, off, 8)
setze off auf off + 8 + 1  # salt1 + filler

setze caps_low auf le_u16(greet, off)
setze off auf off + 2

setze charset auf greet[off]
setze off auf off + 1

setze status_flags auf le_u16(greet, off)
setze off auf off + 2

setze caps_high auf le_u16(greet, off)
setze off auf off + 2

setze auth_len auf greet[off]
setze off auf off + 1 + 10  # auth_len + 10 reserved

setze salt2_len auf 12
wenn auth_len > 8:
    setze salt2_len auf auth_len - 8 - 1
setze salt2 auf slice_bytes(greet, off, salt2_len)
setze off auf off + salt2_len + 1  # +1 fuer NUL

setze plugin_res auf read_cstring(greet, off)
setze auth_plugin auf plugin_res[0]
zeige "Auth plugin: " + auth_plugin

# Salt zusammenbauen (20 bytes total)
setze salt auf []
setze i auf 0
solange i < 8:
    salt.hinzufügen(salt1[i])
    setze i auf i + 1
setze i auf 0
solange i < salt2_len:
    salt.hinzufügen(salt2[i])
    setze i auf i + 1
zeige "Salt gesammelt: " + text(salt.länge()) + " bytes"

# === Scramble: SHA1(pw) XOR SHA1(salt || SHA1(SHA1(pw))) ===
funktion mysql_scramble(pw_str, salt_liste):
    setze sha1_pw auf sha1_bytes(pw_str)
    setze sha1_sha1_pw auf sha1_bytes(sha1_pw)
    # salt || sha1_sha1_pw — als String
    setze salt_str auf bytes_neu(salt_liste)
    setze inner auf sha1_bytes(salt_str + sha1_sha1_pw)
    # XOR byte-weise (20 bytes)
    setze sha1_pw_l auf bytes_zu_liste(sha1_pw)
    setze inner_l auf bytes_zu_liste(inner)
    setze out auf []
    setze i auf 0
    solange i < 20:
        out.hinzufügen(sha1_pw_l[i] ^ inner_l[i])
        setze i auf i + 1
    gib_zurück out

setze scramble auf mysql_scramble(PASS, salt)
zeige "Scramble-Laenge: " + text(scramble.länge())

# === HandshakeResponse41 ===
# capability flags 4 bytes LE
#   CLIENT_LONG_PASSWORD=1, CLIENT_PROTOCOL_41=0x200,
#   CLIENT_CONNECT_WITH_DB=8, CLIENT_TRANSACTIONS=0x2000,
#   CLIENT_SECURE_CONNECTION=0x8000, CLIENT_PLUGIN_AUTH=0x80000
setze client_caps auf 0x88209
setze resp auf to_le(client_caps, 4)
# max_packet_size = 16M
setze mp auf to_le(16777216, 4)
setze i auf 0
solange i < 4:
    resp.hinzufügen(mp[i])
    setze i auf i + 1
# charset = 33 (utf8)
resp.hinzufügen(33)
# 23 bytes filler (0)
setze i auf 0
solange i < 23:
    resp.hinzufügen(0)
    setze i auf i + 1
# user + NUL
setze user_b auf str_to_bytes(USER)
setze i auf 0
solange i < user_b.länge():
    resp.hinzufügen(user_b[i])
    setze i auf i + 1
resp.hinzufügen(0)
# auth response: 1 byte length + 20 bytes
resp.hinzufügen(20)
setze i auf 0
solange i < 20:
    resp.hinzufügen(scramble[i])
    setze i auf i + 1
# DB + NUL
setze db_b auf str_to_bytes(DB)
setze i auf 0
solange i < db_b.länge():
    resp.hinzufügen(db_b[i])
    setze i auf i + 1
resp.hinzufügen(0)
# Plugin name + NUL
setze plug_b auf str_to_bytes("mysql_native_password")
setze i auf 0
solange i < plug_b.länge():
    resp.hinzufügen(plug_b[i])
    setze i auf i + 1
resp.hinzufügen(0)

zeige "HandshakeResponse-Laenge: " + text(resp.länge()) + " bytes"
conn.write_packet(resp)

# === Auth-Antwort ===
setze auth_resp auf conn.read_packet()
zeige "Auth-Response: " + text(auth_resp.länge()) + " bytes, erstes byte=" + text(auth_resp[0])

wenn auth_resp[0] == 254:
    # AuthSwitchRequest: 0xFE + plugin-name + NUL + new-salt + NUL
    zeige "AuthSwitchRequest — parse neuen salt"
    setze psw_res auf read_cstring(auth_resp, 1)
    setze neu_plugin auf psw_res[0]
    setze nps_off auf psw_res[1]
    zeige "Neues Plugin: " + neu_plugin
    # Neuer salt: 20 bytes ab nps_off (+ NUL am Ende)
    setze new_salt auf slice_bytes(auth_resp, nps_off, 20)
    setze new_scramble auf mysql_scramble(PASS, new_salt)
    # Sende scramble als naechstes packet
    conn.write_packet(new_scramble)
    zeige "Auth-Switch-Response gesendet (" + text(new_scramble.länge()) + " bytes)"
    # Naechste response lesen
    setze auth_resp auf conn.read_packet()
    zeige "Auth-Response2: " + text(auth_resp.länge()) + " bytes, erstes byte=" + text(auth_resp[0])

wenn auth_resp[0] == 255:
    zeige "ERR beim Auth: " + bytes_to_str(auth_resp, 3, auth_resp.länge() - 3)

wenn auth_resp[0] != 0:
    zeige "Auth fehlgeschlagen"

zeige ""
zeige "*** AUTH OK ***"

# --- Query-API: sendet SQL, liest response, returnt
#     {"typ": "ok"|"result"|"err", "affected": ..., "rows": ..., "msg": ...} ---
funktion mysql_exec(c, sql):
    c.seq = 0
    setze q auf [3]
    setze qb auf str_to_bytes(sql)
    setze i auf 0
    solange i < qb.länge():
        q.hinzufügen(qb[i])
        setze i auf i + 1
    c.write_packet(q)
    setze p auf c.read_packet()
    wenn p[0] == 0:
        # OK packet: affected_rows (lenenc), last_insert_id (lenenc), status, warnings
        setze r auf read_lenenc_int(p, 1)
        setze erg auf {}
        erg["typ"] = "ok"
        erg["affected"] = r[0]
        erg["rows"] = []
        gib_zurück erg
    wenn p[0] == 255:
        setze erg auf {}
        erg["typ"] = "err"
        erg["msg"] = bytes_to_str(p, 3, p.länge() - 3)
        erg["rows"] = []
        gib_zurück erg
    # Result Set
    setze cr auf read_lenenc_int(p, 0)
    setze col_count auf cr[0]
    # Column defs schlucken
    setze col_names auf []
    setze j auf 0
    solange j < col_count:
        setze cdef auf c.read_packet()
        # Struktur: catalog, schema, table, org_table, name, org_name, ...
        # Skip catalog
        setze off auf 0
        setze rr auf read_lenenc_str(cdef, off)
        setze off auf rr[1]
        setze rr auf read_lenenc_str(cdef, off)
        setze off auf rr[1]
        setze rr auf read_lenenc_str(cdef, off)
        setze off auf rr[1]
        setze rr auf read_lenenc_str(cdef, off)
        setze off auf rr[1]
        setze rr auf read_lenenc_str(cdef, off)
        col_names.hinzufügen(rr[0])
        setze j auf j + 1

    # Evtl. EOF nach col-defs
    setze first auf c.read_packet()
    wenn first[0] == 254 und first.länge() < 9:
        setze first auf c.read_packet()

    # Rows einsammeln
    setze rows auf []
    setze aktuell auf first
    solange aktuell[0] != 254:
        # Parse lenenc-strs bis Ende
        setze row auf []
        setze off auf 0
        setze plen auf aktuell.länge()
        solange off < plen:
            setze rr auf read_lenenc_str(aktuell, off)
            row.hinzufügen(rr[0])
            setze off auf rr[1]
        rows.hinzufügen(row)
        setze aktuell auf c.read_packet()

    setze erg auf {}
    erg["typ"] = "result"
    erg["cols"] = col_names
    erg["rows"] = rows
    gib_zurück erg

funktion druck_ergebnis(label, r):
    wenn r["typ"] == "err":
        zeige "  [ERR] " + label + ": " + r["msg"]
    sonst:
        wenn r["typ"] == "ok":
            zeige "  [OK]  " + label + " (affected=" + text(r["affected"]) + ")"
        sonst:
            zeige "  [OK]  " + label + " → " + text(r["rows"].länge()) + " Zeile(n)"

# --- Tests ---
zeige ""
zeige "--- Test 1: SELECT VERSION() ---"
setze r1 auf mysql_exec(conn, "SELECT VERSION()")
druck_ergebnis("VERSION", r1)
wenn r1["typ"] == "result":
    zeige "  → " + r1["rows"][0][0]

zeige ""
zeige "--- Test 2: CREATE TABLE ---"
setze r2 auf mysql_exec(conn, "DROP TABLE IF EXISTS moo_test")
druck_ergebnis("DROP", r2)
setze r3 auf mysql_exec(conn, "CREATE TABLE moo_test (id INT PRIMARY KEY, name VARCHAR(50))")
druck_ergebnis("CREATE", r3)

zeige ""
zeige "--- Test 3: INSERT 5 Zeilen ---"
setze i auf 1
solange i <= 5:
    setze sql auf "INSERT INTO moo_test VALUES (" + text(i) + ", 'row" + text(i) + "')"
    setze r auf mysql_exec(conn, sql)
    druck_ergebnis("INSERT " + text(i), r)
    setze i auf i + 1

zeige ""
zeige "--- Test 4: SELECT alle ---"
setze r4 auf mysql_exec(conn, "SELECT id, name FROM moo_test ORDER BY id")
druck_ergebnis("SELECT", r4)
wenn r4["typ"] == "result":
    setze k auf 0
    solange k < r4["rows"].länge():
        setze zl auf r4["rows"][k]
        zeige "    [" + zl[0] + "] " + zl[1]
        setze k auf k + 1

zeige ""
zeige "--- Test 5: UPDATE + DELETE ---"
setze r5 auf mysql_exec(conn, "UPDATE moo_test SET name='updated' WHERE id=3")
druck_ergebnis("UPDATE", r5)
setze r6 auf mysql_exec(conn, "DELETE FROM moo_test WHERE id=5")
druck_ergebnis("DELETE", r6)
setze r7 auf mysql_exec(conn, "SELECT COUNT(*) FROM moo_test")
druck_ergebnis("COUNT", r7)
wenn r7["typ"] == "result":
    zeige "    → " + r7["rows"][0][0] + " Zeilen verblieben"

zeige ""
zeige "--- Test 6: Fehlerbehandlung (kaputte SQL) ---"
setze r8 auf mysql_exec(conn, "SELECT * FROM existiert_nicht")
druck_ergebnis("Fehler-Test", r8)

# Aufraeumen
setze r9 auf mysql_exec(conn, "DROP TABLE moo_test")
druck_ergebnis("Cleanup", r9)

sock.schliessen()
zeige ""
zeige "=== Fertig ==="
