# Postgres-Performance-Phase Bench-Files

Dokumentation der Bench-Methodik nach Phase-0/Phase-1 (2026-05-06).

## Server-Setup

- 192.168.50.65:5433 (postgresql16_2 auf Unraid, getuned)
- `synchronous_commit=off` auf moo_scram, moo_trust, moo_bench (S1)
- Bench-Tabellen: `UNLOGGED` (S2 in jedem Bench-File)
- Doppel-User-Setup: moo_trust (trust-Auth, Wire-Baseline) + moo_scram (SCRAM, Production-Path)
- TCP_NODELAY in `compiler/runtime/moo_net.c::moo_tcp_connect` (T6, wirkt -34% auf trust-Connect)

## Bench-Files

| Datei | Was | Erwartung (Median) |
|---|---|---|
| `bench_baseline_perfphase.moo` | Connect/SELECT/INSERT/SELECT-1000 ohne T1-T4 | siehe Memory `postgres-bench-baseline-perfphase` |
| `bench_copy.moo` | T2 COPY UNLOGGED 1000 rows | 5.26 ms = 29.4x Win vs Baseline (5/10 erfolgreich) |
| `scram_crypto_smoke.moo` | RFC-Vektoren fuer hmac/pbkdf2/sha256 (Phase 0) | 5/5 OK |

## Reproduktion

Da `importiere postgres_client` nicht geht (Smoke-Block am Datei-Ende
wuerde mit-importieren), nutzen die Benches `head -n 587 ... | cat` zum
Konkatenieren der Library mit dem Bench-Body.

**Wichtig:** `head -n 587` ist der korrekte Cut, weil PgClient.copy_in
body bis Zeile 587 reicht. Ein zu fruher Cut (z.B. 548) schneidet die
Klasse mitten ab — moo-Parser toleriert das silent (keine
Klassenenden-Validierung), Tests werden dadurch ungueltig (False-Positives
moeglich).

```bash
{ head -n 587 ../../beispiele/domain/db/postgres_client.moo;
  cat bench_copy.moo;
} > /tmp/bench_copy_run.moo
./compiler/target/release/moo-compiler run /tmp/bench_copy_run.moo
```

## Bekannter Bug

`moo-runtime-corrupted-double-linked-list-2026-05-06`: Non-deterministisch
Heap-Korruption bei copy_in (~50% Crash-Rate, 5-row gleich wie 1000-row).
Bench-Werte aus erfolgreichen Runs sind valide (alle ~4-6ms). T1 Prepared
blockiert bis Bug eingegrenzt (ASan-Build im Gange durch koordinator).
