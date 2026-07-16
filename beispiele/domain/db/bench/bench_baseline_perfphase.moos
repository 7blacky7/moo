# Baseline-Bench fuer Perf-Phase. Mit:
#   - TCP_NODELAY (T6 done)
#   - synchronous_commit=off (S1 done server-side)
#   - UNLOGGED TABLE (S2)
#
# Wird als Memory 'postgres-bench-baseline-perfphase' verewigt.
# Vergleich gegen postgres-bench-2026-05-06 zeigt Win von T6+S1+S2 isoliert.

importiere postgres_client_lib

setze HOST auf "192.168.50.65"
setze PORT auf 5433

funktion zeit_ms_diff(start_ms, end_ms):
    gib_zurück end_ms - start_ms

zeige "================================================"
zeige "  BASELINE-BENCH (T6+S1+S2, vor T1-T4)"
zeige "================================================"

# === Test 1: Connect-Latenz (Median 10 Versuche) ===
zeige ""
zeige "--- Connect-Latenz x10 (SCRAM) ---"
setze i auf 0
setze zeiten auf []
solange i < 10:
    setze t0 auf zeit_ms()
    setze c auf neu PgClient(HOST, PORT, "moo_scram", "moo_scram", "xjJ6FnbwV9KX43nnBT9PoOHTuJVw")
    c.verbinde()
    setze t1 auf zeit_ms()
    zeiten.hinzufügen(t1 - t0)
    c.schliesse()
    setze i auf i + 1
zeiten.sortieren()
zeige "  Connect-Median (SCRAM) = " + text(zeiten[5]) + " ms (alle: " + text(zeiten) + ")"

zeige ""
zeige "--- Connect-Latenz x10 (trust) ---"
setze i auf 0
setze zeitenT auf []
solange i < 10:
    setze t0 auf zeit_ms()
    setze c auf neu PgClient(HOST, PORT, "moo_trust", "moo_trust", "")
    c.verbinde()
    setze t1 auf zeit_ms()
    zeitenT.hinzufügen(t1 - t0)
    c.schliesse()
    setze i auf i + 1
zeitenT.sortieren()
zeige "  Connect-Median (trust) = " + text(zeitenT[5]) + " ms"

# === Lang-Connection fuer Query-Benches ===
setze db auf neu PgClient(HOST, PORT, "moo_scram", "moo_scram", "xjJ6FnbwV9KX43nnBT9PoOHTuJVw")
db.verbinde()

# === Setup: unlogged Bench-Tabelle ===
db.query("DROP TABLE IF EXISTS bench_unlogged")
db.query("CREATE UNLOGGED TABLE bench_unlogged (id SERIAL PRIMARY KEY, name TEXT, age INT)")

# === Test 2: 1000x SELECT 1 ===
zeige ""
zeige "--- 1000x SELECT 1 ---"
setze t0 auf zeit_ms()
setze i auf 0
solange i < 1000:
    setze r auf db.query("SELECT 1")
    setze i auf i + 1
setze t1 auf zeit_ms()
setze ms auf t1 - t0
zeige "  Total: " + text(ms) + " ms"
zeige "  Pro Query: " + text(ms / 1000.0) + " ms"

# === Test 3: 1000x INSERT (unlogged + synchronous_commit=off) ===
zeige ""
zeige "--- 1000x INSERT (unlogged) ---"
setze t0 auf zeit_ms()
setze i auf 0
solange i < 1000:
    db.query("INSERT INTO bench_unlogged (name, age) VALUES ('row" + text(i) + "', 30)")
    setze i auf i + 1
setze t1 auf zeit_ms()
setze ms auf t1 - t0
zeige "  Total: " + text(ms) + " ms"
zeige "  Pro INSERT: " + text(ms / 1000.0) + " ms"

# === Test 4: SELECT 1000 rows ===
zeige ""
zeige "--- SELECT 1000 rows ---"
setze t0 auf zeit_ms()
setze r auf db.query("SELECT * FROM bench_unlogged ORDER BY id")
setze t1 auf zeit_ms()
zeige "  Total: " + text(t1 - t0) + " ms"
zeige "  Rows zurueck: " + text(r["anzahl"])

# Cleanup
db.query("DROP TABLE bench_unlogged")
db.schliesse()

zeige ""
zeige "================================================"
zeige "  BASELINE Done"
zeige "================================================"
