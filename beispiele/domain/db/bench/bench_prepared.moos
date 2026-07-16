# Phase 2 T1 Prepared-Bench (2026-05-07)
#
# Vergleicht Simple-Query (String-Konkat) vs Extended-Query (Prepared+Bind+Execute)
# bei 1000 SELECT mit WHERE-Klausel auf realistischer Tabelle.
#
# Erwartung: Prepared 1.2x schneller + SQL-Injection-Schutz inklusive.
#
# Reproduktion:
#   { head -n 760 ../../beispiele/domain/db/postgres_client.moo;
#     cat bench_prepared.moo;
#   } > /tmp/bench_prepared_run.moo
#   ./compiler/target/release/moo-compiler run /tmp/bench_prepared_run.moo

setze db auf neu PgClient("192.168.50.65", 5433, "moo_scram", "moo_scram", "xjJ6FnbwV9KX43nnBT9PoOHTuJVw")
db.verbinde()

db.query("DROP TABLE IF EXISTS bench_users")
db.query("CREATE UNLOGGED TABLE bench_users (id SERIAL PRIMARY KEY, name TEXT, email TEXT, age INT)")
setze rows auf []
setze i auf 0
solange i < 1000:
    rows.hinzufügen(["U_" + text(i), "u" + text(i) + "@x.de", 18 + (i % 60)])
    setze i auf i + 1
db.copy_in("bench_users", ["name", "email", "age"], rows)

setze SQL auf "SELECT id, name, email, age FROM bench_users WHERE name = $1"

# Simple-Query Baseline: String-Konkat fuer Param
setze t0 auf zeit_ms()
setze i auf 0
solange i < 1000:
    db.query("SELECT id, name, email, age FROM bench_users WHERE name = 'U_" + text(i) + "'")
    setze i auf i + 1
setze ms_simple auf zeit_ms() - t0
zeige "1000x simple WHERE: " + text(ms_simple) + " ms (" + text(ms_simple / 1000.0) + " ms/q)"

# Prepared: 1x parse, 1000x bind+execute
db.prepare("by_name", SQL, [])
setze t0 auf zeit_ms()
setze i auf 0
solange i < 1000:
    db.execute("by_name", ["U_" + text(i)])
    setze i auf i + 1
setze ms_prep auf zeit_ms() - t0
zeige "1000x prepared WHERE: " + text(ms_prep) + " ms (" + text(ms_prep / 1000.0) + " ms/q)"

zeige "Win prepared: " + text(ms_simple / ms_prep) + "x"

db.query("DROP TABLE bench_users")
db.schliesse()
