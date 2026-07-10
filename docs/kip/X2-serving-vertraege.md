# KIP-X2 — Serving-Design: Verträge für B2/T3 (Streaming, Batching, Paged-KV)

Stand: 2026-07-10. Task fcf9e0a9. Status: Entwurf, GPT-Review pending.
NUR Design — kein Code. Liefert bindende Schnittstellen-Verträge an KIP-B2
(RoPE/Cache) und KIP-T3 (Stop-Tokens), BEVOR diese implementiert werden.

## 1. Scope-Abgrenzung Runtime vs Userland
- moo-RUNTIME (C11) liefert: Cache-Primitiven, Sampling-Primitiven, inkrementellen
  Decode-Schritt, Abbruch-Flag. Alles synchron, sessionlos, zustandslos außer
  explizit übergebenen Handles.
- moo-USERLAND (.moo-Programme) baut daraus: HTTP-Server (moo_web.c-Bestand:
  eigener Webserver existiert), Request-Scheduler, Session-Verwaltung,
  Continuous Batching. Begründung: Scheduler-Logik ist Policy, nicht Mathematik —
  sie gehört in die Sprache, nicht in die C-Runtime. Das hält die Runtime testbar.

## 2. VERTRAG AN KIP-B2 (bindend)

### 2.1 Cache-Layout
- KV-Cache bleibt pro Attention-Schicht im Layer-Dict (Bestandsmuster), ABER:
  Cache-Zustand wird als eigenständiges, extrahierbares Handle-Objekt definierbar
  (`cache_zustand(netz)` → Dict; `cache_setzen(netz, zustand)`), damit Userland
  Sessions verwalten kann  (Session = Zustand rein/raus, Runtime bleibt sessionlos).
- OWNERSHIP-SEMANTIK (Pflicht, GPT 49573fb4): der extrahierte Zustand ist
  DETACHED und +1-owning — KEIN stilles mutable Aliasing zwischen Netz und
  Session-Handle. Prefix-Clone = tiefe Kopie in v1 (immutable/refcounted
  Sharing erst als spätere Optimierung mit eigenem Vertrag).
  `cache_setzen` validiert: Layerzahl, Shapes, DTypes UND RoPE-Konfiguration
  — jeder Mismatch wirft erklärend.
- Cache speichert ROTIERTE K (RoPE angewandt beim Schreiben). Konsequenz: ein
  Cache ist an seine RoPE-Konfiguration (base, dim, ab B2b scaling) gebunden —
  diese Felder MÜSSEN im Cache-Zustand mitgeführt und beim `cache_setzen`
  validiert werden (Mismatch → erklärender Fehler).

### 2.2 Positions-Offsets
- Der bestehende Offset `t_alt` (moo_nn.c:642ff, heute nur Masken-Offset) wird
  zur EINZIGEN Positionsquelle: RoPE-Rotation für neue Tokens nutzt Position
  `t_alt + i`. Kein zweiter Positionszähler. B2-Gate "chunked Prefill ==
  Vollsequenz" beweist genau diesen Vertrag.
- Positions-Reset pro Dokument (B4a) und Cache-Reset (bestehendes
  `cache_leeren`) setzen `t_alt` deterministisch.

### 2.3 Chunked-Prefill-API
- `vorwaerts(netz, block)` mit aktivem Cache MUSS für beliebige Chunk-Größen
  (1..block) dasselbe Ergebnis liefern wie ein Voll-Prefill. Das ist der
  Grundbaustein für Streaming und späteres Batching — B2 implementiert und
  beweist es, X2-Umsetzung konsumiert es nur.

### 2.4 Paged KV (nur Richtung, NICHT B2-Scope)
- B2 baut den Cache als zusammenhängende Tensoren (einfach, korrekt).
- Paged KV (Blöcke à z.B. 64 Positionen, Freiliste, Prefix-Sharing) ist
  Serving-Umsetzungsphase. B2 muss dafür NUR garantieren: Cache-Zugriff läuft
  über eine interne Lese-/Schreibfunktion (ein Indirektionspunkt), nicht über
  verstreute Direktzugriffe — dann ist Paging später ein lokaler Umbau.

## 3. VERTRAG AN KIP-T3 (bindend)
- Stop-Bedingungen sind DATEN, nicht Code: Liste von Stop-Token-IDs +
  max_tokens + optionales Abbruch-Flag (Userland setzt es, Decode-Schleife
  prüft es pro Token). T3 reserviert: EOS-Token global + pro-Rolle-Ende-Token
  (`<|ende|>`-Äquivalent); die Render-API liefert zu jedem Template die
  zugehörige Stop-Liste mit (Template und Stop-Regeln sind EIN Artefakt,
  nie getrennt versioniert).
- Streaming-Decode liefert Token-IDs; Detokenisierung ist Userland (UTF-8-
  Grenzen: decode() muss partielle Multibyte-Sequenzen puffern können —
  T2-API braucht dafür `decode_stream`-Variante oder dokumentiertes Puffern).

## 4. Serving-Bausteine (Umsetzungsphase, Reihenfolge)
1. Token-Streaming + Abbruch (nur B2+T3-Verträge nötig).
2. Sessions über Cache-Zustand-Handles (2.1).
3. Prefix-Cache (Systemprompt einmal prefillen, Zustand klonen).
4. Continuous Batching (erfordert Batch-Decode-Pfad — separate EVAL, nicht
   versprechen bevor G4-Zahlen existieren).
5. Speculative Decoding produktiv (ki_mtp-Basis; erst nach 1–3).
6. API: Moo-nativ (Dict rein/raus) zuerst; OpenAI-kompatible HTTP-Schicht
   als .moo-Programm auf moo_web.c — optional, klar getrennt.
- Quantisierte Inferenz: außerhalb KIP (M6-Plan, fp8/int-Pfad).

## 5. Exit-Gate
GPT-Review: GO (49573fb4, Ownership-Pflicht eingearbeitet). Vertrags-Abnahme
durch B2- und T3-Implementierer (Kommentar-Thought mit Task-Referenz) vor
B2/T3-Start.
