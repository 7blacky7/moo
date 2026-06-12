# Phase 1 T2 COPY-Bench (final, 2026-05-07).
#
# Stand: copy_in nutzt Inline-Frames (kein PgMessage-Subklassen-Wrapper)
# wegen moo-codegen-subclass-instance-double-release-Bug. Dadurch
# deterministisch grün:
#   - Native: 10/10 grün, 4.35-5.48 ms (Median 4.74 ms = 32.6x Win)
#   - ASan:   10/10 grün, ~7-8 ms (mit ASan-Overhead)
#
# Reproduktion:
#   { head -n 592 ../../beispiele/domain/db/postgres_client.moo;
#     cat bench_copy.moo;
#   } > /tmp/bench_copy_run.moo
#   ./compiler/target/release/moo-compiler run /tmp/bench_copy_run.moo

setze HOST auf "192.168.50.65"
setze PORT auf 5433

setze db auf neu PgClient(HOST, PORT, "moo_scram", "moo_scram", "xjJ6FnbwV9KX43nnBT9PoOHTuJVw")
db.verbinde()

setze zeilen auf []
setze i auf 0
solange i < 1000:
    zeilen.hinzufügen(["Name_" + text(i), 20 + (i % 60)])
    setze i auf i + 1

db.query("DROP TABLE IF EXISTS bench_copy_unlogged")
db.query("CREATE UNLOGGED TABLE bench_copy_unlogged (name TEXT, age INT)")
setze t0 auf zeit_ms()
setze cnt auf db.copy_in("bench_copy_unlogged", ["name", "age"], zeilen)
setze ms auf zeit_ms() - t0
zeige "UNLOGGED COPY: " + text(cnt) + " rows in " + text(ms) + " ms (" + text(ms / 1000.0) + " ms/row)"
db.query("DROP TABLE bench_copy_unlogged")

db.schliesse()
