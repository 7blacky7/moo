# PerfPhase Postgres — Handoff für nächste Session

**Stand:** 2026-05-07
**Branch:** `nacht-session/moo-gtk-event-hooks` (NICHT passend für diese Arbeit, neuer Branch nötig)

## TL;DR — Wie weitermachen

```bash
cd /home/blacky/dev/moo

# 1. Status check
git status
git log --oneline -10

# 2. Alle 5 Bench-Files laufen lassen (sollten alle grün sein)
SMOKE_LINE=$(grep -n "^# Smoketest" beispiele/domain/db/postgres_client.moo | head -1 | cut -d: -f1)
LIB_END=$((SMOKE_LINE - 1))
for name in bench_copy bench_prepared bench_pipelining bench_binary bench_pool; do
    echo "=== $name ==="
    { head -n "$LIB_END" beispiele/domain/db/postgres_client.moo; cat scratch/db_bench/$name.moo; } > /tmp/run.moo
    ./compiler/target/release/moo-compiler run /tmp/run.moo
done

# 3. Memories laden
mcp__synapse__memory action=read name=session-handoff-perfphase-2026-05-07
mcp__synapse__memory action=read name=postgres-bench-2026-05-06
```

## Was abgeschlossen ist

| Phase | Win | Memory |
|---|---|---|
| 0: TCP_NODELAY + SCRAM + SHA256-K5-Fix | -34% Connect | `postgres-scram-implementiert` |
| 1: COPY FROM STDIN | **32.6×** INSERT | `postgres-t2-copy-done` |
| 2: Prepared + SQL-Injection-Schutz | 1.18× | `postgres-t1-prepared-done` |
| 3: batch_prepared Pipelining | **2.3×** | `postgres-t3-pipelining-done` |
| 4: Binary Format Codes Welle 1 | 0.94× (Type-Win) | `postgres-t4-binary-welle1-done` |
| 5: PgPool | **45.7×** Connect-Heavy | `postgres-c1-pgpool-done` |

**Aggregate Web-Workload:** 3318 ms → 206 ms = **~16×**

## Noch zu tun (User-Aktion erforderlich)

### A) Branch + Commits (HOCH-PRIO)

Aktueller Branch ist UI-bezogen — neuer empfohlen:

```bash
git checkout master
git pull
git checkout -b perfphase-postgres-2026-05
```

**7 thematische Commits** (konventionelle Commits auf Deutsch, Autor `Moritz Kolar <moritz.kolar@gmail.com>`, **NIE** `git add -A`):

1. `feat(runtime): TCP_NODELAY in tcp_verbinde` — `compiler/runtime/moo_net.c`
2. `fix(runtime): SHA-256 K[5] Tippfehler-Bugfix` — `compiler/runtime/moo_crypto.c` (nur die K-Konstante)
3. `feat(runtime+codegen): SCRAM-Crypto-Primitives` — `moo_crypto.c` + `moo_runtime.h` + `runtime_bindings.rs` + `codegen.rs` (crypto-aliases)
4. `feat(compiler): Lexer Multi-Line-Argument-Listen via Bracket-Depth` — `compiler/src/lexer.rs`
5. `fix(compiler): User-Method execute/ausfuehren respektiert sqlite-stmt-Shadow nicht` — `compiler/src/codegen.rs` (nur execute-Block)
6. `feat(beispiele/db): postgres_client SCRAM + COPY + Prepared + Pipelining + Binary + Pool` — `beispiele/domain/db/postgres_client.moo`
7. `docs(scratch): perfphase bench-files + README` — `scratch/db_bench/*` (nur falls scratch in den Branch soll, sonst skip; ist `.synapseignore`-d)

### B) Compiler-Bug-Fixes (Folge-Arbeiten, eigene Sessions)

Drei Codegen-Bugs dokumentiert (mit Workarounds in der Library):

1. **`moo-codegen-subclass-instance-double-release-2026-05-07`** — `setze X auf neu PgMessage-Subklasse(...)` in Methoden mit Function-Exit-Cleanup → Double-Release. Workaround: Inline-Frame-Pattern.
2. **`moo-codegen-method-slot-reuse-volume-2026-05-07`** — `selbst.method_xy(...)` in Hot-Loops > 80 Iter → Heap-Korruption. Workaround: freie Funktionen statt Methoden.
3. **`moo-codegen-default-params-class-methods`** — Default-Parameter auf Klassen-Methoden brechen LLVM-Verifikation. Workaround: Pflicht-Parameter.

Gemeinsamer Fix: `compile_function_def` + `compile_return` neu strukturieren als single-function-exit-BB Pattern.

### C) Phase 6 C3 Async (eigene Phase)

Non-blocking TCP + Compiler async/await für parallele Worker-Pools. ~2-3 Tage, separater Branch+PR.

### D) Skip-Entscheidungen aus PerfPhase

- **Binary Welle 1.5/2** (int8/timestamp/bytea/uuid/float8): Skip — pure-moo Decode bringt keine Performance ohne C-Side Builtins.
- **D1 libpq-Binding**: Skip — pure-moo schlägt postgres.js bei 4/5 Tests, libpq-Aufwand nicht gerechtfertigt.

## Wichtige Konventionen / Fallen

1. **Subklassen-Pattern**: Niemals `setze X auf neu PgMessage-Subklasse(...)` in Hot-Loops. Inline-Frames stattdessen.
2. **Method-Volume**: Helfer in 80+ Iterationen müssen freie Funktionen sein.
3. **Lib-Cut**: head -n bei `SMOKE_LINE - 1` (aktuell ~1036). **Dynamisch finden**, nicht hardcoden!
4. **ASan ready**: `PATH=/tmp/asan-wrap:$PATH` + `ASAN_OPTIONS='abort_on_error=1:halt_on_error=1:detect_leaks=0'`. Build-Setup-Doku in `moo-codegen-method-slot-reuse-volume-2026-05-07`.
5. **moo-Parser** validiert Klassen-Enden NICHT. Falscher head-Cut → silent ungültige Tests.

## Test-Server

- Server: `192.168.50.65:5433` (postgresql16_2 auf Unraid)
- User: `moo_trust` (trust) + `moo_scram` (SCRAM, PW in `postgres-projekt-db-setup` Memory) + `moo_bench` (legacy)
- Server-Setup: `synchronous_commit=off` auf allen drei DBs (vom Koordinator gesetzt).

## Kommunikation

Diese Session lief mit Koordinator-PID `1312854`. In der nächsten Session ist die PID anders. Synapse-Memories sind PID-unabhängig.

`cc-send <PID> 'Nachricht'` für Koordinator-Kommunikation. PID erkennen aus Session-Start-Hook (`WRAPPER_PID`).
