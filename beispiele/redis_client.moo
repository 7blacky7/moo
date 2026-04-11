# ============================================================
# moo Redis-Client — Reiner RESP-Wire-Protocol Client in moo
#
# Kompilieren: moo-compiler compile redis_client.moo -o redis_client
# Starten:     ./redis_client
#
# Testet gegen lokalen Docker-Redis (moo-test-redis auf Port 6380).
# ============================================================

konstante HOST auf "127.0.0.1"
konstante PORT auf 6380
konstante CRLF auf "\r\n"

# --- Read-Buffer fuer inkrementelles Lesen vom Socket ---
klasse RespBuf:
    funktion erstelle(sock):
        selbst.sock = sock
        selbst.buf = ""

    funktion fuellen():
        setze data auf selbst.sock.lesen(65536)
        wenn data == nichts:
            wirf "Socket geschlossen (nichts gelesen)"
        wenn länge(data) == 0:
            wirf "Socket geschlossen (0 bytes)"
        selbst.buf = selbst.buf + data

    # Liest eine Zeile bis \r\n (ohne \r\n). Blockiert bis genug da.
    funktion zeile():
        setze idx auf 0
        solange wahr:
            # Suche \r\n ab idx
            setze blen auf länge(selbst.buf)
            solange idx < blen - 1:
                wenn selbst.buf[idx] == "\r":
                    wenn selbst.buf[idx + 1] == "\n":
                        setze line auf selbst.buf.slice(0, idx)
                        selbst.buf = selbst.buf.slice(idx + 2, blen)
                        gib_zurück line
                setze idx auf idx + 1
            selbst.fuellen()

    funktion bytes(n):
        # Braucht genau n+2 Bytes (mit \r\n)
        solange länge(selbst.buf) < n + 2:
            selbst.fuellen()
        setze part auf selbst.buf.slice(0, n)
        selbst.buf = selbst.buf.slice(n + 2, länge(selbst.buf))
        gib_zurück part

    funktion parse_n_bulk(n):
        setze arr auf []
        setze i auf 0
        solange i < n:
            setze eline auf selbst.zeile()
            setze etyp auf eline[0]
            setze erest auf eline.slice(1, länge(eline))
            wenn etyp == "$":
                setze el auf zahl(erest)
                wenn el < 0:
                    arr.hinzufügen(nichts)
                sonst:
                    arr.hinzufügen(selbst.bytes(el))
            sonst:
                wenn etyp == ":":
                    arr.hinzufügen(zahl(erest))
                sonst:
                    arr.hinzufügen(erest)
            setze i auf i + 1
        gib_zurück arr

# --- RESP-Encoder: Liste von Argumenten → Request-String ---
funktion encode_req(args):
    setze n auf args.länge()
    setze teile auf []
    teile.hinzufügen("*" + text(n) + CRLF)
    setze i auf 0
    solange i < n:
        setze a auf args[i]
        teile.hinzufügen("$" + text(länge(a)) + CRLF)
        teile.hinzufügen(a + CRLF)
        setze i auf i + 1
    gib_zurück teile.verbinden("")

# Alles-in-einem Decoder. Array-Case wird inline iterativ geparst
# (fuer KEYS/LRANGE sind die Elemente immer bulk-strings).
funktion decode_resp(rbuf):
    setze line auf rbuf.zeile()
    setze typ auf line[0]
    setze rest auf line.slice(1, länge(line))
    wenn typ == "+":
        gib_zurück rest
    wenn typ == "-":
        wirf "Redis-Fehler: " + rest
    wenn typ == ":":
        gib_zurück zahl(rest)
    wenn typ == "$":
        setze n auf zahl(rest)
        wenn n < 0:
            gib_zurück nichts
        gib_zurück rbuf.bytes(n)
    wenn typ == "*":
        setze n auf zahl(rest)
        wenn n < 0:
            gib_zurück nichts
        setze arr auf []
        setze i auf 0
        solange i < n:
            # Inline bulk-string parsing (alle Array-Elemente von
            # KEYS/LRANGE sind bulk strings)
            setze eline auf rbuf.zeile()
            setze etyp auf eline[0]
            setze erest auf eline.slice(1, länge(eline))
            wenn etyp == "$":
                setze el auf zahl(erest)
                wenn el < 0:
                    arr.hinzufügen(nichts)
                sonst:
                    arr.hinzufügen(rbuf.bytes(el))
            sonst:
                wenn etyp == ":":
                    arr.hinzufügen(zahl(erest))
                sonst:
                    arr.hinzufügen(erest)
            setze i auf i + 1
        gib_zurück arr
    wirf "Unbekannter RESP-Typ: " + typ

# --- Redis-Client als Dict (keine Methoden — Klassen-Method-Chaining
# triggert aktuell einen Runtime-Crash bei mehreren verschachtelten
# selbst.x.y() Aufrufen. Freie Funktionen bleiben stabil.) ---
funktion redis_verbinde(host, port):
    setze c auf {}
    c["sock"] = tcp_verbinde(host, port)
    c["rbuf"] = neu RespBuf(c["sock"])
    gib_zurück c

