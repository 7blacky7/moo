# ============================================================
# moo MQTT-Broker — MQTT 3.1.1 (OASIS) in pure moo
#
# Features (geplant, Parser/Encoder komplett, I/O-Loop siehe unten):
#   - Fixed Header + Remaining Length (Variable-Length Integer)
#   - CONNECT / CONNACK
#   - PUBLISH (QoS 0)
#   - SUBSCRIBE / SUBACK
#   - UNSUBSCRIBE / UNSUBACK
#   - PINGREQ / PINGRESP
#   - DISCONNECT
#   - Topic-Routing mit '+' und '#' Wildcards
#
# Starten: moo-compiler compile mqtt_broker.moo -o mqtt_broker
#          ./mqtt_broker   (Port 1884)
# Testen:  mosquitto_pub -h 127.0.0.1 -p 1884 -t test/foo -m hello
# ============================================================

konstante PORT auf 1884

# Packet-Typen (obere 4 Bit von byte 0)
konstante PT_CONNECT auf 1
konstante PT_CONNACK auf 2
konstante PT_PUBLISH auf 3
konstante PT_PUBACK auf 4
konstante PT_SUBSCRIBE auf 8
konstante PT_SUBACK auf 9
konstante PT_UNSUBSCRIBE auf 10
konstante PT_UNSUBACK auf 11
konstante PT_PINGREQ auf 12
konstante PT_PINGRESP auf 13
konstante PT_DISCONNECT auf 14

# ------------------------------------------------------------
# Byte-Helfer
# ------------------------------------------------------------

funktion bytes_anhaengen(ziel, quelle):
    setze i auf 0
    solange i < länge(quelle):
        ziel.hinzufügen(quelle[i])
        setze i auf i + 1

funktion be16(n):
    gib_zurück [boden(n / 256) % 256, n % 256]

funktion lies_be16(liste, off):
    gib_zurück liste[off] * 256 + liste[off + 1]

# Variable-length integer: 1-4 bytes, bit 7 = continuation
funktion kodiere_vli(n):
    setze out auf []
    setze laeuft auf wahr
    solange laeuft:
        setze b auf n % 128
        setze n auf boden(n / 128)
        wenn n > 0:
            setze b auf b + 128
            out.hinzufügen(b)
        sonst:
            out.hinzufügen(b)
            setze laeuft auf falsch
    gib_zurück out

# Dekodiere VLI aus Socket. Liefert [wert, gelesene_bytes] oder nichts.
funktion lies_vli_socket(sock):
    setze wert auf 0
    setze mult auf 1
    setze i auf 0
    solange i < 4:
        setze b auf sock.lesen_bytes(1)
        wenn länge(b) == 0:
            gib_zurück nichts
        setze byte auf b[0]
        setze wert auf wert + (byte % 128) * mult
        wenn byte < 128:
            gib_zurück wert
        setze mult auf mult * 128
        setze i auf i + 1
    gib_zurück nichts

# MQTT String: 2-byte big-endian length + UTF-8 bytes
funktion kodiere_string(s):
    setze bytes auf bytes_zu_liste(s)
    setze out auf []
    bytes_anhaengen(out, be16(länge(bytes)))
    bytes_anhaengen(out, bytes)
    gib_zurück out

# Liest einen MQTT-String aus einem Byte-Puffer. Liefert [text, neue_off].
funktion lies_mqtt_string(liste, off):
    setze laenge auf lies_be16(liste, off)
    setze off auf off + 2
    setze bytes auf []
    setze i auf 0
    solange i < laenge:
        bytes.hinzufügen(liste[off + i])
        setze i auf i + 1
    setze s auf bytes_neu(bytes)
    gib_zurück [s, off + laenge]

# ------------------------------------------------------------
# Buffer-basierter Parser (versucht ein komplettes Paket aus Bytes ab off)
# Liefert Dict mit "typ","flags","body","neue_off" oder nichts bei unvollstaendig
# ------------------------------------------------------------

funktion parse_vli_buf(buf, off):
    setze wert auf 0
    setze mult auf 1
    setze i auf 0
    solange i < 4:
        wenn off + i >= länge(buf):
            gib_zurück nichts
        setze byte auf buf[off + i]
        setze wert auf wert + (byte % 128) * mult
        wenn byte < 128:
            setze r auf {}
            r["wert"] = wert
            r["neue_off"] = off + i + 1
            gib_zurück r
        setze mult auf mult * 128
        setze i auf i + 1
    gib_zurück nichts

