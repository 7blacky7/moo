# Phase 4 T4 Welle 1 Bench (2026-05-07)
#
# Vergleicht Simple-Query TEXT vs Prepared BINARY bei 1000 rows.
# Korrektheit OK (ASan-grün), Performance neutral bis -10% (Decode in pure moo
# ist teurer als bytes_neu-Builtin im Text-Pfad).
#
# Reproduktion:
#   { head -n 978 ../../beispiele/domain/db/postgres_client.moo;
#     cat bench_binary.moo;
#   } > /tmp/bench_binary_run.moo
#   ./compiler/target/release/moo-compiler run /tmp/bench_binary_run.moo

setze db auf neu PgClient("192.168.50.65", 5433, "moo_scram", "moo_scram", "xjJ6FnbwV9KX43nnBT9PoOHTuJVw")
db.verbinde()

db.query("DROP TABLE IF EXISTS bench_bin")
db.query("CREATE UNLOGGED TABLE bench_bin (id SERIAL PRIMARY KEY, name TEXT, age INT, aktiv BOOL)")
db.query("INSERT INTO bench_bin (name, age, aktiv) SELECT 'Name_' || g, 18 + (g % 60), g % 2 = 0 FROM generate_series(1, 1000) g")

# Text via simple query
setze t0 auf zeit_ms()
setze r auf db.query("SELECT id, name, age, aktiv FROM bench_bin ORDER BY id")
setze ms_simple auf zeit_ms() - t0
zeige "1000 rows simple TEXT: " + text(ms_simple) + " ms (anzahl=" + text(r["anzahl"]) + ")"

# Binary via prepared
db.prepare("get_all", "SELECT id, name, age, aktiv FROM bench_bin ORDER BY id", [])
setze t0 auf zeit_ms()
setze r auf db.execute("get_all", [])
setze ms_bin auf zeit_ms() - t0
zeige "1000 rows BINARY: " + text(ms_bin) + " ms (anzahl=" + text(r["anzahl"]) + ")"
zeige "Win binary: " + text(ms_simple / ms_bin) + "x"

# Type-Korrektheit Sample
zeige "row[0] id=" + text(r["rows"][0]["id"]) + " (Number, nicht String)"
zeige "row[0] aktiv=" + text(r["rows"][0]["aktiv"]) + " (Bool, nicht String)"

db.query("DROP TABLE bench_bin")
db.schliesse()
