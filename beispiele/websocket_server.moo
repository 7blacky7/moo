# ============================================================
# moo WebSocket-Server — RFC 6455 in pure moo
#
# Features:
#   - HTTP/1.1 Upgrade-Handshake (Sec-WebSocket-Accept via SHA1+base64)
#   - Frame-Parser (FIN/Opcode, 7/16/64-bit Laenge, Client-Masking)
#   - Frame-Encoder (unmaskierte Server-Frames)
#   - Echo-Server: jedes text/binary frame wird zurueckgesendet
#   - Close-Frame Handhabung
#
# Starten: moo-compiler compile websocket_server.moo -o ws_server
#          ./ws_server
# Testen:  wscat -c ws://localhost:9001
# ============================================================

konstante WS_GUID auf "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
konstante PORT auf 9001

# ------------------------------------------------------------
# Byte-Helfer
# ------------------------------------------------------------

funktion bytes_anhaengen(ziel, quelle):
    setze i auf 0
    solange i < länge(quelle):
        ziel.hinzufügen(quelle[i])
        setze i auf i + 1

funktion int_zu_byte(n):
    gib_zurück n % 256

funktion be16(n):
    gib_zurück [boden(n / 256) % 256, n % 256]

funktion be64(n):
    setze liste auf []
    # obere 32 Bits = 0 (wir gehen nicht ueber 2^32)
    liste.hinzufügen(0)
    liste.hinzufügen(0)
    liste.hinzufügen(0)
    liste.hinzufügen(0)
    liste.hinzufügen(boden(n / (256 * 256 * 256)) % 256)
    liste.hinzufügen(boden(n / (256 * 256)) % 256)
    liste.hinzufügen(boden(n / 256) % 256)
    liste.hinzufügen(n % 256)
    gib_zurück liste

# ------------------------------------------------------------
# HTTP-Handshake
# ------------------------------------------------------------

# Parst HTTP-Request-Header aus einem Byte-Puffer (bis \r\n\r\n)
funktion parse_http_header(sock):
    setze puffer auf []
    setze laeuft auf wahr
    solange laeuft:
        setze b auf sock.lesen_bytes(1)
        wenn länge(b) == 0:
            setze laeuft auf falsch
        sonst:
            puffer.hinzufügen(b[0])
            # Check auf \r\n\r\n Abschluss
            setze n auf länge(puffer)
            wenn n >= 4:
                wenn puffer[n-4] == 13 und puffer[n-3] == 10 und puffer[n-2] == 13 und puffer[n-1] == 10:
                    setze laeuft auf falsch
    gib_zurück bytes_neu(puffer)

funktion extrahiere_ws_key(header):
    setze zeilen auf header.teilen("\r\n")
    setze i auf 0
    solange i < länge(zeilen):
        setze z auf zeilen[i]
        # Case-insensitive Match auf "Sec-WebSocket-Key:"
        wenn länge(z) > 18:
            setze prefix auf z.klein()
            wenn prefix.teilstring(0, 18) == "sec-websocket-key:":
                setze rest auf z.teilstring(18, länge(z))
                gib_zurück rest.trimmen()
        setze i auf i + 1
    gib_zurück ""

funktion berechne_accept(key):
    setze kombiniert auf key + WS_GUID
    setze roh auf sha1_bytes(kombiniert)
    gib_zurück base64_kodieren(roh)

funktion sende_handshake_response(sock, accept):
    setze r auf "HTTP/1.1 101 Switching Protocols\r\n"
    setze r auf r + "Upgrade: websocket\r\n"
    setze r auf r + "Connection: Upgrade\r\n"
    setze r auf r + "Sec-WebSocket-Accept: " + accept + "\r\n"
    setze r auf r + "\r\n"
    sock.schreibe_bytes(bytes_zu_liste(r))

# ------------------------------------------------------------
# Frame-Parser
# ------------------------------------------------------------

klasse WsFrame:
    funktion erstelle():
        selbst.fin = wahr
        selbst.opcode = 0
        selbst.payload = []

funktion lies_genau(sock, n):
    setze out auf []
    setze rest auf n
    solange rest > 0:
        setze chunk auf sock.lesen_bytes(rest)
        wenn länge(chunk) == 0:
            gib_zurück nichts
        bytes_anhaengen(out, chunk)
        setze rest auf rest - länge(chunk)
    gib_zurück out

funktion lies_frame(sock):
    setze kopf auf lies_genau(sock, 2)
    wenn kopf == nichts:
        gib_zurück nichts
    setze b0 auf kopf[0]
    setze b1 auf kopf[1]
    setze frame auf neu WsFrame()
    wenn (b0 / 128) % 2 >= 1:
        frame.fin = wahr
    sonst:
        frame.fin = falsch
    setze frame.opcode auf b0 % 16
    setze maskiert auf falsch
    wenn (b1 / 128) % 2 >= 1:
        setze maskiert auf wahr
    setze laenge auf b1 % 128
    wenn laenge == 126:
        setze ext auf lies_genau(sock, 2)
        wenn ext == nichts:
            gib_zurück nichts
        setze laenge auf ext[0] * 256 + ext[1]
    sonst wenn laenge == 127:
        setze ext auf lies_genau(sock, 8)
        wenn ext == nichts:
            gib_zurück nichts
        # Nimm nur untere 32 Bits (reicht fuer Tests)
        setze laenge auf ext[4] * 256 * 256 * 256 + ext[5] * 256 * 256 + ext[6] * 256 + ext[7]
    setze mask auf [0, 0, 0, 0]
    wenn maskiert:
        setze mask auf lies_genau(sock, 4)
        wenn mask == nichts:
            gib_zurück nichts
    setze payload auf []
    wenn laenge > 0:
        setze payload auf lies_genau(sock, laenge)
        wenn payload == nichts:
            gib_zurück nichts
    wenn maskiert:
        setze i auf 0
        solange i < länge(payload):
            setze b auf payload[i]
            setze m auf mask[i % 4]
            # XOR ohne xor-Builtin: a XOR b = (a+b) - 2*(a AND b); simpler:
            # Wir implementieren via Bit-Iteration. Aber einfacher: Tabelle.
            setze xored auf xor_byte(b, m)
            payload[i] = xored
            setze i auf i + 1
    frame.payload = payload
    gib_zurück frame