funktion parse_packet_buf(buf, off):
    wenn off >= länge(buf):
        gib_zurück nichts
    setze b0 auf buf[off]
    setze typ auf boden(b0 / 16)
    setze flags auf b0 % 16
    setze vli auf parse_vli_buf(buf, off + 1)
    wenn vli == nichts:
        gib_zurück nichts
    setze rem auf vli["wert"]
    setze body_start auf vli["neue_off"]
    wenn body_start + rem > länge(buf):
        gib_zurück nichts
    setze body auf []
    setze i auf 0
    solange i < rem:
        body.hinzufügen(buf[body_start + i])
        setze i auf i + 1
    setze p auf {}
    p["typ"] = typ
    p["flags"] = flags
    p["body"] = body
    p["neue_off"] = body_start + rem
    gib_zurück p

# ------------------------------------------------------------
# CONNECT body parsen
# ------------------------------------------------------------

funktion parse_connect(body):
    # Protocol Name (MQTT), Level (4), Connect Flags (1), Keep Alive (2), Client ID
    setze erg auf lies_mqtt_string(body, 0)
    setze proto auf erg[0]
    setze off auf erg[1]
    setze level auf body[off]
    setze off auf off + 1
    setze cflags auf body[off]
    setze off auf off + 1
    setze keep_alive auf lies_be16(body, off)
    setze off auf off + 2
    setze erg auf lies_mqtt_string(body, off)
    setze client_id auf erg[0]
    setze r auf {}
    r["proto"] = proto
    r["level"] = level
    r["flags"] = cflags
    r["keep_alive"] = keep_alive
    r["client_id"] = client_id
    gib_zurück r

# ------------------------------------------------------------
# PUBLISH body parsen (QoS 0 — kein packet_id)
# ------------------------------------------------------------

funktion parse_publish(body, flags):
    setze qos auf boden(flags / 2) % 4
    setze erg auf lies_mqtt_string(body, 0)
    setze topic auf erg[0]
    setze off auf erg[1]
    setze packet_id auf 0
    wenn qos > 0:
        setze packet_id auf lies_be16(body, off)
        setze off auf off + 2
    setze payload auf []
    setze i auf off
    solange i < länge(body):
        payload.hinzufügen(body[i])
        setze i auf i + 1
    setze r auf {}
    r["topic"] = topic
    r["qos"] = qos
    r["packet_id"] = packet_id
    r["payload"] = payload
    gib_zurück r

# ------------------------------------------------------------
# SUBSCRIBE body parsen
# ------------------------------------------------------------

funktion parse_subscribe(body):
    setze packet_id auf lies_be16(body, 0)
    setze off auf 2
    setze topics auf []
    solange off < länge(body):
        setze erg auf lies_mqtt_string(body, off)
        setze topic auf erg[0]
        setze off auf erg[1]
        setze qos auf body[off]
        setze off auf off + 1
        setze t auf {}
        t["topic"] = topic
        t["qos"] = qos
        topics.hinzufügen(t)
    setze r auf {}
    r["packet_id"] = packet_id
    r["topics"] = topics
    gib_zurück r

funktion parse_unsubscribe(body):
    setze packet_id auf lies_be16(body, 0)
    setze off auf 2
    setze topics auf []
    solange off < länge(body):
        setze erg auf lies_mqtt_string(body, off)
        topics.hinzufügen(erg[0])
        setze off auf erg[1]
    setze r auf {}
    r["packet_id"] = packet_id
    r["topics"] = topics
    gib_zurück r

# ------------------------------------------------------------
# Packet-Encoder
# ------------------------------------------------------------

funktion baue_fixed_header(typ, flags, remaining_len):
    setze out auf []
    out.hinzufügen(typ * 16 + flags)
    bytes_anhaengen(out, kodiere_vli(remaining_len))
    gib_zurück out

funktion baue_connack(session_present, return_code):
    setze body auf [session_present % 2, return_code]
    setze out auf baue_fixed_header(PT_CONNACK, 0, länge(body))
    bytes_anhaengen(out, body)
    gib_zurück out

funktion baue_publish(topic, payload):
    # QoS 0, kein packet_id
    setze body auf []
    bytes_anhaengen(body, kodiere_string(topic))
    bytes_anhaengen(body, payload)
    setze out auf baue_fixed_header(PT_PUBLISH, 0, länge(body))
    bytes_anhaengen(out, body)
    gib_zurück out

funktion baue_suback(packet_id, granted_qos_liste):
    setze body auf []
    bytes_anhaengen(body, be16(packet_id))
    setze i auf 0
    solange i < länge(granted_qos_liste):
        body.hinzufügen(granted_qos_liste[i])
        setze i auf i + 1
    setze out auf baue_fixed_header(PT_SUBACK, 0, länge(body))
    bytes_anhaengen(out, body)
    gib_zurück out

funktion baue_unsuback(packet_id):
    setze body auf be16(packet_id)
    setze out auf baue_fixed_header(PT_UNSUBACK, 0, länge(body))
    bytes_anhaengen(out, body)
    gib_zurück out

