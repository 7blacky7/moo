# Test-Team moo — Protokoll (gueltig ab 2026-07-14)

GPT-Agenten sind bis 19.07.2026 geblockt. Bis dahin gilt dieses Setup:

- **koordinator** (Fable, diese Session): macht ALLE Programmierung, Datei-Aenderungen, Commits.
- **test-linux** (Haiku-Spezialist): fuehrt NUR auf Kommando Tests lokal auf Linux aus. Briefing: `docs/agenten/test-linux.md`
- **test-windows** (Haiku-Spezialist): fuehrt NUR auf Kommando Tests auf/gegen die Windows-VM aus. Briefing: `docs/agenten/test-windows.md`

**Channel:** `moo-testteam` — alle Team-Kommunikation laeuft hier.

## Grundregeln (fuer beide Spezialisten, PFLICHT)

1. **NIE ungefragt testen.** Ihr wartet, bis der koordinator einen AUFTRAG postet. Kein proaktives Bauen, kein Aufraeumen, keine eigenen Ideen ausfuehren.
2. **KEINE Code-Aenderungen.** Ihr aendert keine Dateien, macht keine Commits, keine git-Operationen ausser lesen (`git log`, `git status`, `git rev-parse HEAD`). Bugs werden GEMELDET, nicht gefixt.
3. **Keine Workarounds.** Wenn ein Test rot ist, meldet ihr ihn rot — mit rohem stderr/stdout-Auszug. Niemals Testbedingungen abschwaechen.
4. **Ehrliche Evidenz.** Jede Meldung enthaelt: HEAD-SHA, Kommando, RC, PASS/FAIL-Counts, Log-Auszug bei Fehlern.
5. **Ressourcen:** Windows-VM NIEMALS starten/stoppen/rebooten (laeuft dauerhaft). Auf dem Linux-Host (KDE/Wayland) KEINE nativen UI-Fenster, keine Input-Injektion, keine Screenshots des Hosts — nur die isolierten Harness-Tasks aus dem Briefing.
6. **Abmelden nur** wenn der koordinator im Channel `Du darfst dich abmelden` an euch schreibt.

## Auftragsformat (Koordinator → Spezialist)

Der koordinator postet im Channel `moo-testteam`:

```
AUFTRAG <agent> <auftrag-id>: <task1> [<task2> ...] [HINWEIS: ...]
```

Beispiel: `AUFTRAG test-linux A007: test-ui-moo-effects test-ui-moo-compositor`

Nur der adressierte Agent fuehrt aus. Tasks sind die Namen aus dem jeweiligen Briefing (Abschnitt "Erlaubte Tests").

## Ergebnisformat (Spezialist → Channel)

Sofort nach Abschluss ALLER Tasks eines Auftrags EIN Post:

```
ERGEBNIS <auftrag-id> <agent>: <GESAMT: PASS|FAIL>
HEAD=<sha> HOST=<linux|winvm>
- <task>: rc=<n> <PASS|FAIL> [<kurz-zusammenfassung, 1 Zeile>]
...
FEHLER-DETAIL (nur bei FAIL): <letzte ~15 relevante Zeilen stderr/stdout>
```

Bei Auftraegen die laenger als ~10 Minuten laufen: kurzer Zwischenstand-Post (`STATUS <auftrag-id>: <task> laeuft seit Xmin`).

## Synapse-Pflichten

- Onboarding beim Start: `admin(action:'index_stats', project:'moo', agent_id:'<dein-name>', role:'spezialist')`, dann `channel(join, moo-testteam)`, dann `channel(feed)` lesen.
- `agent_id: '<dein-name>'` an JEDEN Synapse-Call.
- **Alle Test-/Shell-Kommandos AUSSCHLIESSLICH ueber das Synapse `shell`-Tool ausfuehren** (`shell(action:'exec', project:'moo', target:'local', agent_id:'<dein-name>', timeout_ms: <grosszuegig>)`) — NICHT ueber das eigene Bash-Tool. Der Koordinator auditiert jeden Lauf via `shell(history/activity)`; nicht-attribuierte Laeufe gelten als nicht passiert.
- Events: `event(pending)` regelmaessig pruefen, JEDES Event sofort `ack`en und befolgen (WORK_STOP = sofort anhalten).
- Warte-Loop im Idle: Channel-Feed + Events pollen (alle ~30-60s), max 3000 Zyklen, Reset bei jeder Nachricht.
