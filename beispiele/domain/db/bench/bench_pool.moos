# Phase 5 C1 PgPool-Bench (2026-05-07)
#
# Vergleicht 100x fresh-connect+query+close vs 100x pool-checkout+query+checkin.
# Erwartung: 40-50x Win durch eliminierte SCRAM-Auth-Roundtrips.
#
# Reproduktion:
#   { head -n 1024 ../../beispiele/domain/db/postgres_client.moo;
#     cat bench_pool.moo;
#   } > /tmp/bench_pool_run.moo
#   ./compiler/target/release/moo-compiler run /tmp/bench_pool_run.moo

# Variante A: 100x fresh PgClient
setze t0 auf zeit_ms()
setze i auf 0
solange i < 100:
    setze c auf neu PgClient("192.168.50.65", 5433, "moo_scram", "moo_scram", "xjJ6FnbwV9KX43nnBT9PoOHTuJVw")
    c.verbinde()
    c.query("SELECT 1")
    c.schliesse()
    setze i auf i + 1
setze ms_fresh auf zeit_ms() - t0
zeige "100x fresh: " + text(ms_fresh) + " ms (" + text(ms_fresh / 100.0) + " ms/op)"

# Variante B: Pool
setze p auf neu PgPool("192.168.50.65", 5433, "moo_scram", "moo_scram", "xjJ6FnbwV9KX43nnBT9PoOHTuJVw", 4)
setze t0 auf zeit_ms()
setze i auf 0
solange i < 100:
    setze c auf p.checkout()
    c.query("SELECT 1")
    p.checkin(c)
    setze i auf i + 1
setze ms_pool auf zeit_ms() - t0
zeige "100x pool: " + text(ms_pool) + " ms (" + text(ms_pool / 100.0) + " ms/op)"
zeige "Win pool: " + text(ms_fresh / ms_pool) + "x"

p.schliesse_alle()
