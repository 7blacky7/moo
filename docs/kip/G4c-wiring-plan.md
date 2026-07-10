# KIP-G4c — Wiring-Plan (SUPERSEDED)

**Dieses Dokument ist überholt.** Kanonisches Deliverable fuer KIP-G4c Phase 1:
`docs/kip/G4c-production-wiring-plan.md` + `skripte/kip_g4c_gate.sh` (Commit `f6b20d9`).

## Hintergrund

Nach einem Context-Reset liefen kurzzeitig zwei kip-gpu-Instanzen parallel (Respawn-Race)
und haben unabhängig voneinander dasselbe Phase-1-Prework fuer [KIP-G4c] ab09b47c
erstellt (dieses Dokument, Commit `e29783c`, und das kanonische Dokument, Commit
`f6b20d9`, beide direkt nacheinander auf `master`). Beide sind inhaltlich sehr ähnlich
(Symbol-/Callgraph-Inventar, STRIKT-Vertrag-Entwurf, Telemetrie-/Gate-Design).

## Korrektur (wichtig)

Dieses Dokument enthielt in §1.4/§8 einen **faktischen Fehler**: die Annahme, `MooTensor`
halte keinen GPU-Buffer-Handle und benötige entweder eine Side-Table oder eine
Struct-Erweiterung durch kip-kern. **Das ist falsch** — verifiziert gegen
`compiler/runtime/moo_runtime.h:140-157` (Commit `KIP-STRUCT f2cbebc7`, vor meiner Session
bereits gemergt): `MooTensor` hat bereits `gpu_buf`/`gpu_grad`/`valid`/`grad_valid`/
`device`-Felder inkl. `MOO_V_DEV`-Bitmaske fuer GPU-Residenz-Tracking. **Kein
Struct-Redesign nötig** — die Residenz-Bruecke existiert schon, Phase 2 nutzt sie direkt
(Resident-Buffer-Lebenszyklus ist im kanonischen Dokument beschrieben:
`res_ensure(t) = buf_belegen+upload wenn !MOO_V_DEV; op_res; o->valid=MOO_V_DEV`).

## Weiterarbeit

Siehe `docs/kip/G4c-production-wiring-plan.md` fuer die vollständige, verifizierte
Schnittstellen-Inventur (exakte Zeilen gegen HEAD `87ef09e`) und `skripte/kip_g4c_gate.sh`
fuer das Abnahme-Gate-Skelett (6 Kriterien). Phase 2 bleibt blockiert bis kip-ops (B4b)
FREI meldet fuer `moo_nn.c`/`moo_autograd.c`/`moo_tensor_ops.c`/`moo_runtime.h`.
