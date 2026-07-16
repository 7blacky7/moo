# Phase 3 T3 Pipelining-Bench (2026-05-07)
#
# Vergleicht 1000x sequential execute vs 1000x batch_prepared (alle in einem Pipeline-Batch).
# Erwartung: 2-3x Win durch Wegfall Per-Query-Roundtrips.
#
# HINWEIS: nutzt INSERT SELECT generate_series statt copy_in damit nicht zwei
# Volume-Operationen in einer moo-Process auflaufen — copy_in 1000 + batch
# 1000 zeigt einen moo-Runtime-Akkumulations-Bug (haengt). Separat OK.
#
# Reproduktion:
#   { head -n 1024 ../../beispiele/domain/db/postgres_client.moo;
#     cat bench_pipelining.moo;
#   } > /tmp/bench_pipelining_run.moo
#   ./compiler/target/release/moo-compiler run /tmp/bench_pipelining_run.moo

setze db auf neu PgClient("192.168.50.65", 5433, "moo_scram", "moo_scram", "xjJ6FnbwV9KX43nnBT9PoOHTuJVw")
db.verbinde()

db.query("DROP TABLE IF EXISTS bench_users")
db.query("CREATE UNLOGGED TABLE bench_users (id SERIAL PRIMARY KEY, name TEXT, age INT)")
db.query("INSERT INTO bench_users (name, age) SELECT 'U_' || g, 18 + (g % 60) FROM generate_series(0, 999) g")

db.prepare("by_name", "SELECT id, age FROM bench_users WHERE name = $1", [])

# A) Sequential execute (Phase 2 baseline)
setze t0 auf zeit_ms()
setze i auf 0
solange i < 1000:
    db.execute("by_name", ["U_" + text(i)])
    setze i auf i + 1
setze ms_seq auf zeit_ms() - t0
zeige "1000x sequential execute: " + text(ms_seq) + " ms (" + text(ms_seq / 1000.0) + " ms/q)"

# B) Pipelined batch
setze specs auf []
setze i auf 0
solange i < 1000:
    setze sp auf {}
    sp["stmt"] = "by_name"
    sp["params"] = ["U_" + text(i)]
    specs.hinzufügen(sp)
    setze i auf i + 1
setze t0 auf zeit_ms()
setze rs auf db.batch_prepared(specs)
setze ms_batch auf zeit_ms() - t0
zeige "1000x batch_prepared: " + text(ms_batch) + " ms (" + text(ms_batch / 1000.0) + " ms/q)"
zeige "Win pipelining: " + text(ms_seq / ms_batch) + "x"
zeige "Anzahl Ergebnisse: " + text(länge(rs))

db.query("DROP TABLE bench_users")
db.schliesse()