funktion baue_pingresp():
    gib_zurück baue_fixed_header(PT_PINGRESP, 0, 0)

# ------------------------------------------------------------
# Topic-Matching mit Wildcards
# ------------------------------------------------------------

# '+' matcht genau ein Level, '#' matcht beliebig viele Levels (nur am Ende)
funktion topic_passt(filter_str, topic):
    setze f_teile auf filter_str.teilen("/")
    setze t_teile auf topic.teilen("/")
    setze i auf 0
    solange i < länge(f_teile):
        setze ft auf f_teile[i]
        wenn ft == "#":
            gib_zurück wahr
        wenn i >= länge(t_teile):
            gib_zurück falsch
        wenn ft != "+" und ft != t_teile[i]:
            gib_zurück falsch
        setze i auf i + 1
    # Alle Filter-Segmente passten; topic darf nicht laenger sein
    wenn länge(f_teile) == länge(t_teile):
        gib_zurück wahr
    gib_zurück falsch

# ------------------------------------------------------------
# Subscription-Registry
# ------------------------------------------------------------

klasse Broker:
    funktion erstelle():
        # subscriptions: dict filter_str -> liste von client_ids
        selbst.subs = {}
        # clients: liste von dict{id, sock, buf, lebt, name}
        selbst.clients = []
        selbst.next_id = 0

    funktion add_client(sock):
        setze id auf selbst.next_id
        setze selbst.next_id auf id + 1
        setze c auf {}
        c["id"] = id
        c["sock"] = sock
        c["buf"] = []
        c["lebt"] = wahr
        c["name"] = ""
        selbst.clients.hinzufügen(c)
        gib_zurück c

    funktion finde_client(id):
        setze i auf 0
        solange i < länge(selbst.clients):
            wenn selbst.clients[i]["id"] == id:
                gib_zurück selbst.clients[i]
            setze i auf i + 1
        gib_zurück nichts

    funktion cleanup_tote():
        setze neu_liste auf []
        setze i auf 0
        solange i < länge(selbst.clients):
            setze c auf selbst.clients[i]
            wenn c["lebt"]:
                neu_liste.hinzufügen(c)
            sonst:
                zeige "[" + text(c["id"]) + "] entfernt"
                c["sock"].schliessen()
                # Aus allen Subs entfernen
                setze fl auf selbst.subs.schlüssel()
                setze j auf 0
                solange j < länge(fl):
                    setze f auf fl[j]
                    setze nl auf []
                    setze k auf 0
                    solange k < länge(selbst.subs[f]):
                        wenn selbst.subs[f][k] != c["id"]:
                            nl.hinzufügen(selbst.subs[f][k])
                        setze k auf k + 1
                    selbst.subs[f] = nl
                    setze j auf j + 1
            setze i auf i + 1
        setze selbst.clients auf neu_liste

    funktion abonniere(client_id, filter_str):
        wenn selbst.subs.hat(filter_str) == falsch:
            selbst.subs[filter_str] = []
        selbst.subs[filter_str].hinzufügen(client_id)

    funktion deabonniere(client_id, filter_str):
        wenn selbst.subs.hat(filter_str) == falsch:
            gib_zurück nichts
        setze nl auf []
        setze j auf 0
        solange j < länge(selbst.subs[filter_str]):
            wenn selbst.subs[filter_str][j] != client_id:
                nl.hinzufügen(selbst.subs[filter_str][j])
            setze j auf j + 1
        selbst.subs[filter_str] = nl

    funktion empfaenger(topic):
        setze out auf []
        setze fl auf selbst.subs.schlüssel()
        setze i auf 0
        solange i < länge(fl):
            setze f auf fl[i]
            wenn topic_passt(f, topic):
                setze j auf 0
                solange j < länge(selbst.subs[f]):
                    setze cid auf selbst.subs[f][j]
                    setze bekannt auf falsch
                    setze k auf 0
                    solange k < länge(out):
                        wenn out[k] == cid:
                            setze bekannt auf wahr
                        setze k auf k + 1
                    wenn bekannt == falsch:
                        out.hinzufügen(cid)
                    setze j auf j + 1
            setze i auf i + 1
        gib_zurück out

    funktion leite_weiter(topic, payload):
        setze frame auf baue_publish(topic, payload)
        setze ids auf selbst.empfaenger(topic)
        setze n auf 0
        setze i auf 0
        solange i < länge(ids):
            setze c auf selbst.finde_client(ids[i])
            wenn c != nichts:
                versuche:
                    c["sock"].schreibe_bytes(frame)
                    setze n auf n + 1
                fange e:
                    c["lebt"] = falsch
            setze i auf i + 1
        gib_zurück n

