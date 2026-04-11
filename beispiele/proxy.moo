# ============================================================
# moo TCP Reverse-Proxy — sequenzieller HTTP-Forward-Proxy
#
# Kompilieren: moo-compiler compile proxy.moo -o proxy
# Starten:     ./proxy
#
# Listen  auf: 0.0.0.0:9000
# Forward auf: 127.0.0.1:8080
#
# Aufbau: pro Verbindung wird der komplette Client-Request
# eingelesen (bis \r\n\r\n), eine Upstream-Verbindung geoeffnet,
# der Request 1:1 weitergeleitet und die Antwort bis EOF zurueck
# an den Client gestreamt. Sequenziell (eine Verbindung zur
# Zeit) weil Threading in moo aktuell mit Lambdas crasht; das ist
# ausreichend fuer Tests mit curl.
#
# Stats: Verbindungs-Counter + uebertragene Bytes (client→up / up→client).
# ============================================================

konstante LISTEN_PORT auf 9000
konstante UPSTREAM_HOST auf "127.0.0.1"
konstante UPSTREAM_PORT auf 8080

# --- Hilfs: lese vom client bis \r\n\r\n (HTTP-Header-Ende) ---
funktion lese_http_header(sock):
    setze buf auf ""
    setze versuche auf 0
    solange versuche < 1000:
        setze chunk auf sock.lesen(4096)
        wenn länge(chunk) == 0:
            gib_zurück buf
        setze buf auf buf + chunk
        # Suche \r\n\r\n
        setze blen auf länge(buf)
        setze i auf 0
        solange i < blen - 3:
            wenn buf[i] == "\r":
                wenn buf[i + 1] == "\n":
                    wenn buf[i + 2] == "\r":
                        wenn buf[i + 3] == "\n":
                            gib_zurück buf
            setze i auf i + 1
        setze versuche auf versuche + 1
    gib_zurück buf

# --- Hilfs: lese vom upstream bis EOF, schreibe direkt auf client ---
funktion stream_bis_eof(src, dst):
    setze total auf 0
    solange wahr:
        setze chunk auf src.lesen(8192)
        setze n auf länge(chunk)
        wenn n == 0:
            gib_zurück total
        dst.schreiben(chunk)
        setze total auf total + n

# --- Proxy-Handler: eine Verbindung ---
funktion handle(client, verb_nr):
    zeige "[" + text(verb_nr) + "] Neue Verbindung"

    # 1. Request vom Client einlesen
    setze req auf lese_http_header(client)
    setze req_len auf länge(req)
    zeige "[" + text(verb_nr) + "] Request: " + text(req_len) + " bytes"

    wenn req_len == 0:
        client.schliessen()
        gib_zurück [0, 0]

    # 2. Upstream verbinden
    setze up auf tcp_verbinde(UPSTREAM_HOST, UPSTREAM_PORT)
    wenn up == nichts:
        zeige "[" + text(verb_nr) + "] Upstream-Verbindung fehlgeschlagen"
        client.schliessen()
        gib_zurück [0, 0]

    # 3. Request 1:1 an upstream forwarden
    up.schreiben(req)

    # 4. Response streamen
    setze resp_bytes auf stream_bis_eof(up, client)
    zeige "[" + text(verb_nr) + "] Response: " + text(resp_bytes) + " bytes"

    # schliessen() loest aktuell glibc pthread-priority-Assert aus —
    # Socket wird beim naechsten Ziel-Close implizit freigegeben.
    gib_zurück [req_len, resp_bytes]

# --- Hauptschleife ---
zeige "=== moo TCP Reverse-Proxy ==="
zeige "Listen: 0.0.0.0:" + text(LISTEN_PORT)
zeige "Upstream: " + UPSTREAM_HOST + ":" + text(UPSTREAM_PORT)

setze server auf tcp_server(LISTEN_PORT)
zeige "Server gestartet, warte auf Verbindungen..."

setze verb_counter auf 0
setze total_in auf 0
setze total_out auf 0

solange wahr:
    setze client auf server.annehmen()
    wenn client == nichts:
        zeige "Accept fehlgeschlagen"
        warte(100)
    sonst:
        setze verb_counter auf verb_counter + 1
        setze res auf handle(client, verb_counter)
        setze total_in auf total_in + res[0]
        setze total_out auf total_out + res[1]
        zeige "  Stats: " + text(verb_counter) + " Verbindungen, IN=" + text(total_in) + " OUT=" + text(total_out)
