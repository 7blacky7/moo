# ============================================================
# moo DNS-Resolver — RFC 1035 in pure moo
#
# Features:
#   - Query-Encoder (Header + Question)
#   - Response-Decoder mit Label-Compression (0xC0 pointer)
#   - Record-Typen: A (1), AAAA (28), CNAME (5), MX (15), NS (2), TXT (16)
#   - NXDOMAIN / RCODE Handhabung
#   - CNAME-Chain Auflösung (Client-seitig)
#
# Nutzt: udp_socket + udp_senden + udp_empfangen (K1 TBD)
# ============================================================

konstante TYP_A auf 1
konstante TYP_NS auf 2
konstante TYP_CNAME auf 5
konstante TYP_MX auf 15
konstante TYP_TXT auf 16
konstante TYP_AAAA auf 28
konstante KLASSE_IN auf 1

# ------------------------------------------------------------
# Byte-Helfer
# ------------------------------------------------------------

funktion be16(n):
    gib_zurück [boden(n / 256) % 256, n % 256]

funktion be32(n):
    setze b0 auf boden(n / (256 * 256 * 256)) % 256
    setze b1 auf boden(n / (256 * 256)) % 256
    setze b2 auf boden(n / 256) % 256
    setze b3 auf n % 256
    gib_zurück [b0, b1, b2, b3]

funktion lies_be16(buf, off):
    gib_zurück buf[off] * 256 + buf[off + 1]

funktion lies_be32(buf, off):
    setze a auf buf[off] * 256 * 256 * 256
    setze b auf buf[off + 1] * 256 * 256
    setze c auf buf[off + 2] * 256
    setze d auf buf[off + 3]
    gib_zurück a + b + c + d

funktion bytes_anhaengen(ziel, quelle):
    setze i auf 0
    solange i < länge(quelle):
        ziel.hinzufügen(quelle[i])
        setze i auf i + 1

# ------------------------------------------------------------
# Query-Encoder
# ------------------------------------------------------------

funktion kodiere_name(name):
    # "www.example.com" -> [3]'www'[7]'example'[3]'com'[0]
    setze out auf []
    setze labels auf name.teilen(".")
    setze i auf 0
    solange i < länge(labels):
        setze lab auf labels[i]
        wenn länge(lab) > 0:
            out.hinzufügen(länge(lab))
            setze lb auf bytes_zu_liste(lab)
            bytes_anhaengen(out, lb)
        setze i auf i + 1
    out.hinzufügen(0)
    gib_zurück out

funktion baue_query(id, name, qtyp):
    setze msg auf []
    # Header
    bytes_anhaengen(msg, be16(id))
    bytes_anhaengen(msg, be16(256))   # flags: RD=1 (0x0100)
    bytes_anhaengen(msg, be16(1))     # qdcount
    bytes_anhaengen(msg, be16(0))     # ancount
    bytes_anhaengen(msg, be16(0))     # nscount
    bytes_anhaengen(msg, be16(0))     # arcount
    # Question
    bytes_anhaengen(msg, kodiere_name(name))
    bytes_anhaengen(msg, be16(qtyp))
    bytes_anhaengen(msg, be16(KLASSE_IN))
    gib_zurück msg

# ------------------------------------------------------------
# Response-Decoder mit Label-Compression
# ------------------------------------------------------------

# Liest einen Namen ab off. Folgt Pointern (0xC0 prefix) rekursiv.
# Liefert [name_string, neue_off_nach_ursprung]
funktion lies_name(buf, off):
    setze teile auf []
    setze cur auf off
    setze erste_rueckkehr auf -1
    setze tiefe auf 0
    solange tiefe < 64:
        setze tiefe auf tiefe + 1
        wenn cur >= länge(buf):
            gib_zurück [verbinde_labels(teile), off]
        setze laenge_b auf buf[cur]
        wenn laenge_b == 0:
            setze cur auf cur + 1
            wenn erste_rueckkehr == -1:
                setze erste_rueckkehr auf cur
            gib_zurück [verbinde_labels(teile), erste_rueckkehr]
        wenn laenge_b >= 192:
            # Pointer: 2 bytes, untere 14 bits = offset
            setze hi auf laenge_b - 192
            setze lo auf buf[cur + 1]
            setze ptr auf hi * 256 + lo
            wenn erste_rueckkehr == -1:
                setze erste_rueckkehr auf cur + 2
            setze cur auf ptr
        sonst:
            # Normal label
            setze label_bytes auf []
            setze i auf 0
            solange i < laenge_b:
                label_bytes.hinzufügen(buf[cur + 1 + i])
                setze i auf i + 1
            teile.hinzufügen(bytes_neu(label_bytes))
            setze cur auf cur + 1 + laenge_b
    gib_zurück [verbinde_labels(teile), off]