# ------------------------------------------------------------
# Packet-Dispatch (verarbeitet ein bereits geparstes Paket)
# ------------------------------------------------------------

funktion dispatch_paket(broker, c, p):
    setze cid auf c["id"]
    setze typ auf p["typ"]
    wenn typ == PT_CONNECT:
        setze conn auf parse_connect(p["body"])
        c["name"] = conn["client_id"]
        zeige "[" + text(cid) + "] CONNECT client_id=" + conn["client_id"] + " keep_alive=" + text(conn["keep_alive"])
        c["sock"].schreibe_bytes(baue_connack(0, 0))
    sonst wenn typ == PT_PUBLISH:
        setze pub auf parse_publish(p["body"], p["flags"])
        zeige "[" + text(cid) + "] PUBLISH " + pub["topic"] + " (" + text(länge(pub["payload"])) + " bytes)"
        setze n auf broker.leite_weiter(pub["topic"], pub["payload"])
        zeige "  -> " + text(n) + " Empfaenger"
    sonst wenn typ == PT_SUBSCRIBE:
        setze sub auf parse_subscribe(p["body"])
        setze granted auf []
        setze i auf 0
        solange i < länge(sub["topics"]):
            setze t auf sub["topics"][i]
            broker.abonniere(cid, t["topic"])
            zeige "[" + text(cid) + "] SUBSCRIBE " + t["topic"] + " qos=" + text(t["qos"])
            granted.hinzufügen(0)
            setze i auf i + 1
        c["sock"].schreibe_bytes(baue_suback(sub["packet_id"], granted))
    sonst wenn typ == PT_UNSUBSCRIBE:
        setze uns auf parse_unsubscribe(p["body"])
        setze i auf 0
        solange i < länge(uns["topics"]):
            broker.deabonniere(cid, uns["topics"][i])
            zeige "[" + text(cid) + "] UNSUBSCRIBE " + uns["topics"][i]
            setze i auf i + 1
        c["sock"].schreibe_bytes(baue_unsuback(uns["packet_id"]))
    sonst wenn typ == PT_PINGREQ:
        c["sock"].schreibe_bytes(baue_pingresp())
    sonst wenn typ == PT_DISCONNECT:
        zeige "[" + text(cid) + "] DISCONNECT"
        c["lebt"] = falsch
    sonst:
        zeige "[" + text(cid) + "] unbekannter Packet-Typ " + text(typ)

# Verarbeitet einen Client: liest verfuegbare Bytes, parst + dispatcht alle
# kompletten Pakete im Puffer. Markiert den Client tot bei IO-Error.
funktion poll_client(broker, c):
    wenn c["lebt"] == falsch:
        gib_zurück nichts
    versuche:
        setze neue auf c["sock"].lesen_bytes(4096)
        wenn länge(neue) > 0:
            bytes_anhaengen(c["buf"], neue)
        # Leere Antwort = entweder Timeout oder EOF. Wir koennen das ohne
        # zusaetzlichen Runtime-Hinweis nicht sauber unterscheiden, also
        # behandeln wir es als "no data" und vertrauen auf DISCONNECT /
        # Write-Fehler fuer echtes Cleanup.
    fange e:
        # timeout, kein Problem
        setze ignore auf 0
    setze off auf 0
    setze laeuft auf wahr
    solange laeuft:
        setze p auf parse_packet_buf(c["buf"], off)
        wenn p == nichts:
            setze laeuft auf falsch
        sonst:
            dispatch_paket(broker, c, p)
            setze off auf p["neue_off"]
    wenn off > 0:
        # Puffer kuerzen auf rest
        setze rest auf []
        setze i auf off
        solange i < länge(c["buf"]):
            rest.hinzufügen(c["buf"][i])
            setze i auf i + 1
        c["buf"] = rest

# ------------------------------------------------------------
# Server-Loop (polling)
# ------------------------------------------------------------

zeige "================================================"
zeige "  moo MQTT-Broker (MQTT 3.1.1)"
zeige "  Port: " + text(PORT)
zeige "================================================"

setze broker auf neu Broker()
setze server auf tcp_server(PORT)
server.timeout_setzen(50)
zeige "Listening on " + text(PORT) + " ..."

solange wahr:
    # Neue Verbindungen annehmen (non-blocking via timeout)
    versuche:
        setze neu_sock auf server.annehmen()
        neu_sock.timeout_setzen(10)
        setze c auf broker.add_client(neu_sock)
        zeige "[" + text(c["id"]) + "] neu verbunden"
    fange e:
        setze ignore auf 0

    # Alle aktiven Clients pollen
    setze i auf 0
    solange i < länge(broker.clients):
        poll_client(broker, broker.clients[i])
        setze i auf i + 1

    # Tote Clients aufraeumen
    broker.cleanup_tote()