# Byte-XOR ohne bit-builtin: manueller Bit-Loop
funktion xor_byte(a, b):
    setze r auf 0
    setze bit auf 1
    setze i auf 0
    solange i < 8:
        setze ab auf boden(a / bit) % 2
        setze bb auf boden(b / bit) % 2
        wenn ab != bb:
            setze r auf r + bit
        setze bit auf bit * 2
        setze i auf i + 1
    gib_zurück r

# ------------------------------------------------------------
# Frame-Encoder (Server -> Client, unmaskiert)
# ------------------------------------------------------------

funktion baue_frame(opcode, payload):
    setze frame auf []
    # FIN=1, opcode
    setze b0 auf 128 + opcode
    frame.hinzufügen(b0)
    setze laenge auf länge(payload)
    wenn laenge < 126:
        frame.hinzufügen(laenge)
    sonst wenn laenge < 65536:
        frame.hinzufügen(126)
        bytes_anhaengen(frame, be16(laenge))
    sonst:
        frame.hinzufügen(127)
        bytes_anhaengen(frame, be64(laenge))
    bytes_anhaengen(frame, payload)
    gib_zurück frame

funktion sende_text(sock, text):
    setze payload auf bytes_zu_liste(text)
    setze frame auf baue_frame(1, payload)
    sock.schreibe_bytes(frame)

funktion sende_binary(sock, payload):
    setze frame auf baue_frame(2, payload)
    sock.schreibe_bytes(frame)

funktion sende_pong(sock, payload):
    setze frame auf baue_frame(10, payload)
    sock.schreibe_bytes(frame)

funktion sende_close(sock):
    # Close-Frame mit Status 1000 (normal closure)
    setze payload auf [3, 232]  # 0x03E8 = 1000
    setze frame auf baue_frame(8, payload)
    sock.schreibe_bytes(frame)

# ------------------------------------------------------------
# Client-Handler
# ------------------------------------------------------------

funktion bearbeite_client(sock, client_id):
    zeige "[" + text(client_id) + "] verbunden"
    # 1. Handshake
    setze header auf parse_http_header(sock)
    wenn länge(header) == 0:
        zeige "[" + text(client_id) + "] kein header, abbruch"
        sock.schliessen()
        gib_zurück nichts
    setze key auf extrahiere_ws_key(header)
    wenn länge(key) == 0:
        zeige "[" + text(client_id) + "] kein Sec-WebSocket-Key"
        sock.schliessen()
        gib_zurück nichts
    setze accept auf berechne_accept(key)
    sende_handshake_response(sock, accept)
    zeige "[" + text(client_id) + "] handshake ok, key=" + key

    # 2. Frame-Loop
    setze laeuft auf wahr
    setze anzahl auf 0
    solange laeuft:
        setze frame auf lies_frame(sock)
        wenn frame == nichts:
            setze laeuft auf falsch
        sonst:
            setze anzahl auf anzahl + 1
            wenn frame.opcode == 1:
                # text
                setze t auf bytes_neu(frame.payload)
                zeige "[" + text(client_id) + "] text: " + t
                sende_text(sock, t)
            sonst wenn frame.opcode == 2:
                # binary
                zeige "[" + text(client_id) + "] binary: " + text(länge(frame.payload)) + " bytes"
                sende_binary(sock, frame.payload)
            sonst wenn frame.opcode == 8:
                # close
                zeige "[" + text(client_id) + "] close frame"
                sende_close(sock)
                setze laeuft auf falsch
            sonst wenn frame.opcode == 9:
                # ping
                zeige "[" + text(client_id) + "] ping"
                sende_pong(sock, frame.payload)
            sonst wenn frame.opcode == 10:
                # pong
                zeige "[" + text(client_id) + "] pong"
    sock.schliessen()
    zeige "[" + text(client_id) + "] disconnected (" + text(anzahl) + " frames)"

# ------------------------------------------------------------
# Server-Loop
# ------------------------------------------------------------

zeige "================================================"
zeige "  moo WebSocket-Server (RFC 6455)"
zeige "  Port: " + text(PORT)
zeige "  Test:  wscat -c ws://localhost:" + text(PORT)
zeige "================================================"

setze server auf tcp_server(PORT)
zeige "Listening on port " + text(PORT) + " ..."

setze client_id auf 0
solange wahr:
    setze client auf server.annehmen()
    setze client_id auf client_id + 1
    bearbeite_client(client, client_id)