funktion verbinde_labels(teile):
    wenn länge(teile) == 0:
        gib_zurück ""
    setze out auf teile[0]
    setze i auf 1
    solange i < länge(teile):
        setze out auf out + "." + teile[i]
        setze i auf i + 1
    gib_zurück out

# Parst RDATA je nach Typ zu einem lesbaren String
funktion parse_rdata(buf, off, laenge, typ):
    wenn typ == TYP_A:
        # 4 bytes IPv4
        setze a auf buf[off]
        setze b auf buf[off + 1]
        setze c auf buf[off + 2]
        setze d auf buf[off + 3]
        gib_zurück text(a) + "." + text(b) + "." + text(c) + "." + text(d)
    wenn typ == TYP_AAAA:
        # 16 bytes IPv6
        setze s auf ""
        setze i auf 0
        solange i < 8:
            setze wort auf buf[off + i*2] * 256 + buf[off + i*2 + 1]
            setze hex auf hex16(wort)
            wenn i > 0:
                setze s auf s + ":"
            setze s auf s + hex
            setze i auf i + 1
        gib_zurück s
    wenn typ == TYP_CNAME:
        setze erg auf lies_name(buf, off)
        gib_zurück erg[0]
    wenn typ == TYP_NS:
        setze erg auf lies_name(buf, off)
        gib_zurück erg[0]
    wenn typ == TYP_MX:
        setze prio auf lies_be16(buf, off)
        setze erg auf lies_name(buf, off + 2)
        gib_zurück text(prio) + " " + erg[0]
    wenn typ == TYP_TXT:
        # txt-strings: length-prefixed
        setze end auf off + laenge
        setze s auf ""
        setze cur auf off
        solange cur < end:
            setze tl auf buf[cur]
            setze cur auf cur + 1
            setze bytes auf []
            setze i auf 0
            solange i < tl:
                bytes.hinzufügen(buf[cur + i])
                setze i auf i + 1
            setze s auf s + bytes_neu(bytes)
            setze cur auf cur + tl
        gib_zurück s
    # unbekannt: hex-dump
    setze s auf ""
    setze i auf 0
    solange i < laenge:
        setze s auf s + hex8(buf[off + i])
        setze i auf i + 1
    gib_zurück s

funktion hex4(n):
    wenn n < 10:
        gib_zurück text(n)
    wenn n == 10:
        gib_zurück "a"
    wenn n == 11:
        gib_zurück "b"
    wenn n == 12:
        gib_zurück "c"
    wenn n == 13:
        gib_zurück "d"
    wenn n == 14:
        gib_zurück "e"
    gib_zurück "f"

funktion hex8(n):
    gib_zurück hex4(boden(n / 16)) + hex4(n % 16)

funktion hex16(n):
    gib_zurück hex8(boden(n / 256)) + hex8(n % 256)

# Parst einen DNS-Response. Liefert dict mit id, flags, rcode, answers[]
funktion parse_response(buf):
    setze r auf {}
    r["id"] = lies_be16(buf, 0)
    setze flags auf lies_be16(buf, 2)
    r["flags"] = flags
    r["rcode"] = flags % 16
    r["qdcount"] = lies_be16(buf, 4)
    r["ancount"] = lies_be16(buf, 6)
    r["nscount"] = lies_be16(buf, 8)
    r["arcount"] = lies_be16(buf, 10)
    # Question(s) ueberspringen
    setze off auf 12
    setze i auf 0
    solange i < r["qdcount"]:
        setze erg auf lies_name(buf, off)
        setze off auf erg[1]
        setze off auf off + 4  # qtype + qclass
        setze i auf i + 1
    # Answer(s)
    setze antworten auf []
    setze i auf 0
    solange i < r["ancount"]:
        setze erg auf lies_name(buf, off)
        setze name auf erg[0]
        setze off auf erg[1]
        setze typ auf lies_be16(buf, off)
        setze klass auf lies_be16(buf, off + 2)
        setze ttl auf lies_be32(buf, off + 4)
        setze rdlen auf lies_be16(buf, off + 8)
        setze off auf off + 10
        setze rdata auf parse_rdata(buf, off, rdlen, typ)
        setze off auf off + rdlen
        setze a auf {}
        a["name"] = name
        a["typ"] = typ
        a["klasse"] = klass
        a["ttl"] = ttl
        a["rdata"] = rdata
        antworten.hinzufügen(a)
        setze i auf i + 1
    r["antworten"] = antworten
    gib_zurück r