funktion redis_befehl(c, args):
    setze req auf encode_req(args)
    c["sock"].schreiben(req)
    setze rb auf c["rbuf"]
    setze line auf rb.zeile()
    setze typ auf line[0]
    setze rest auf line.slice(1, länge(line))
    wenn typ == "+":
        gib_zurück rest
    wenn typ == "-":
        wirf "Redis-Fehler: " + rest
    wenn typ == ":":
        gib_zurück zahl(rest)
    wenn typ == "$":
        setze n auf zahl(rest)
        wenn n < 0:
            gib_zurück nichts
        gib_zurück rb.bytes(n)
    wenn typ == "*":
        setze n auf zahl(rest)
        wenn n < 0:
            gib_zurück nichts
        gib_zurück rb.parse_n_bulk(n)
    wirf "Unbekannter RESP-Typ: " + typ

funktion redis_ping(c):
    gib_zurück redis_befehl(c, ["PING"])
funktion redis_set(c, k, v):
    gib_zurück redis_befehl(c, ["SET", k, v])
funktion redis_get(c, k):
    gib_zurück redis_befehl(c, ["GET", k])
funktion redis_del(c, k):
    gib_zurück redis_befehl(c, ["DEL", k])
funktion redis_incr(c, k):
    gib_zurück redis_befehl(c, ["INCR", k])
funktion redis_keys(c, pattern):
    gib_zurück redis_befehl(c, ["KEYS", pattern])
funktion redis_flushall(c):
    gib_zurück redis_befehl(c, ["FLUSHALL"])
funktion redis_lpush(c, k, v):
    gib_zurück redis_befehl(c, ["LPUSH", k, v])
funktion redis_lrange(c, k, a, b):
    gib_zurück redis_befehl(c, ["LRANGE", k, text(a), text(b)])
funktion redis_close(c):
    c["sock"].schliessen()

# --- Test-Hilfsfunktionen ---
setze tests_ok auf 0
setze tests_fail auf 0

funktion check(label, ist, checkt):
    wenn ist == checkt:
        setze tests_ok auf tests_ok + 1
        zeige "  [OK] " + label + " = " + text(checkt)
    sonst:
        setze tests_fail auf tests_fail + 1
        zeige "  [FAIL] " + label + ": checkt " + text(checkt) + ", ist " + text(ist)

# --- Main ---
zeige "=== moo Redis-Client ==="
zeige "Verbinde mit " + HOST + ":" + text(PORT) + "..."

setze r auf redis_verbinde(HOST, PORT)
zeige "Verbunden."

# Test 1: PING
zeige ""
zeige "Test 1: PING"
setze pong auf redis_ping(r)
check("PING", pong, "PONG")

# Test 2: SET / GET
zeige ""
zeige "Test 2: SET foo bar; GET foo"
redis_set(r, "foo", "bar")
setze wert auf redis_get(r, "foo")
check("GET foo", wert, "bar")

# Test 3: INCR 100x
zeige ""
zeige "Test 3: INCR counter 100x"
redis_del(r, "counter")
setze i auf 0
solange i < 100:
    redis_incr(r, "counter")
    setze i auf i + 1
setze cval auf redis_get(r, "counter")
check("GET counter", cval, "100")

# Test 4: 1000 keys setzen, KEYS *, alle loeschen
zeige ""
zeige "Test 4: 1000 keys SET + KEYS + DEL"
redis_flushall(r)
setze i auf 0
solange i < 1000:
    redis_set(r, "k" + text(i), "v" + text(i))
    setze i auf i + 1
setze all_keys auf redis_keys(r, "k*")
check("KEYS Anzahl", all_keys.länge(), 1000)
setze v500 auf redis_get(r, "k500")
check("GET k500", v500, "v500")

setze geloescht auf 0
setze i auf 0
solange i < 1000:
    setze geloescht auf geloescht + redis_del(r, "k" + text(i))
    setze i auf i + 1
check("DEL Gesamt", geloescht, 1000)

# Test 5: LPUSH / LRANGE
zeige ""
zeige "Test 5: LPUSH liste a b c; LRANGE 0 -1"
redis_del(r, "liste")
redis_lpush(r, "liste", "a")
redis_lpush(r, "liste", "b")
redis_lpush(r, "liste", "c")
setze items auf redis_lrange(r, "liste", 0, -1)
check("LRANGE Anzahl", items.länge(), 3)
check("LRANGE[0]", items[0], "c")
check("LRANGE[1]", items[1], "b")
check("LRANGE[2]", items[2], "a")

zeige ""
zeige "=== Ergebnis: " + text(tests_ok) + " OK / " + text(tests_fail) + " FAIL ==="
redis_flushall(r)
redis_close(r)
wenn tests_fail == 0:
    zeige "ALLE TESTS BESTANDEN"
sonst:
    zeige "FEHLER!"
