# Followup-Tickets

Nicht-dringende Aufgaben, die im Rahmen der Konsolidierungs-Welle (Prio 1–3,
2026-04-14) bewusst **aus Scope gehalten** wurden, weil sie Neu-Features sind.
Kein Deployment-Blocker.

## P2c-followup: Vision-API-Wrapper für Welt-Engine

**Herkunft**: Prio2c (Welt-API entdupen), thought `b4e4f5d7` (Vision 2026-04-13).

**Fehlend als öffentliche API** (weder als `stdlib/welt.moo`-Wrapper noch als
`__welt_*`-Builtin noch als `moo_world_*`-Runtime-Funktion vorhanden):

- `welt_wolken(w, aktiv)` — Wolken-Layer an/aus
- `welt_wasser(w, hoehe)` — Meeresspiegel-Variante mit Wasser-Shader
- `welt_licht(w, stil)` — Preset-Licht (`"abendsonne"`, `"mittag"`, `"nacht"`)
- `welt_starten(w)` — komplette Game-Loop (WASD + Maus + Schleife)

**Grund für Vertagung**: Benötigt **neue C-Runtime-Implementierungen** in
`compiler/runtime/moo_world.c` inklusive Shader-/Asset-Arbeit. Das ist
Feature-Entwicklung, keine Konsolidierung — widerspricht der User-Auflage
"Neue Features erst nach Konsolidierung".

**Voraussetzungen zur Umsetzung**:
1. Shader-Erweiterung für Wolken (Volumetric oder Skybox-Layer).
2. Water-Shader (Wellen, Reflexion) im aktiven 3D-Backend.
3. Preset-Tabelle für `welt_licht`-Stile (Sonne-Position + Farbe + Nebel).
4. Generischer Game-Loop-Baustein in C (`moo_world_run`) oder moo-Ebene.

**Priorität**: niedrig — nur beginnen, nachdem P2b/P3b/P3c gelandet sind und
die Runtime sich stabilisiert hat.

## E1-followup: Sprite-/Chunk-Slot-Auto-Free

**Herkunft**: Prio-E1 Handle-Leak-Audit (`/tmp/moo-audit/k4_E1_handle_leak.md`,
commit 77167ae deckt nur den `smart_close`-Quick-Win ab).

**Problem**: `moo_sprite.c` und `moo_3d.c` (Chunks) verwalten Ressourcen in
statischen Slot-Arrays (`g_sprites[256]`, chunk-Slots im jeweiligen Backend).
Die "IDs" sind **numerische Werte** (Tag `MOO_NUMBER`) — daher gibt es keinen
Dispatch-Pfad für `smart_close` und keinen Refcount-Fallback. Wenn ein User
`sprite_freigeben` oder `chunk_lösche` vergisst, bleibt der Slot bis
Prozessende belegt; beim 257. Sprite wirft die Runtime "Maximale Sprite-Anzahl
erreicht".

**Optionen (beide aufwändiger als Quick-Win)**:

1. **atexit-Hook für clean Shutdown**: `atexit(moo_sprite_cleanup_all)` +
   analog für Chunks. **Risiko**: SDL_Quit läuft selbst als atexit-Handler;
   Reihenfolge zwischen atexits ist implementation-defined. Eine
   `SDL_DestroyTexture`-Runde nach bereits freigegebenem `SDL_Renderer`
   crasht. Braucht expliziten Guard (Window-Ptr NULL? Renderer-State
   prüfen?) und Tests mit Valgrind.

2. **Eigener `MOO_SPRITE` / `MOO_CHUNK`-Tag** mit Heap-Objekt + Refcount, wie
   bei Sockets/DB: Release bei RC=0 ruft Free. **Risiko**: breaking change
   für User-Code, der Sprite-IDs arithmetisch nutzt oder serialisiert.
   Braucht Deprecation-Pfad oder Version-Bump.

**Empfehlung**: Zunächst Dokumentation ausreichend (`docs/referenz/sprites.md`
bzw. `grafik-3d.md` Abschnitt "Lebenszyklus" mit Hinweis auf `*_freigeben` /
`*_lösche` als Pflicht). Runtime-Umbau erst wenn echter Bedarf entsteht
(z.B. Game mit dynamischem Asset-Streaming, das die 256 Sprite-Slots reizt).

**Priorität**: niedrig — User-Pfade mit expliziter Freigabe funktionieren;
Slot-Array-Grenze ist erst bei > 256 Sprites eine harte Wand.

## ~~P3c-followup: DB Statement-Objekt (Variante A)~~ **ERLEDIGT**

Variante A wurde im Nachgang zur Konsolidierungs-Welle umgesetzt:
`db_vorbereite` + `.binde/.ausfuehren/.abfrage/.schritt/.zuruecksetzen/.schliessen`,
positional + `:name`-Params, neuer Tag `MOO_DB_STMT`, `moo_smart_close`-Eintrag.
Verifiziert: Named-Params, Bulk-Insert 1000 Rows + Transaktion, Rollback.

---

### Historie

**Herkunft**: Prio3c (DB-Prepared-Statements), Inventar `/tmp/moo-verify/db_prepared_inventar.md`.

**In P3c implementiert (Variante B)**: `db_ausführen_mit_params` / `db_abfrage_mit_params`
binden Parameter inline pro Aufruf. Prepared Statement lebt nur für einen Call.

**Nachgetragen (Variante A)**: Echtes Statement-Objekt mit eigenem MooValue-Tag
`MOO_DB_STMT`:

- `db_vorbereite(db, sql) → stmt`
- `stmt.binde(idx, wert) → stmt` (chainable)
- `stmt.schritt() → row | nichts` (lazy Iteration)
- `stmt.ausführen() → zahl`
- `stmt.abfrage() → liste<dict>`
- `stmt.schliessen()` (+ smart_close-Eintrag)

**Grund für Vertagung**: Neuer Runtime-Typ → `moo_runtime.h`-Aenderung, RC-
Disziplin, `moo_smart_close`-Eintrag, mehr Testflaeche. Variante B deckt 95 %
aller SQL-Injection-Faelle ohne Runtime-Aufwand. Statement-Objekt lohnt sich
erst wenn echte prepared-statement-Wiederverwendung oder lazy Iteration fuer
grosse Resultsets gebraucht wird.