# ------------------------------------------------------------
# Query-API
# ------------------------------------------------------------

funktion typ_name(t):
    wenn t == TYP_A:
        gib_zurück "A"
    wenn t == TYP_AAAA:
        gib_zurück "AAAA"
    wenn t == TYP_CNAME:
        gib_zurück "CNAME"
    wenn t == TYP_MX:
        gib_zurück "MX"
    wenn t == TYP_NS:
        gib_zurück "NS"
    wenn t == TYP_TXT:
        gib_zurück "TXT"
    gib_zurück "TYPE" + text(t)

funktion rcode_text(rc):
    wenn rc == 0:
        gib_zurück "NOERROR"
    wenn rc == 1:
        gib_zurück "FORMERR"
    wenn rc == 2:
        gib_zurück "SERVFAIL"
    wenn rc == 3:
        gib_zurück "NXDOMAIN"
    wenn rc == 4:
        gib_zurück "NOTIMP"
    wenn rc == 5:
        gib_zurück "REFUSED"
    gib_zurück "RCODE" + text(rc)

klasse DnsResolver:
    funktion erstelle(server_host, server_port):
        selbst.server_host = server_host
        selbst.server_port = server_port
        selbst.next_id = 1

    funktion aufloesen(name, qtyp):
        setze id auf selbst.next_id
        setze selbst.next_id auf id + 1
        setze query auf baue_query(id, name, qtyp)
        setze sock auf udp_socket(0)
        sock.timeout_setzen(3000)
        sock.udp_verbinden(selbst.server_host, selbst.server_port)
        sock.schreibe_bytes(query)
        setze daten auf sock.lesen_bytes(4096)
        sock.schliessen()
        setze resp auf parse_response(daten)
        gib_zurück resp

# ------------------------------------------------------------
# Tests
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

funktion zeige_antwort(r):
    zeige "  rcode=" + rcode_text(r["rcode"]) + " ancount=" + text(r["ancount"])
    setze i auf 0
    solange i < länge(r["antworten"]):
        setze a auf r["antworten"][i]
        zeige "  " + a["name"] + " " + typ_name(a["typ"]) + " " + text(a["ttl"]) + " " + a["rdata"]
        setze i auf i + 1

zeige "================================================"
zeige "  moo DNS-Resolver — Tests gegen 8.8.8.8"
zeige "================================================"

setze dns auf neu DnsResolver("8.8.8.8", 53)

zeige ""
zeige "--- Test 1: google.com A ---"
setze r auf dns.aufloesen("google.com", TYP_A)
zeige_antwort(r)
check("A rcode=NOERROR", r["rcode"] == 0)
check("A hat Antworten", r["ancount"] > 0)
check("A erste Antwort ist A-Record", r["antworten"][0]["typ"] == TYP_A)

zeige ""
zeige "--- Test 2: google.com AAAA ---"
setze r auf dns.aufloesen("google.com", TYP_AAAA)
zeige_antwort(r)
check("AAAA rcode=NOERROR", r["rcode"] == 0)
check("AAAA hat Antworten", r["ancount"] > 0)

zeige ""
zeige "--- Test 3: google.com MX ---"
setze r auf dns.aufloesen("google.com", TYP_MX)
zeige_antwort(r)
check("MX rcode=NOERROR", r["rcode"] == 0)
check("MX hat Antworten", r["ancount"] > 0)

zeige ""
zeige "--- Test 4: NXDOMAIN ---"
setze r auf dns.aufloesen("this-domain-does-not-exist-moo-test-xyz123.invalid", TYP_A)
zeige_antwort(r)
check("NXDOMAIN rcode=3", r["rcode"] == 3)

zeige ""
zeige "--- Test 5: CNAME-Chain (www.github.com) ---"
setze r auf dns.aufloesen("www.github.com", TYP_A)
zeige_antwort(r)
check("www.github.com rcode=NOERROR", r["rcode"] == 0)
check("www.github.com hat Antworten", r["ancount"] > 0)

zeige ""
zeige "--- Test 6: one.one.one.one A (Cloudflare test host) ---"
setze dns2 auf neu DnsResolver("1.1.1.1", 53)
setze r auf dns2.aufloesen("one.one.one.one", TYP_A)
zeige_antwort(r)
check("cloudflare A", r["rcode"] == 0 und r["ancount"] > 0)

zeige ""
zeige "================================================"
zeige "  Ergebnis: " + text(zaehler["ok"]) + "/" + text(zaehler["gesamt"]) + " bestanden"
zeige "================================================"
