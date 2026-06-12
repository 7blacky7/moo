importiere "../../beispiele/domain/db/postgres_client.moo"

setze HOST auf "192.168.50.65"
setze PORT auf 5433

setze ok auf 0
setze fail auf 0

funktion check(label, bedingung):
    wenn bedingung:
        zeige "OK   " + label
        setze ok auf ok + 1
    sonst:
        zeige "FAIL " + label
        setze fail auf fail + 1

zeige "================================================"
zeige "  SCRAM E2E: trust-Anker + SCRAM-SHA-256"
zeige "================================================"

# --- Trust-Anker: moo_trust ohne Passwort ---
zeige ""
zeige "--- moo_trust (trust, kein PW) ---"
setze t auf neu PgClient(HOST, PORT, "moo_trust", "moo_trust")
t.verbinde()
check("trust: ready", t.ready)
check("trust: backend_pid > 0", t.backend_pid > 0)
wenn t.ready:
    setze r auf t.query("SELECT 1 AS eins")
    check("trust: SELECT 1 wert", r["rows"][0]["eins"] == "1")
sonst:
    zeige "  letzte_fehler: " + text(t.letzte_fehler)
t.schliesse()

# --- SCRAM: moo_scram mit Passwort ---
zeige ""
zeige "--- moo_scram (SCRAM-SHA-256) ---"
setze s auf neu PgClient(HOST, PORT, "moo_scram", "moo_scram", "xjJ6FnbwV9KX43nnBT9PoOHTuJVw")
s.verbinde()
check("scram: ready", s.ready)
check("scram: backend_pid > 0", s.backend_pid > 0)
check("scram: server_signature verifiziert", s.scram_server_verified)
wenn s.ready:
    setze r auf s.query("SELECT 42 AS antwort")
    check("scram: SELECT 42 wert", r["rows"][0]["antwort"] == "42")
    setze r auf s.query("SELECT current_user AS u")
    check("scram: current_user=moo_scram", r["rows"][0]["u"] == "moo_scram")
sonst:
    zeige "  letzte_fehler: " + text(s.letzte_fehler)
s.schliesse()

# --- Negativ: SCRAM-User mit falschem Passwort ---
zeige ""
zeige "--- moo_scram mit falschem Passwort (muss fehlschlagen) ---"
setze bad auf neu PgClient(HOST, PORT, "moo_scram", "moo_scram", "FALSCH-PW-12345")
bad.verbinde()
check("falsches PW: NICHT ready", nicht bad.ready)
check("falsches PW: fehler gesetzt", bad.letzte_fehler != nichts)
wenn bad.letzte_fehler != nichts:
    zeige "  letzte_fehler: " + text(bad.letzte_fehler)
bad.schliesse()

zeige ""
zeige "================================================"
zeige "  Ergebnis: " + text(ok) + " ok / " + text(fail) + " fail"
zeige "================================================"
