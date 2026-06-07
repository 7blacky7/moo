/**
 * moo_voxel.c - Voxel-Welt-Runtime fuer moo (Plan 005, Phasen 1a-1d).
 *
 * Datenlayout (Phase 1c, RT3): Palette + bitgepacktes Index-Array.
 *   - Jeder belegte Chunk haelt eine Palette der distinkten Block-IDs + ein
 *     bitgepacktes Palette-Index-Array (1/2/4/8/16 Bit/Voxel, lazy upgrade).
 *     Loest das naive uint16_t blocks[CHUNK_DIM^3] (64 KB) ab; typische
 *     Terrain-Chunks sparen >85% RAM (siehe test_voxel_palette_asan.c).
 *   - Leere Chunks = NULL (keine Voxel-Datenbytes). Mesher in 1b (RT2),
 *     Raycast/AABB in 1d (RT4). ALLE Block-Zugriffe laufen ueber die
 *     chunk_block_get/set-Schicht; hoehere Pfade kennen das Bitpacking nicht.
 *
 * Design-Invarianten (Plan 005):
 *   - Voxel-Daten leben NIE als moo-Listen. VoxelWorld ist ein opaker C-Heap-Typ
 *     (Tag MOO_VOXELWORLD); die moo-API tauscht nur Handles + Skalare aus.
 *   - Refcount ist int32_t und das ERSTE Feld jedes Heap-Structs (Konvention
 *     aus moo_memory.c). is_heap_type()/moo_release() wurden um MOO_VOXELWORLD
 *     erweitert und dispatchen hierher ueber moo_voxel_free().
 *   - Koordinaten: moo Numbers -> floor()-Cast auf int32_t. Chunk-Lookup per
 *     floor-div/floor-mod, NICHT C-/ und % (die runden Richtung 0 und wuerden
 *     negative Koordinaten falsch abbilden -> Pflichttest test_voxel_negative_coords).
 *   - CPU-Daten strikt getrennt vom GPU-Render-Cache (kommt in 1b). Diese Datei
 *     enthaelt in Phase 1a ausschliesslich CPU-State, keine GL/SDL/Vulkan-Symbole.
 *
 * API-Contract (Plan 005 P0.5):
 *   moo_voxel_welt_neu(seed)               -> MOO_VOXELWORLD-Handle
 *   moo_voxel_setzen(w,x,y,z,id)           -> Bool (Erfolg); invalider Handle
 *                                              oder out-of-range Block-ID = moo_throw
 *   moo_voxel_holen(w,x,y,z)               -> Number Block-ID; nie allokierter
 *                                              Chunk = 0 (Luft); invalider Handle = moo_throw
 *   moo_voxel_chunk_entladen(w,x,y,z)      -> Bool (true = Chunk war da & entladen)
 *   moo_voxel_ram_statistik(w)             -> Dict {chunks, bytes_blocks,
 *                                              bytes_palette, bytes_mesh,
 *                                              bytes_total, empty_chunks}
 *
 * Block-Registry (C-intern, Phase 1):
 *   0=luft, 1=gras, 2=erde, 3=stein, 4=sand, 5=wasser.
 *   Wasser wird in Phase 1 OPAK behandelt (keine Transparenz, Plan-Entscheidung).
 *
 * Phase 3 (Plan-005 V3.1/V3.2, Agent p005-perf1) — Async-Meshing + Greedy/AO:
 *   - C-interne pthread Job-Queue (KEINE moo-Channels, Risiko 7). Worker bauen
 *     pro dirty Chunk ein CPU-Vertex-Array (MooVoxelMeshBuf) GANZ ohne GPU- und
 *     ohne moo-Heap-Aufrufe (der moo-Heap ist nicht thread-safe). Der GPU-Upload
 *     (moo_3d_chunk_begin/triangle/end) passiert AUSSCHLIESSLICH im Main-Thread
 *     in moo_voxel_aktualisieren, der die fertigen Buffer abholt (Risiko 8).
 *   - THREADING-MODELL-ENTSCHEIDUNG: posix-only (pthread). Der gesamte Pool ist
 *     in #ifndef _WIN32 gekapselt; auf _WIN32 faellt moo_voxel_aktualisieren auf
 *     SYNCHRONES Remeshing zurueck (identisches Ergebnis, nur seriell) — kein
 *     doppelter CONDITION_VARIABLE-Pfad fuer einen PoC. moo_voxel.c wird ohnehin
 *     nur im 3D-Featureblock gebaut (build.rs), die Zielplattform der GL/Vulkan-
 *     Backends ist Linux. pthread ist bereits gelinkt (moo_thread.c stets gebaut).
 *   - RACE-MODELL: Gameplay laeuft single-threaded (moo ruft setzen + aktualisieren
 *     seriell). aktualisieren blockiert bis ALLE Worker fertig sind, bevor es
 *     zurueckkehrt — zwischen Enqueue und Join wird die Welt nie veraendert, also
 *     lesen Worker eine stabile Chunk-Hashmap. Der Mutex schuetzt nur die
 *     Job-/Result-Queues. Verifiziert mit ThreadSanitizer + ASan-Stress
 *     (test_voxel_jobqueue_asan.c, TSan praeziser als helgrind fuer pthread).
 *   - GREEDY MESHING + VERTEX-AO: greedy_mesh_axis() merged koplanare Faces
 *     gleicher Block-ID UND gleichem AO-Wert pro 2D-Slice; Vertex-AO (0..3) aus
 *     den 3 diagonalen Nachbarn je Ecke. AO an Chunk-Grenzen liest diagonale
 *     Nachbarn ueber moo_voxel_world_block (Daten sind da -> AO ist KORREKT).
 *     BOUNDARY-AO-DIRTY (P006-R4, GELOEST via Dirty-Flag-Splitting): der Chunk
 *     traegt ZWEI Flags. `dirty` = Face/Inhalt veraltet (Plan-1b-Contract: die 6
 *     FACE-Nachbarn werden in moo_voxel_setzen markiert, von den QA-Harnesses
 *     fest geprueft — UNVERAENDERT). `dirty_ao` = nur die an der Grenze gelesene
 *     Vertex-AO ist stale, weil ein DIAGONALER (Kanten-/Eck-)Nachbar geaendert
 *     wurde. moo_voxel_setzen markiert ADDITIV genau die diagonalen Nachbarn
 *     (>=2 Nicht-Null-Offset-Komponenten), die das Randvoxel via ihrer
 *     (DIM+2)^3-Pad-Schale ueberhaupt samplen: Kanten-Voxel -> +1 Diagonale
 *     (bis 18 gesamt), Eck-Voxel -> +3 Kanten +1 Eck-Nachbar (bis 26 gesamt).
 *     Innen-/Flaechen-Edits markieren KEINE Diagonalen (kein Overhead). In
 *     moo_voxel_aktualisieren loesen `dirty` UND `dirty_ao` denselben vollen
 *     Greedy+AO-Remesh aus (der Mesher liest die Diagonalen ohnehin via
 *     pad_fill); beim Upload werden beide Flags geloescht. Damit ist die AO an
 *     Chunk-Grenzen korrekt, OHNE pauschal 26 Nachbarn zu remeshen (Plan-006-
 *     Risiko 5: Remesh-Kaskaden vermieden, Overhead gemessen in P006-R4-Thought).
 *
 * Phase R3 (Plan-006 P006-R3, Agent p006-r3) — Mutation/Downgrade-Optimierung:
 *   - Der Einzel-Write-Pfad (voxel_setzen) wertet Sections nur AUF (EMPTY->
 *     PALETTE, SOLID->PALETTE); er scannt bewusst NICHT die 512 Voxel pro
 *     Setzung (zu teuer, Plan-006-Risiko 4). Nach Abbau/Mining bleiben deshalb
 *     PALETTE-Sections zurueck, die wieder uniform/leer sind oder ungenutzte
 *     Palette-IDs tragen. moo_voxel_section_downgrade / _chunk_optimize holen
 *     diesen RAM LAZY zurueck: PALETTE->SOLID/EMPTY wo wieder uniform, sonst
 *     Palette-Kompaktierung (ungenutzte IDs raus, Bitbreite runter). Wird ein
 *     Chunk komplett leer, kollabiert sein Section-Array auf NULL (0 Datenbytes).
 *   - WORLD-LOCK-CONTRACT / KERN-INVARIANTE K5 (Review-Auflage, NICHT
 *     VERHANDELBAR): Eine Layout-MUTATION (Downgrade/Optimize, also free/realloc
 *     von Section-palette/indices oder NULL-Kollaps) darf NIEMALS laufen, waehrend
 *     ein Mesh-Worker denselben Chunk ueber moo_voxel_chunk_block_get LIEST. Die
 *     Worker des Threadpools lesen die Chunk-Hashmap zwischen pool-broadcast und
 *     pool-join (s. RACE-MODELL oben). Downgrade ist deshalb strikt
 *     MAIN-THREAD-ONLY und nur an zwei definierten Punkten erlaubt, die beide
 *     beweisbar AUSSERHALB eines aktiven Worker-Fensters liegen:
 *       (1) zu Beginn von moo_voxel_aktualisieren, VOR der Job-Submission
 *           (der Pool des vorherigen Frames ist dort bereits vollstaendig
 *           gejoint; der Pool dieses Frames wird erst NACH dem Downgrade
 *           geweckt) — strukturell abgesichert durch die Code-Reihenfolge.
 *       (2) im expliziten Builtin moo_voxel_welt_optimieren, das im
 *           single-threaded Gameplay-Modell (setzen/aktualisieren/optimieren
 *           seriell) garantiert ohne laufende Worker aufgerufen wird.
 *     Downgrade NIE aus moo_voxel_worker, NIE zwischen broadcast und join.
 *     Verifiziert mit ThreadSanitizer (test_voxel_jobqueue_asan.c) + dediziertem
 *     Downgrade-ASan-Harness (test_voxel_downgrade_asan.c).
 */

#include "moo_runtime.h"
#include "moo_noise.h"   /* seed-parametrisierte fBm/Perlin (Plan-005 P0.3, RT0) */
#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>      /* sysconf(_SC_NPROCESSORS_ONLN) fuer Worker-Anzahl */
#endif

/* ========================================================================
 * Forward-Decls der 3D-Chunk-API (Plan-005 1b).
 *
 * Diese vier Funktionen sind NICHT in moo_runtime.h deklariert (sie leben als
 * lokale extern-Decls in moo_3d.c/moo_world.c). Wir folgen demselben Muster.
 * moo_3d_triangle / moo_3d_backend_active stehen bereits in moo_runtime.h.
 *
 * Backend-Semantik (G0-Befund, KRITISCH fuers Mesher-Design):
 *   - GL21: chunk = Display-List, chunk_draw ECHT (glCallList).
 *   - GL33: chunk = VBO/VAO, chunk_draw ECHT (VBO-Replay).
 *   - Vulkan: chunk_draw ist NO-OP; vk_swap zeichnet JEDEN Slot mit
 *     is_used && is_compiled bedingungslos. Selektives Rendern via "nur
 *     sichtbare Chunks draw'en" funktioniert auf Vulkan NICHT — Culling/
 *     Entladen muss physisch ueber chunk_delete (Slot-Entfernung) laufen.
 *   Konsequenz fuer diesen Mesher: er baut pro Voxel-Chunk GENAU EINEN
 *   Render-Chunk (begin/end). Wer einen Chunk nicht mehr sehen will, ruft
 *   moo_voxel_chunk_entladen -> EIN chunk_delete. Es gibt hier bewusst KEINE
 *   "draw nur wenn sichtbar"-Logik, weil die auf Vulkan wirkungslos waere.
 * ======================================================================== */
extern MooValue moo_3d_chunk_create(void);
extern void     moo_3d_chunk_begin(MooValue id);
extern void     moo_3d_chunk_end(void);
extern void     moo_3d_chunk_delete(MooValue id);

/* ========================================================================
 * Konstanten
 * ======================================================================== */

#define MOO_VOXEL_CHUNK_DIM   32
#define MOO_VOXEL_CHUNK_VOL   (MOO_VOXEL_CHUNK_DIM * MOO_VOXEL_CHUNK_DIM * MOO_VOXEL_CHUNK_DIM)

/* Plan-006 (P006-R1): 8^3-Section-Unterteilung des 32^3-Chunks.
 *   SECTION_DIM = 8 -> 512 Voxel/Section; SECTIONS_PER_AXIS = 32/8 = 4;
 *   SECTIONS_PER_CHUNK = 4^3 = 64. */
#define MOO_VOXEL_SECTION_DIM        8
#define MOO_VOXEL_SECTION_VOL        (MOO_VOXEL_SECTION_DIM * MOO_VOXEL_SECTION_DIM * MOO_VOXEL_SECTION_DIM)
#define MOO_VOXEL_SECTIONS_PER_AXIS  (MOO_VOXEL_CHUNK_DIM / MOO_VOXEL_SECTION_DIM)
#define MOO_VOXEL_SECTIONS_PER_CHUNK (MOO_VOXEL_SECTIONS_PER_AXIS * MOO_VOXEL_SECTIONS_PER_AXIS * MOO_VOXEL_SECTIONS_PER_AXIS)

/* Section-Modi (Plan-006). EMPTY=0, damit ein memset(0)-Section-Array korrekt
 * 64 leere Sections ergibt. */
typedef enum {
    MOO_VOXEL_SECTION_EMPTY   = 0,
    MOO_VOXEL_SECTION_SOLID   = 1,
    MOO_VOXEL_SECTION_PALETTE = 2
} MooVoxelSectionMode;

/* Hoechste gueltige Block-ID in Phase 1 (0=luft .. 5=wasser).
 * voxel_setzen wirft bei id < 0 oder id > MOO_VOXEL_MAX_BLOCK_ID (kein stilles
 * Clampen, kein Silent-Korrupt -> Plan-Regel KEINE HACKS). */
#define MOO_VOXEL_MAX_BLOCK_ID 5

/* Anfangskapazitaet der Chunk-Hashmap (Power-of-two fuer &-Maskierung). */
#define MOO_VOXEL_INITIAL_CAP  16

/* ------------------------------------------------------------------------
 * Worldgen-Parameter (Plan-005 RT5, minimaler Heightmap-Terrain).
 *
 * Vertikale Achse ist die Z-Achse (3. Voxel-Koordinate). Die Heightmap ist
 * eine Funktion der beiden horizontalen Welt-Koordinaten (wx,wy) und damit
 * seed-deterministisch (moo_noise_fbm, KEIN globaler State). Schichten von
 * oben nach unten: gras (Oberflaeche ueber Wasserlinie) bzw. sand (Oberflaeche
 * auf/unter Wasserlinie), darunter wenige Lagen erde, darunter stein. Unter
 * dem Meeresspiegel und oberhalb der Terrain-Oberflaeche wird wasser (opak)
 * aufgefuellt.
 * ------------------------------------------------------------------------ */
#define MOO_VOXEL_GEN_SEA_LEVEL   16   /* Meeresspiegel (Welt-Z) */
#define MOO_VOXEL_GEN_BASE_HEIGHT 18   /* mittlere Terrainhoehe (Welt-Z) */
#define MOO_VOXEL_GEN_AMPLITUDE   14   /* +/- Auslenkung der Heightmap */
#define MOO_VOXEL_GEN_DIRT_DEPTH  4    /* Lagen Erde unter der Oberflaeche */
#define MOO_VOXEL_GEN_OCTAVES     4    /* fBm-Oktaven */
#define MOO_VOXEL_GEN_FREQ        0.035f /* horizontale Grundfrequenz */

/* ========================================================================
 * Datenstrukturen
 * ======================================================================== */

/* Ein Chunk: naiver, voller uint16-Block-Array. NULL-blocks = noch nie
 * beschrieben (komplett Luft).
 *
 * Render-ID-Mapping (Plan-005 1b, Risiko 5): die GPU-Render-Chunk-ID ist NICHT
 * identisch mit der Voxel-Chunk-Koordinate. Sie wird pro Chunk-Slot in
 * render_id gefuehrt (eigene Mapping-Tabelle, hier co-lokal im Slot). -1 =
 * kein GPU-Cache angelegt (z.B. nie gemesht oder kein Backend aktiv). Die
 * Render-ID wird GENAU EINMAL freigegeben: in moo_voxel_chunk_entladen bzw.
 * moo_voxel_free, jeweils nur bei aktivem Backend (sonst safe no-op). */
/* Phase-1c-Layout: Palette (distinkte Block-IDs) + bitpacktes Index-Array.
 *
 * Statt eines vollen uint16_t blocks[VOL] (64 KB) speichert jeder Chunk nur:
 *   - palette[]: die im Chunk tatsaechlich vorkommenden Block-IDs. palette[0]
 *     ist IMMER reserviert fuer Luft (0). palette_count >= 1 sobald allokiert.
 *   - indices[]: pro Voxel ein Palette-INDEX (nicht die Block-ID), bitgepackt
 *     mit bits Bit pro Eintrag in 32-Bit-Woerter. bits in {1,2,4,8,16}.
 * Hybrid-Strategie (Plan-005 1c): die Bitbreite richtet sich nach der Anzahl
 * distinkter IDs; voxel_setzen upgraded lazy (1->2->4->8->16), sobald eine neue
 * ID die aktuelle Breite sprengt. Worldgen kann eine Breite direkt vorwaehlen
 * (chunk_set_bits), spart das schrittweise Hochpacken.
 *
 * LEER-Semantik (NULL-Chunk, Plan-005 1c "leere Chunks=NULL"): ein nie mit
 * Festblock beschriebener Chunk haelt indices==NULL && palette==NULL && bits==0
 * und belegt KEINE Voxel-Datenbytes. Alle Lese-Pfade behandeln das als Luft.
 *
 * Render-ID-Mapping (1b) unveraendert: render_id, dirty wie zuvor. */
/* ------------------------------------------------------------------------
 * Plan-006-Layout (P006-R1): 8^3-Sections statt chunk-weiter Palette.
 *
 * Ein 32^3-Chunk wird in 4x4x4 = 64 Sections a 8^3 = 512 Voxel geteilt. Jede
 * Section traegt einen eigenen Modus:
 *   EMPTY   : komplett Luft. KEINE Palette-/Index-Bytes. Lesen -> 0.
 *   SOLID   : alle 512 Voxel = solid_id (einheitlich != 0). KEINE Index-Bytes.
 *   PALETTE : eigene Mini-Palette ([0]=Luft) + bitgepacktes Index-Array
 *             (1/2/4/8/16 Bit, lazy upgrade) ueber die 512 Voxel der Section.
 *
 * Upgrade-Regeln (Plan-006-Memory, exakt):
 *   EMPTY + set(id!=0)        -> PALETTE {0,id}, NUR dieses eine Voxel gesetzt
 *                                (NICHT SOLID — eine einzelne Setzung macht die
 *                                 Section nicht uniform).
 *   SOLID(s) + set(neu != s)  -> PALETTE {s, neu}: Indices initial alle s, dann
 *                                das eine Voxel auf neu.
 *   Downgrade (PALETTE->SOLID/EMPTY) ist NICHT Teil von R1 (kommt in R3,
 *   main-thread-only). Eine PALETTE-Section bleibt PALETTE.
 *
 * Warum feiner als die alte chunk-weite Palette: Surface-Mischung zwingt die
 * alte Variante chunk-weit auf 4-bit (~75%-Limit). Mit Sections sind die
 * meisten 8^3-Bloecke reiner Stein/Luft/Wasser (SOLID/EMPTY, ~0 Datenbytes);
 * nur die wenigen Surface-Sections brauchen eine Palette -> deutlich >80%.
 *
 * Header-Ehrlichkeit (Review K1): sections ist NULL solange der Chunk reine
 * Luft ist (komplett leerer Chunk = 0 Datenbytes, Plan-005-Verhalten bleibt).
 * Erst der erste Festblock allokiert das 64-Eintrag-Section-Array
 * (sizeof(MooVoxelSection)*64). Diese Header-Bytes fliessen ehrlich in
 * ram_statistik.bytes_total (Key bytes_sections, additiv, Review K2).
 *
 * ZUGRIFFSSCHICHT bleibt identisch: alle hoeheren Pfade (holen/setzen/Mesher/
 * Raycast/AABB/Worldgen) gehen weiter ueber moo_voxel_chunk_block_get/set mit
 * UNVERAENDERTER Signatur (ch, lidx in [0, CHUNK_VOL)). Die Section-Zerlegung
 * ist ein reines Implementierungsdetail dieser Schicht. */
typedef struct {
    uint16_t* palette;       /* PALETTE: distinkte IDs ([0]=Luft); sonst NULL */
    uint32_t* indices;       /* PALETTE: bitgepackte Section-Indizes; sonst NULL */
    uint16_t  palette_count; /* PALETTE: genutzte Eintraege (>=1) */
    uint16_t  palette_cap;   /* PALETTE: allokierte Eintraege */
    uint16_t  solid_id;      /* SOLID: einheitliche Block-ID */
    uint8_t   mode;          /* MooVoxelSectionMode: EMPTY/SOLID/PALETTE */
    uint8_t   bits;          /* PALETTE: Bit pro Index (1/2/4/8/16); sonst 0 */
} MooVoxelSection;

typedef struct {
    int32_t          cx, cy, cz;   /* Chunk-Koordinaten (signed, negative first-class) */
    MooVoxelSection* sections;     /* 64 Sections (4x4x4), NULL = komplett leerer Chunk */
    bool      occupied;     /* Hashmap-Slot belegt? */
    int32_t   render_id;    /* GPU-Render-Chunk-ID, -1 = kein Cache (CPU/GPU getrennt) */
    bool      dirty;        /* true = Geometrie/Faces veraltet, remesh noetig (Face-Dirty) */
    bool      dirty_ao;     /* P006-R4: true = NUR Boundary-AO dieses Chunks ist stale,
                             * weil ein DIAGONALER (Kanten-/Eck-)Nachbar geaendert wurde.
                             * Inhalt/Faces unveraendert; ein voller Greedy+AO-Remesh
                             * (liest die Diagonalen via pad_fill) stellt die AO wieder
                             * her. Wird wie dirty behandelt: aktualisieren remesht den
                             * Chunk und loescht BEIDE Flags. Additiv zum 6-Face-Dirty-
                             * Contract (Plan-005 1b) — Face-Dirty-Semantik bleibt. */
} MooVoxelChunk;

/* VoxelWorld: opaker Heap-Typ. refcount MUSS erstes Feld sein. */
typedef struct {
    int32_t        refcount;      /* MUSS erstes Feld sein (moo_memory.c-Konvention) */
    uint32_t       seed;          /* Worldgen-Seed (Phase 1a nur gespeichert) */
    MooVoxelChunk* chunks;        /* Open-Addressing-Hashmap (linear probing) */
    int32_t        chunk_cap;     /* Kapazitaet (Power-of-two) */
    int32_t        chunk_count;   /* Anzahl belegter Slots */
    void*          jobs;          /* MooVoxelJobPool* (lazy, Phase 3), NULL = nie genutzt */
} MooVoxelWorld;

/* ========================================================================
 * Floor-Division / Floor-Modulo (PFLICHT fuer negative Koordinaten)
 *
 * C-/ und % runden Richtung Null: (-1)/32 == 0, (-1)%32 == -1. Falsch fuer
 * Voxel-Gitter. Wir brauchen mathematisches Floor:
 *   floordiv(-1, 32)  == -1
 *   floormod(-1, 32)  == 31
 * d MUSS > 0 sein (CHUNK_DIM).
 * ======================================================================== */

static inline int32_t moo_voxel_floordiv(int32_t a, int32_t d) {
    int32_t q = a / d;
    int32_t r = a % d;
    /* Wenn Rest != 0 und Vorzeichen von Rest != Vorzeichen von d -> abrunden. */
    if (r != 0 && ((r < 0) != (d < 0))) {
        q--;
    }
    return q;
}

static inline int32_t moo_voxel_floormod(int32_t a, int32_t d) {
    int32_t r = a % d;
    if (r != 0 && ((r < 0) != (d < 0))) {
        r += d;
    }
    return r;
}

/* ========================================================================
 * Hash + Hashmap (Open Addressing, lineares Probing)
 * ======================================================================== */

/* Robuster Hash aus 3 signed ints. Wir packen NICHT in zu kleine Bitfelder
 * (Plan-Regel), sondern mischen alle 32 Bit jeder Koordinate. */
static inline uint32_t moo_voxel_hash3(int32_t x, int32_t y, int32_t z) {
    uint32_t h = 2166136261u; /* FNV-1a Offset-Basis */
    uint32_t ux = (uint32_t)x;
    uint32_t uy = (uint32_t)y;
    uint32_t uz = (uint32_t)z;
    h = (h ^ ux) * 16777619u;
    h = (h ^ uy) * 16777619u;
    h = (h ^ uz) * 16777619u;
    /* Finaler Avalanche-Schritt (xorshift), damit benachbarte Chunks streuen. */
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

/* Findet den Slot-Index fuer (cx,cy,cz). Gibt bei Treffer den belegten Slot
 * zurueck, sonst den ersten freien Slot zum Einfuegen. Voraussetzung:
 * chunk_count < chunk_cap (es gibt immer mindestens einen freien Slot). */
static int32_t moo_voxel_find_slot(MooVoxelWorld* w, int32_t cx, int32_t cy, int32_t cz) {
    uint32_t mask = (uint32_t)w->chunk_cap - 1u;
    uint32_t idx = moo_voxel_hash3(cx, cy, cz) & mask;
    while (w->chunks[idx].occupied) {
        if (w->chunks[idx].cx == cx &&
            w->chunks[idx].cy == cy &&
            w->chunks[idx].cz == cz) {
            return (int32_t)idx; /* Treffer */
        }
        idx = (idx + 1u) & mask;
    }
    return (int32_t)idx; /* Erster freier Slot */
}

/* Verdoppelt die Hashmap-Kapazitaet und rehasht alle belegten Chunks.
 * Gibt false bei OOM zurueck (Aufrufer wirft dann). */
static bool moo_voxel_grow(MooVoxelWorld* w) {
    int32_t old_cap = w->chunk_cap;
    int32_t new_cap = old_cap * 2;
    MooVoxelChunk* old = w->chunks;

    size_t fresh_bytes = (size_t)new_cap * sizeof(MooVoxelChunk);
    MooVoxelChunk* fresh = (MooVoxelChunk*)moo_alloc(fresh_bytes);
    if (!fresh) return false;
    /* moo_alloc nutzt malloc und nullt NICHT -> Slots explizit leeren
     * (occupied = false). */
    memset(fresh, 0, fresh_bytes);

    w->chunks = fresh;
    w->chunk_cap = new_cap;

    for (int32_t i = 0; i < old_cap; i++) {
        if (old[i].occupied) {
            int32_t slot = moo_voxel_find_slot(w, old[i].cx, old[i].cy, old[i].cz);
            w->chunks[slot] = old[i];
        }
    }
    moo_free(old);
    return true;
}

/* ========================================================================
 * Helfer: Handle-Validierung + Koordinaten-Cast
 * ======================================================================== */

static MooVoxelWorld* moo_voxel_check(MooValue w, const char* fn_name) {
    if (w.tag != MOO_VOXELWORLD) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Voxel-Fehler in %s: Erster Parameter ist keine VoxelWorld", fn_name);
        moo_throw(moo_error(msg));
        return NULL;
    }
    MooVoxelWorld* vw = (MooVoxelWorld*)moo_val_as_ptr(w);
    if (!vw) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Voxel-Fehler in %s: VoxelWorld-Handle ist NULL", fn_name);
        moo_throw(moo_error(msg));
        return NULL;
    }
    return vw;
}

/* moo Number -> int32_t mit floor()-Cast (auch fuer negative Werte korrekt). */
static inline int32_t moo_voxel_coord(MooValue v) {
    return (int32_t)floor(moo_as_number(v));
}

/* Lineare Voxel-Indexberechnung innerhalb eines Chunks (lx,ly,lz in [0,DIM)). */
static inline int32_t moo_voxel_local_index(int32_t lx, int32_t ly, int32_t lz) {
    return (lz * MOO_VOXEL_CHUNK_DIM + ly) * MOO_VOXEL_CHUNK_DIM + lx;
}

/* ========================================================================
 * Plan-006 (P006-R1): 8^3-Section-Daten-Layout-Schicht
 *
 * ZENTRAL: alle Block-Lese-/Schreibzugriffe auf einen Chunk laufen ueber
 * moo_voxel_chunk_block_get / _set MIT UNVERAENDERTER SIGNATUR (ch, lidx in
 * [0, CHUNK_VOL)). Hoehere Pfade (holen, setzen, Mesher, world_block, Raycast,
 * AABB) kennen weder Sections noch Bitpacking — sie sehen nur Block-IDs. Die
 * Zerlegung des chunk-lokalen Index in (Section, Section-lokaler Index) und die
 * EMPTY/SOLID/PALETTE-Logik sind reines Implementierungsdetail dieser Schicht.
 *
 * Bitbreiten-Tiers (pro Section): 1 Bit (2 IDs), 2 (4), 4 (16), 8 (256),
 * 16 (65536). 32 % bits == 0 fuer alle Tiers -> kein Index kreuzt eine
 * 32-Bit-Wortgrenze (einfaches Pack/Unpack). Eine Section hat SECTION_VOL=512
 * Voxel, also passen die Index-Worte glatt.
 * ======================================================================== */

/* Anzahl 32-Bit-Woerter fuer SECTION_VOL Eintraege a bits Bit. */
static inline size_t moo_voxel_index_words(uint8_t bits) {
    size_t per_word = 32u / bits;                 /* Eintraege pro Wort */
    return ((size_t)MOO_VOXEL_SECTION_VOL + per_word - 1) / per_word;
}

/* Kleinste Bitbreite, die >= n distinkte Palette-Eintraege fasst. */
static inline uint8_t moo_voxel_bits_for(int32_t n) {
    if (n <= 2)   return 1;
    if (n <= 4)   return 2;
    if (n <= 16)  return 4;
    if (n <= 256) return 8;
    return 16;
}

/* Liest den Palette-Index an Section-lokalem Index sidx aus dem bitgepackten
 * Array (sidx in [0, SECTION_VOL)). */
static inline uint32_t moo_voxel_idx_get(const uint32_t* indices, uint8_t bits, int32_t sidx) {
    uint32_t per_word = 32u / bits;
    uint32_t mask = (bits == 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    uint32_t word = indices[(uint32_t)sidx / per_word];
    uint32_t shift = ((uint32_t)sidx % per_word) * bits;
    return (word >> shift) & mask;
}

/* Schreibt den Palette-Index pidx an Section-lokalem Index sidx. */
static inline void moo_voxel_idx_set(uint32_t* indices, uint8_t bits, int32_t sidx, uint32_t pidx) {
    uint32_t per_word = 32u / bits;
    uint32_t mask = (bits == 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    uint32_t w = (uint32_t)sidx / per_word;
    uint32_t shift = ((uint32_t)sidx % per_word) * bits;
    indices[w] = (indices[w] & ~(mask << shift)) | ((pidx & mask) << shift);
}

/* ------------------------------------------------------------------------
 * Index-Zerlegung: chunk-lokaler lidx -> (Section-Slot, Section-lokaler Index)
 *
 * lidx = (lz*DIM + ly)*DIM + lx  (lx,ly,lz in [0,32)).
 * Section-Koordinate je Achse = local/8; Section-Slot = (sz*4 + sy)*4 + sx.
 * Section-lokaler Index = ((lz%8)*8 + (ly%8))*8 + (lx%8).
 * ------------------------------------------------------------------------ */
static inline int32_t moo_voxel_section_slot_of(int32_t lidx, int32_t* out_sidx) {
    int32_t lx = lidx % MOO_VOXEL_CHUNK_DIM;
    int32_t ly = (lidx / MOO_VOXEL_CHUNK_DIM) % MOO_VOXEL_CHUNK_DIM;
    int32_t lz = lidx / (MOO_VOXEL_CHUNK_DIM * MOO_VOXEL_CHUNK_DIM);
    int32_t sx = lx / MOO_VOXEL_SECTION_DIM;
    int32_t sy = ly / MOO_VOXEL_SECTION_DIM;
    int32_t sz = lz / MOO_VOXEL_SECTION_DIM;
    int32_t ix = lx % MOO_VOXEL_SECTION_DIM;
    int32_t iy = ly % MOO_VOXEL_SECTION_DIM;
    int32_t iz = lz % MOO_VOXEL_SECTION_DIM;
    *out_sidx = (iz * MOO_VOXEL_SECTION_DIM + iy) * MOO_VOXEL_SECTION_DIM + ix;
    return (sz * MOO_VOXEL_SECTIONS_PER_AXIS + sy) * MOO_VOXEL_SECTIONS_PER_AXIS + sx;
}

/* ------------------------------------------------------------------------
 * Section-Lese-/Schreib-Primitive
 * ------------------------------------------------------------------------ */

/* Block-ID an Section-lokalem Index sidx. */
static inline uint16_t moo_voxel_section_get(const MooVoxelSection* sec, int32_t sidx) {
    switch (sec->mode) {
        case MOO_VOXEL_SECTION_EMPTY: return 0;
        case MOO_VOXEL_SECTION_SOLID: return sec->solid_id;
        case MOO_VOXEL_SECTION_PALETTE: {
            if (!sec->indices || sec->bits == 0) return 0;        /* defensiv */
            uint32_t pidx = moo_voxel_idx_get(sec->indices, sec->bits, sidx);
            if (pidx >= sec->palette_count) return 0;             /* defensiv */
            return sec->palette[pidx];
        }
        default: return 0;
    }
}

/* Gibt alle PALETTE-Allokationen einer Section frei und setzt sie auf EMPTY. */
static void moo_voxel_section_free(MooVoxelSection* sec) {
    if (sec->palette) { moo_free(sec->palette); sec->palette = NULL; }
    if (sec->indices) { moo_free(sec->indices); sec->indices = NULL; }
    sec->palette_count = 0;
    sec->palette_cap = 0;
    sec->bits = 0;
    sec->solid_id = 0;
    sec->mode = MOO_VOXEL_SECTION_EMPTY;
}

/* Allokiert Palette + Index-Array fuer eine PALETTE-Section. palette[0]=Luft,
 * Indices initial alle 0 (=Luft). bits ist die Start-Bitbreite. false bei OOM.
 * Voraussetzung: Section ist noch nicht PALETTE (palette/indices == NULL). */
static bool moo_voxel_section_alloc_palette(MooVoxelSection* sec, uint8_t bits) {
    uint16_t pcap = (uint16_t)((1u << bits) < 4u ? 4u : (1u << bits));
    if (pcap > 256 && bits < 16) pcap = 256;
    if (bits == 16) pcap = 256;
    uint16_t* pal = (uint16_t*)moo_alloc((size_t)pcap * sizeof(uint16_t));
    if (!pal) return false;
    memset(pal, 0, (size_t)pcap * sizeof(uint16_t));
    size_t iwords = moo_voxel_index_words(bits);
    uint32_t* idx = (uint32_t*)moo_alloc(iwords * sizeof(uint32_t));
    if (!idx) { moo_free(pal); return false; }
    memset(idx, 0, iwords * sizeof(uint32_t));     /* alle Voxel -> Palette[0]=Luft */
    sec->palette = pal;
    sec->palette_cap = pcap;
    sec->palette_count = 1;                         /* nur Luft */
    sec->indices = idx;
    sec->bits = bits;
    sec->mode = MOO_VOXEL_SECTION_PALETTE;
    return true;
}

/* Packt das Section-Index-Array auf eine groessere Bitbreite um. false bei OOM. */
static bool moo_voxel_section_widen(MooVoxelSection* sec, uint8_t new_bits) {
    size_t new_words = moo_voxel_index_words(new_bits);
    uint32_t* fresh = (uint32_t*)moo_alloc(new_words * sizeof(uint32_t));
    if (!fresh) return false;
    memset(fresh, 0, new_words * sizeof(uint32_t));
    for (int32_t i = 0; i < MOO_VOXEL_SECTION_VOL; i++) {
        uint32_t pidx = moo_voxel_idx_get(sec->indices, sec->bits, i);
        moo_voxel_idx_set(fresh, new_bits, i, pidx);
    }
    moo_free(sec->indices);
    sec->indices = fresh;
    sec->bits = new_bits;
    return true;
}

/* Findet die Palette-Position von id in einer PALETTE-Section oder fuegt sie
 * hinzu (lazy bit-upgrade + Palette-Wachstum). Palette-Index oder -1 bei OOM.
 * Voraussetzung: sec->mode == PALETTE. */
static int32_t moo_voxel_section_palette_intern(MooVoxelSection* sec, uint16_t id) {
    for (uint16_t i = 0; i < sec->palette_count; i++) {
        if (sec->palette[i] == id) return (int32_t)i;
    }
    int32_t need = (int32_t)sec->palette_count + 1;
    uint8_t need_bits = moo_voxel_bits_for(need);
    if (need_bits > sec->bits) {
        if (!moo_voxel_section_widen(sec, need_bits)) return -1;
    }
    if (sec->palette_count >= sec->palette_cap) {
        uint32_t new_cap = (uint32_t)sec->palette_cap * 2u;
        if (new_cap > 65536u) new_cap = 65536u;
        uint16_t* fresh = (uint16_t*)moo_alloc((size_t)new_cap * sizeof(uint16_t));
        if (!fresh) return -1;
        memset(fresh, 0, (size_t)new_cap * sizeof(uint16_t));
        memcpy(fresh, sec->palette, (size_t)sec->palette_count * sizeof(uint16_t));
        moo_free(sec->palette);
        sec->palette = fresh;
        sec->palette_cap = (uint16_t)new_cap;
    }
    int32_t pos = (int32_t)sec->palette_count;
    sec->palette[pos] = id;
    sec->palette_count++;
    return pos;
}

/* Wandelt eine SOLID-Section in eine PALETTE-Section um, deren 512 Voxel alle
 * den bisherigen solid_id tragen (Palette {solid_id} bzw. {0, solid_id} falls
 * solid_id != 0). false bei OOM. Voraussetzung: sec->mode == SOLID. */
static bool moo_voxel_section_solid_to_palette(MooVoxelSection* sec) {
    uint16_t sid = sec->solid_id;
    /* Bitbreite: brauchen mindestens 2 Eintraege (Luft + sid) sobald sid!=0,
     * damit nachfolgende Setzungen anderer IDs Platz finden. */
    if (!moo_voxel_section_alloc_palette(sec, 1)) return false;
    /* alloc_palette setzt mode=PALETTE, palette={0}, count=1, indices alle 0. */
    if (sid == 0) return true;                      /* war faktisch leer */
    int32_t pidx = moo_voxel_section_palette_intern(sec, sid);
    if (pidx < 0) { moo_voxel_section_free(sec); return false; }
    for (int32_t i = 0; i < MOO_VOXEL_SECTION_VOL; i++) {
        moo_voxel_idx_set(sec->indices, sec->bits, i, (uint32_t)pidx);
    }
    return true;
}

/* Setzt Block-ID id an Section-lokalem Index sidx. Implementiert die exakten
 * Plan-006-Upgrade-Regeln. false bei OOM. */
static bool moo_voxel_section_set(MooVoxelSection* sec, int32_t sidx, uint16_t id) {
    switch (sec->mode) {
        case MOO_VOXEL_SECTION_EMPTY:
            if (id == 0) return true;               /* Luft in leere Section: No-op */
            /* EMPTY + set(id!=0) -> PALETTE {0,id}, nur dieses Voxel gesetzt. */
            if (!moo_voxel_section_alloc_palette(sec, 1)) return false;
            {
                int32_t pidx = moo_voxel_section_palette_intern(sec, id);
                if (pidx < 0) { moo_voxel_section_free(sec); return false; }
                moo_voxel_idx_set(sec->indices, sec->bits, sidx, (uint32_t)pidx);
            }
            return true;
        case MOO_VOXEL_SECTION_SOLID:
            if (id == sec->solid_id) return true;   /* gleiche ID: No-op */
            /* SOLID(s) + set(neu != s) -> PALETTE {s, neu}. */
            if (!moo_voxel_section_solid_to_palette(sec)) return false;
            /* faellt durch in den PALETTE-Pfad unten. */
            /* fallthrough */
        case MOO_VOXEL_SECTION_PALETTE: {
            int32_t pidx = moo_voxel_section_palette_intern(sec, id);
            if (pidx < 0) return false;
            moo_voxel_idx_set(sec->indices, sec->bits, sidx, (uint32_t)pidx);
            return true;
        }
        default:
            return false;
    }
}

/* ------------------------------------------------------------------------
 * Section-Direkt-Build (P006-R2): baut eine Section in EINEM Schritt aus einem
 * voll-evaluierten 512-Eintrag-Block-Array auf, statt 512 Einzel-Setzungen mit
 * schrittweisem EMPTY->PALETTE->widen-Upgrade. Waehlt direkt den optimalen
 * Modus + die minimale Bitbreite:
 *   - alle 0            -> EMPTY (0 Datenbytes)
 *   - alle gleich != 0  -> SOLID (0 Index-Bytes)
 *   - sonst             -> PALETTE mit minimaler Bitbreite (genau die distinkten
 *                          IDs, palette[0]=Luft falls Luft vorkommt).
 *
 * KAPSELUNG: kennt KEINE Worldgen-Semantik — nimmt nur ein fertiges Block-Array
 * entgegen. Voraussetzung: sec ist frisch EMPTY (memset(0) aus
 * chunk_ensure_sections). Liefert exakt dieselben Lese-Resultate wie 512
 * Aufrufe von moo_voxel_section_set in Reihenfolge (Determinismus-invariant).
 * false bei OOM (Section bleibt dann definiert EMPTY). */
static bool moo_voxel_section_build_from_array(MooVoxelSection* sec,
                                              const uint16_t ids[MOO_VOXEL_SECTION_VOL]) {
    /* 1) Distinkte IDs sammeln + Uniformitaet pruefen. palette[0] wird fuer Luft
     *    reserviert (Konvention der PALETTE-Sections), sonst beginnt die Palette
     *    bei der ersten festen ID. */
    uint16_t distinct[MOO_VOXEL_SECTION_VOL];
    int32_t  ndist = 0;
    bool     has_air = false;
    /* kleine lineare Suche reicht: Section-Paletten bleiben winzig (typisch <=6).
     * Bei vielen distinkten IDs (worst-case-random) bricht die Schleife nie ab,
     * bleibt aber O(VOL * ndist) — akzeptabel fuer einen einmaligen Build. */
    for (int32_t i = 0; i < MOO_VOXEL_SECTION_VOL; i++) {
        uint16_t id = ids[i];
        if (id == 0) { has_air = true; continue; }
        bool found = false;
        for (int32_t k = 0; k < ndist; k++) {
            if (distinct[k] == id) { found = true; break; }
        }
        if (!found) distinct[ndist++] = id;
    }

    /* 2) Trivialfaelle ohne Allokation. */
    if (ndist == 0) {
        /* Alles Luft -> EMPTY (sec ist bereits EMPTY). */
        return true;
    }
    if (ndist == 1 && !has_air) {
        /* Alle Voxel == eine feste ID -> SOLID, 0 Index-Bytes. */
        sec->mode = MOO_VOXEL_SECTION_SOLID;
        sec->solid_id = distinct[0];
        return true;
    }

    /* 3) PALETTE: minimale Bitbreite fuer (Luft? + distinkte IDs). palette[0]
     *    bleibt Luft (auch wenn keine Luft vorkommt — kostet nur 1 Eintrag und
     *    haelt die Konvention konsistent mit dem Einzel-Write-Pfad). */
    int32_t pal_entries = ndist + 1;   /* +1 fuer reservierte Luft an Position 0 */
    uint8_t bits = moo_voxel_bits_for(pal_entries);
    if (!moo_voxel_section_alloc_palette(sec, bits)) return false;
    /* alloc_palette: mode=PALETTE, palette={0}, palette_count=1, indices alle 0. */

    /* distinkte IDs internen (deterministische Reihenfolge = Reihenfolge des
     * ersten Vorkommens, identisch zum schrittweisen palette_intern im
     * Einzel-Write-Pfad). intern weitet die Bitbreite nie ueber bits hinaus,
     * weil wir sie oben passend gewaehlt haben. */
    int32_t pidx_of[MOO_VOXEL_SECTION_VOL]; /* distinct[k] -> Palette-Position */
    for (int32_t k = 0; k < ndist; k++) {
        int32_t p = moo_voxel_section_palette_intern(sec, distinct[k]);
        if (p < 0) { moo_voxel_section_free(sec); return false; }
        pidx_of[k] = p;
    }

    /* 4) Indices schreiben: Luft -> 0 (bereits genullt), sonst Palette-Position. */
    for (int32_t i = 0; i < MOO_VOXEL_SECTION_VOL; i++) {
        uint16_t id = ids[i];
        if (id == 0) continue;            /* Index 0 = Luft, schon gesetzt */
        int32_t pidx = 0;
        for (int32_t k = 0; k < ndist; k++) {
            if (distinct[k] == id) { pidx = pidx_of[k]; break; }
        }
        moo_voxel_idx_set(sec->indices, sec->bits, i, (uint32_t)pidx);
    }
    return true;
}


/* Forward-Decl: chunk_optimize (unten) ruft chunk_free_data fuer den NULL-
 * Kollaps eines leer gewordenen Chunks; die Definition folgt weiter unten. */
static void moo_voxel_chunk_free_data(MooVoxelChunk* ch);


/* ------------------------------------------------------------------------
 * Section-Downgrade + Palette-Kompaktierung (P006-R3, Mutation/Optimize)
 *
 * MOTIVATION: Der Einzel-Write-Pfad (moo_voxel_setzen) kann eine Section nur
 * AUFWERTEN (EMPTY->PALETTE, SOLID->PALETTE) — er weiss beim Setzen eines
 * einzelnen Voxels nicht, ob die Section dadurch wieder uniform wird, und ein
 * Scan der 512 Voxel bei JEDEM voxel_setzen waere zu teuer (Plan-006-Risiko 4).
 * Folge: nach Abbau/Mining bleiben PALETTE-Sections zurueck, deren Palette
 * ungenutzte IDs enthaelt oder die faktisch wieder komplett Luft / komplett
 * uniform sind. Der Downgrade holt diese RAM zurueck.
 *
 * Das ist die GENAUE UMKEHRUNG der Upgrade-Regeln in moo_voxel_section_set:
 *   PALETTE, alle 512 == Luft        -> EMPTY  (Palette+Index-Array frei)
 *   PALETTE, alle 512 == eine ID !=0 -> SOLID  (Palette+Index-Array frei)
 *   PALETTE, sonst                   -> bleibt PALETTE, aber kompaktiert:
 *                                       ungenutzte Palette-IDs entfernt, Indizes
 *                                       neu gepackt, minimale Bitbreite gewaehlt.
 * EMPTY/SOLID-Sections sind bereits optimal und werden uebersprungen.
 *
 * NEBENWIRKUNGSFREI fuer Lese-Resultate: liefert nach dem Downgrade fuer jedes
 * der 512 Voxel exakt dieselbe Block-ID wie davor (reine Repraesentations-
 * Umstellung). KEINE Geometrie-Aenderung -> der Aufrufer muss NICHT zwingend
 * remeshen; das Layout ist semantisch identisch.
 *
 * THREADING (Review-Auflage K5): Diese Funktion MUTIERT das Section-Layout
 * (free/realloc von palette/indices). Sie darf NIEMALS laufen, waehrend ein
 * Mesh-Worker denselben Chunk ueber moo_voxel_chunk_block_get liest. Sie wird
 * deshalb AUSSCHLIESSLICH im Main-Thread an definierten Punkten aufgerufen:
 *   (a) zu Beginn von moo_voxel_aktualisieren, VOR der Job-Submission (die
 *       Worker des VORHERIGEN Frames sind dort bereits gejoint), oder
 *   (b) ueber das explizite moo_voxel_welt_optimieren, das ebenfalls nur im
 *       single-threaded Gameplay-Kontext aufgerufen wird (kein aktiver Pool).
 * NIE aus moo_voxel_worker oder zwischen pool-broadcast und pool-join heraus.
 * ------------------------------------------------------------------------ */

/* Kompaktiert die Palette einer PALETTE-Section: entfernt Eintraege, die von
 * KEINEM der 512 Voxel referenziert werden, packt die Indizes auf die neue
 * (kleinere) Palette um und waehlt die minimale Bitbreite. palette[0] bleibt
 * IMMER fuer Luft reserviert (Konvention), auch wenn keine Luft vorkommt — das
 * haelt die Repraesentation konsistent mit dem Build-/Set-Pfad. Reine
 * Repraesentations-Umstellung, Lese-Resultate unveraendert. false bei OOM
 * (Section bleibt dann unveraendert gueltig). */
static bool moo_voxel_section_compact_palette(MooVoxelSection* sec) {
    if (sec->mode != MOO_VOXEL_SECTION_PALETTE) return true;
    if (!sec->indices || !sec->palette) return true;

    /* OBERGRENZE: eine 8^3-Section hat 512 Voxel, also koennen hoechstens 512
     * distinkte feste IDs plus die reservierte Luft-Position vorkommen ->
     * palette_count <= 513. Die Hilfsarrays sind danach dimensioniert (klein
     * genug fuer den Stack, ~1.5 KiB statt 320 KiB bei 65536). */
    uint16_t pc = sec->palette_count;
    if (pc > MOO_VOXEL_SECTION_VOL + 1) return true;   /* darf nicht auftreten; defensiv */

    /* 1) Welche Palette-Positionen werden tatsaechlich referenziert? */
    bool used[MOO_VOXEL_SECTION_VOL + 1]; /* Palette-Position -> referenziert? */
    for (uint16_t i = 0; i < pc; i++) used[i] = false;
    used[0] = true;                       /* Luft-Position bleibt reserviert */
    for (int32_t i = 0; i < MOO_VOXEL_SECTION_VOL; i++) {
        uint32_t pidx = moo_voxel_idx_get(sec->indices, sec->bits, i);
        if (pidx < pc) used[pidx] = true;
    }

    /* 2) Neue, kompakte Palette aufbauen (Reihenfolge = alte Reihenfolge der
     *    genutzten Eintraege; deterministisch). old_pos -> new_pos Mapping. */
    uint16_t remap[MOO_VOXEL_SECTION_VOL + 1];
    uint16_t new_pal[MOO_VOXEL_SECTION_VOL + 1];
    uint16_t new_count = 0;
    for (uint16_t i = 0; i < pc; i++) {
        if (!used[i]) continue;
        remap[i] = new_count;
        new_pal[new_count] = sec->palette[i];
        new_count++;
    }

    if (new_count == pc) {
        /* Keine ungenutzten Eintraege -> nichts zu kompaktieren. */
        return true;
    }

    /* 3) Minimale Bitbreite fuer die kompakte Palette. */
    uint8_t new_bits = moo_voxel_bits_for((int32_t)new_count);

    /* 4) Frisches Index-Array in der (ggf. kleineren) Bitbreite befuellen. */
    size_t new_words = moo_voxel_index_words(new_bits);
    uint32_t* fresh = (uint32_t*)moo_alloc(new_words * sizeof(uint32_t));
    if (!fresh) return false;             /* OOM: Section bleibt unveraendert */
    memset(fresh, 0, new_words * sizeof(uint32_t));
    for (int32_t i = 0; i < MOO_VOXEL_SECTION_VOL; i++) {
        uint32_t old_pidx = moo_voxel_idx_get(sec->indices, sec->bits, i);
        uint32_t np = (old_pidx < pc) ? remap[old_pidx] : 0u;
        moo_voxel_idx_set(fresh, new_bits, i, np);
    }

    /* 5) Palette in-place auf die kompakte Variante zuruecksetzen (palette_cap
     *    bleibt — wir geben den Puffer nicht frei, nur die genutzte Anzahl
     *    schrumpft; das spart eine Re-Allokation und ram_statistik zaehlt
     *    palette_cap, also wird der Cap unten ggf. verkleinert). */
    for (uint16_t i = 0; i < new_count; i++) sec->palette[i] = new_pal[i];
    sec->palette_count = new_count;

    moo_free(sec->indices);
    sec->indices = fresh;
    sec->bits = new_bits;
    return true;
}

/* Versucht, eine PALETTE-Section zu EMPTY oder SOLID herabzustufen, falls alle
 * 512 Voxel wieder Luft bzw. eine einheitliche ID tragen; sonst kompaktiert sie
 * nur die Palette. EMPTY/SOLID-Sections sind bereits optimal -> No-op. Reine
 * Repraesentations-Umstellung (Lese-Resultate unveraendert). */
static void moo_voxel_section_downgrade(MooVoxelSection* sec) {
    if (sec->mode != MOO_VOXEL_SECTION_PALETTE) return;   /* EMPTY/SOLID optimal */
    if (!sec->indices || !sec->palette) return;           /* defensiv */

    /* Erste echte Block-ID finden und pruefen, ob ALLE 512 Voxel gleich sind. */
    uint16_t first = moo_voxel_section_get(sec, 0);
    bool uniform = true;
    for (int32_t i = 1; i < MOO_VOXEL_SECTION_VOL; i++) {
        if (moo_voxel_section_get(sec, i) != first) { uniform = false; break; }
    }

    if (uniform) {
        if (first == 0) {
            /* Alle Luft -> EMPTY (gibt Palette + Index-Array frei). */
            moo_voxel_section_free(sec);                   /* setzt mode=EMPTY */
        } else {
            /* Alle == first (!=0) -> SOLID (0 Index-Bytes). */
            moo_voxel_section_free(sec);                   /* free zuerst (EMPTY) */
            sec->mode = MOO_VOXEL_SECTION_SOLID;
            sec->solid_id = first;
        }
        return;
    }

    /* Nicht uniform: nur die Palette kompaktieren (ungenutzte IDs raus,
     * Bitbreite ggf. runter). OOM ist hier unkritisch — die Section bleibt
     * dann einfach in ihrer bisherigen, gueltigen Form. */
    (void)moo_voxel_section_compact_palette(sec);
}

/* Optimiert einen ganzen Chunk: Downgrade aller PALETTE-Sections. Wird der
 * Chunk dadurch komplett leer (alle 64 Sections EMPTY), kollabiert das
 * Section-Array auf NULL (0 Datenbytes, Plan-005/006-Invariante K1 — ein leerer
 * Chunk darf keine Header-Bytes kosten). Der Hashmap-Slot bleibt belegt
 * (occupied), damit render_id/dirty erhalten bleiben und der naechste
 * aktualisieren-Lauf den ggf. vorhandenen GPU-Cache regulaer abraeumt.
 *
 * Rueckgabe: true, wenn sich am Layout etwas geaendert hat (nur fuer Diagnostik;
 * der Aufrufer braucht es nicht). Main-Thread-only (K5). */
static bool moo_voxel_chunk_optimize(MooVoxelChunk* ch) {
    if (!ch->sections) return false;                       /* schon leer */
    for (int32_t s = 0; s < MOO_VOXEL_SECTIONS_PER_CHUNK; s++) {
        moo_voxel_section_downgrade(&ch->sections[s]);
    }
    /* NULL-Kollaps, falls der Chunk jetzt komplett leer ist. */
    bool all_empty = true;
    for (int32_t s = 0; s < MOO_VOXEL_SECTIONS_PER_CHUNK; s++) {
        if (ch->sections[s].mode != MOO_VOXEL_SECTION_EMPTY) { all_empty = false; break; }
    }
    if (all_empty) {
        moo_voxel_chunk_free_data(ch);                     /* sections -> NULL */
        return true;
    }
    return true;
}


/* Lazy-Allokation des 64-Eintrag-Section-Arrays beim ersten Festblock eines
 * bislang komplett leeren Chunks. memset(0) -> 64x EMPTY-Section. false bei OOM. */
static bool moo_voxel_chunk_ensure_sections(MooVoxelChunk* ch) {
    if (ch->sections) return true;
    size_t bytes = (size_t)MOO_VOXEL_SECTIONS_PER_CHUNK * sizeof(MooVoxelSection);
    MooVoxelSection* secs = (MooVoxelSection*)moo_alloc(bytes);
    if (!secs) return false;
    memset(secs, 0, bytes);  /* mode=EMPTY(0), Pointer NULL, counts 0 */
    ch->sections = secs;
    return true;
}

/* Liefert die Block-ID an Voxel lidx (lidx in [0, CHUNK_VOL)). Komplett leerer
 * Chunk (sections==NULL) = Luft. UNVERAENDERTE SIGNATUR (Invariante). */
static inline uint16_t moo_voxel_chunk_block_get(const MooVoxelChunk* ch, int32_t lidx) {
    if (!ch->sections) return 0;                    /* leerer Chunk = Luft */
    int32_t sidx;
    int32_t slot = moo_voxel_section_slot_of(lidx, &sidx);
    return moo_voxel_section_get(&ch->sections[slot], sidx);
}

/* Setzt Block-ID id an Voxel lidx. Allokiert das Section-Array lazy beim ersten
 * Festblock; Luft in einen komplett leeren Chunk ist ein No-op (Chunk bleibt
 * NULL/0 Bytes). UNVERAENDERTE SIGNATUR (Invariante). false bei OOM. */
static bool moo_voxel_chunk_block_set(MooVoxelChunk* ch, int32_t lidx, uint16_t id) {
    if (!ch->sections) {
        if (id == 0) return true;                   /* Luft in leeren Chunk: nichts tun */
        if (!moo_voxel_chunk_ensure_sections(ch)) return false;
    }
    int32_t sidx;
    int32_t slot = moo_voxel_section_slot_of(lidx, &sidx);
    return moo_voxel_section_set(&ch->sections[slot], sidx, id);
}

/* "Hat dieser Chunk ueberhaupt Voxel-Daten?" — true = komplett leer (reine
 * Luft, 0 Datenbytes, sections==NULL). Ersetzt das fruehere "!ch->indices"-
 * Predikat aus dem chunk-weiten Palette-Layout 1:1: ein leerer Chunk hat keine
 * Geometrie zu meshen und liefert ueberall Luft. */
static inline bool moo_voxel_chunk_is_empty(const MooVoxelChunk* ch) {
    return ch->sections == NULL;
}

/* Gibt ALLE CPU-Voxel-Daten eines Chunks frei: jede PALETTE-Section-Allokation
 * plus das 64-Eintrag-Section-Array selbst. Danach ist der Chunk wieder
 * komplett leer (sections==NULL). Idempotent. GPU-Render-Cache liegt separat
 * (render_id) und wird NICHT hier angefasst. */
static void moo_voxel_chunk_free_data(MooVoxelChunk* ch) {
    if (!ch->sections) return;
    for (int32_t s = 0; s < MOO_VOXEL_SECTIONS_PER_CHUNK; s++) {
        moo_voxel_section_free(&ch->sections[s]);
    }
    moo_free(ch->sections);
    ch->sections = NULL;
}


/* Sucht den Chunk (cx,cy,cz) und gibt ihn zurueck, falls belegt; sonst NULL.
 * Reiner Lookup ohne Allokation (anders als find_slot, das den Einfuege-Slot
 * liefert). Wird vom Mesher fuers Nachbar-Culling und von mark_dirty genutzt. */
static MooVoxelChunk* moo_voxel_lookup(MooVoxelWorld* w, int32_t cx, int32_t cy, int32_t cz) {
    uint32_t mask = (uint32_t)w->chunk_cap - 1u;
    uint32_t idx = moo_voxel_hash3(cx, cy, cz) & mask;
    while (w->chunks[idx].occupied) {
        if (w->chunks[idx].cx == cx &&
            w->chunks[idx].cy == cy &&
            w->chunks[idx].cz == cz) {
            return &w->chunks[idx];
        }
        idx = (idx + 1u) & mask;
    }
    return NULL;
}

/* Markiert einen BEREITS EXISTIERENDEN Nachbar-Chunk als dirty. Nicht-allokierte
 * Nachbarn (komplett Luft) werden bewusst NICHT angelegt — sie haben keine
 * Geometrie zu remeshen. */
static void moo_voxel_mark_dirty(MooVoxelWorld* w, int32_t cx, int32_t cy, int32_t cz) {
    MooVoxelChunk* ch = moo_voxel_lookup(w, cx, cy, cz);
    if (ch) ch->dirty = true;
}

/* P006-R4: Markiert einen BEREITS EXISTIERENDEN Nachbar-Chunk fuer einen
 * AO-only-Remesh (Boundary-AO stale durch Aenderung in einem DIAGONALEN
 * Nachbarn). Setzt nur dirty_ao, NICHT dirty: der Chunk-Inhalt hat sich nicht
 * geaendert, nur seine an der Grenze gelesene Vertex-AO. Setzt dirty_ao auch
 * dann nicht, wenn der Chunk ohnehin schon (Face-)dirty ist? -> doch, harmlos:
 * aktualisieren remesht ihn so oder so genau einmal und loescht beide Flags.
 * Nicht-allokierte Nachbarn (reine Luft) werden NICHT angelegt — sie haben
 * keine Geometrie, also auch keine Boundary-AO zu reparieren. */
static void moo_voxel_mark_dirty_ao(MooVoxelWorld* w, int32_t cx, int32_t cy, int32_t cz) {
    MooVoxelChunk* ch = moo_voxel_lookup(w, cx, cy, cz);
    if (ch) ch->dirty_ao = true;
}

/* ========================================================================
 * Phase 1a/RT5: Minimaler prozeduraler Worldgen (Heightmap)
 *
 * Vollstaendig SEED-DETERMINISTISCH und ZUSTANDSLOS: jede Block-ID ist eine
 * reine Funktion von (seed, wx, wy, wz) ueber moo_noise_fbm (kein globaler
 * Noise-State, Plan-005 P0.3). Daraus folgt: gleicher Seed -> identische
 * Saeulen; benachbarte Seeds -> verschiedene Saeulen (Akzeptanz von
 * test_noise_seed_determinism).
 *
 * Achsen-Konvention: Z ist die VERTIKALE Achse (3. Voxel-Koordinate). Die
 * Heightmap haengt nur von den horizontalen Welt-Koordinaten (wx,wy) ab.
 * ======================================================================== */

/* Terrain-Oberflaechenhoehe (Welt-Z) an horizontaler Position (wx,wy).
 * Hoechster solider Block der Saeule liegt bei z == height-1. */
static int32_t moo_voxel_gen_height(uint32_t seed, int32_t wx, int32_t wy) {
    float n = moo_noise_fbm((int)seed,
                            (float)wx * MOO_VOXEL_GEN_FREQ,
                            (float)wy * MOO_VOXEL_GEN_FREQ,
                            MOO_VOXEL_GEN_OCTAVES,
                            /*freq*/ 1.0f,
                            /*amp*/  1.0f);
    /* n liegt ca. in [-1,1] -> auf Basis +/- Amplitude abbilden. */
    int32_t h = MOO_VOXEL_GEN_BASE_HEIGHT + (int32_t)floorf(n * (float)MOO_VOXEL_GEN_AMPLITUDE);
    if (h < 1) h = 1;   /* mind. ein solider Block, damit es nie reine Luft ist */
    return h;
}

/* Block-ID am Welt-Voxel (wx,wy,wz). Reine Funktion (deterministisch). */
static uint16_t moo_voxel_gen_block(uint32_t seed, int32_t wx, int32_t wy, int32_t wz) {
    int32_t h = moo_voxel_gen_height(seed, wx, wy);
    if (wz >= h) {
        /* Ueber dem Terrain: Wasser bis Meeresspiegel (opak), sonst Luft. */
        if (wz < MOO_VOXEL_GEN_SEA_LEVEL) return 5; /* wasser */
        return 0;                                   /* luft */
    }
    /* Innerhalb des Terrains. */
    if (wz == h - 1) {
        /* Oberflaeche: unter/auf Wasserlinie -> sand, sonst gras. */
        if (h - 1 < MOO_VOXEL_GEN_SEA_LEVEL) return 4; /* sand */
        return 1;                                       /* gras */
    }
    if (wz >= h - 1 - MOO_VOXEL_GEN_DIRT_DEPTH) return 2; /* erde */
    return 3;                                             /* stein */
}

/* Baut eine einzelne 8^3-Section eines Worldgen-Chunks direkt auf (P006-R2,
 * Auflage K4). Statt 512 Einzel-Setzungen mit schrittweisem Palette-Upgrade
 * wird der Section-Modus aus dem Hoehenbereich der 64 Saeulen klassifiziert:
 *
 *   - Komplett SOLID stein:  Section-Oberkante liegt unter dem flachsten
 *                            Stein-Beginn aller 64 Saeulen
 *                            (z_top <= min(h)-2-DIRT_DEPTH). 0 Datenbytes.
 *   - Komplett SOLID wasser: Section komplett ueber Terrain
 *                            (z_bot >= max(h)) UND komplett unter Meeresspiegel
 *                            (z_top < SEA_LEVEL). 0 Datenbytes.
 *   - Komplett EMPTY (Luft): Section komplett ueber Terrain (z_bot >= max(h))
 *                            UND komplett ueber/auf Meeresspiegel
 *                            (z_bot >= SEA_LEVEL). 0 Datenbytes.
 *   - Sonst (Surface-/Wasser-/Erd-Schnittbereich): 512 Voxel via gen_block
 *     evaluieren und per section_build_from_array zur optimalen Section bauen.
 *
 * DETERMINISMUS-INVARIANT: jeder produzierte Voxel-Wert ist exakt
 * moo_voxel_gen_block(seed,wx,wy,wz). Die Fast-Paths sind beweisbar aequivalent
 * zur Voxel-fuer-Voxel-Auswertung (siehe gen_block-Schicht-Logik), liefern also
 * BYTE-IDENTISCHE Bloecke wie der fruehere Einzel-Write-Pfad. false bei OOM. */
static bool moo_voxel_gen_section(MooVoxelWorld* w, MooVoxelChunk* ch,
                                  int32_t slot, int32_t base_x, int32_t base_y,
                                  int32_t base_z, int32_t sx, int32_t sy, int32_t sz) {
    int32_t sec_x0 = base_x + sx * MOO_VOXEL_SECTION_DIM;
    int32_t sec_y0 = base_y + sy * MOO_VOXEL_SECTION_DIM;
    int32_t sec_z0 = base_z + sz * MOO_VOXEL_SECTION_DIM;       /* unterste Welt-Z */
    int32_t sec_z1 = sec_z0 + MOO_VOXEL_SECTION_DIM - 1;        /* oberste Welt-Z */

    /* Hoehenbereich der 64 Saeulen dieser Section (O(64) statt O(512)). */
    int32_t hmin = INT32_MAX, hmax = INT32_MIN;
    for (int32_t iy = 0; iy < MOO_VOXEL_SECTION_DIM; iy++) {
        for (int32_t ix = 0; ix < MOO_VOXEL_SECTION_DIM; ix++) {
            int32_t h = moo_voxel_gen_height(w->seed, sec_x0 + ix, sec_y0 + iy);
            if (h < hmin) hmin = h;
            if (h > hmax) hmax = h;
        }
    }

    MooVoxelSection* sec = &ch->sections[slot];

    /* Fast-Path 1: komplett Stein. Voxel ist stein iff wz <= h-2-DIRT_DEPTH.
     * Gilt fuer alle Voxel, wenn die Oberkante schon unter dem flachsten Stein-
     * Beginn liegt: sec_z1 <= hmin-2-DIRT_DEPTH. */
    if (sec_z1 <= hmin - 2 - MOO_VOXEL_GEN_DIRT_DEPTH) {
        sec->mode = MOO_VOXEL_SECTION_SOLID;
        sec->solid_id = 3; /* stein */
        return true;
    }

    /* Komplett ueber dem Terrain (kein einziger fester Block): z_bot >= hmax.
     * Dann ist jeder Voxel wasser (wz<SEA_LEVEL) oder luft (wz>=SEA_LEVEL). */
    if (sec_z0 >= hmax) {
        if (sec_z1 < MOO_VOXEL_GEN_SEA_LEVEL) {
            /* Fast-Path 2: komplett unter Meeresspiegel -> SOLID wasser. */
            sec->mode = MOO_VOXEL_SECTION_SOLID;
            sec->solid_id = 5; /* wasser */
            return true;
        }
        if (sec_z0 >= MOO_VOXEL_GEN_SEA_LEVEL) {
            /* Fast-Path 3: komplett ueber/auf Meeresspiegel -> EMPTY (Luft).
             * sec ist bereits EMPTY (memset(0)) — nichts zu tun. */
            return true;
        }
        /* sonst: Wasser/Luft-Schnitt am Meeresspiegel -> Block-fuer-Block unten. */
    }

    /* Schnittbereich (Surface/Erde/Wasserlinie): 512 Voxel exakt via gen_block
     * evaluieren und in einem Schritt zur optimalen Section bauen. */
    uint16_t ids[MOO_VOXEL_SECTION_VOL];
    for (int32_t iz = 0; iz < MOO_VOXEL_SECTION_DIM; iz++) {
        int32_t wz = sec_z0 + iz;
        for (int32_t iy = 0; iy < MOO_VOXEL_SECTION_DIM; iy++) {
            int32_t wy = sec_y0 + iy;
            for (int32_t ix = 0; ix < MOO_VOXEL_SECTION_DIM; ix++) {
                int32_t sidx = (iz * MOO_VOXEL_SECTION_DIM + iy) * MOO_VOXEL_SECTION_DIM + ix;
                ids[sidx] = moo_voxel_gen_block(w->seed, sec_x0 + ix, wy, wz);
            }
        }
    }
    return moo_voxel_section_build_from_array(sec, ids);
}

/* Generiert genau EINEN Chunk (cx,cy,cz) aus der Heightmap und claimt seinen
 * Hashmap-Slot. occupied wird IMMER gesetzt (auch fuer reine Luft-Chunks),
 * damit der Lese-Pfad (moo_voxel_holen) einen bereits generierten Chunk nicht
 * erneut generiert.
 *
 * P006-R2 (K4): Statt 32768 Einzel-chunk_block_set baut der Worldgen jede der
 * 64 Sections direkt auf (moo_voxel_gen_section). Klassifikation ueber den
 * Hoehenbereich macht die Mehrzahl der Sections O(Saeulen) statt O(Voxel) und
 * waehlt SOLID/EMPTY/PALETTE mit minimaler Bitbreite direkt. Die Block-Werte
 * sind byte-identisch zum frueheren Einzel-Write-Pfad (jeder Wert = gen_block).
 * Gibt false bei OOM. */
static bool moo_voxel_gen_chunk(MooVoxelWorld* w, int32_t cx, int32_t cy, int32_t cz) {
    /* Last-Factor 0.75: ggf. wachsen, damit immer ein freier Slot existiert. */
    if ((w->chunk_count + 1) * 4 >= w->chunk_cap * 3) {
        if (!moo_voxel_grow(w)) return false;
    }
    int32_t slot = moo_voxel_find_slot(w, cx, cy, cz);
    MooVoxelChunk* ch = &w->chunks[slot];
    if (!ch->occupied) {
        ch->occupied = true;
        ch->cx = cx; ch->cy = cy; ch->cz = cz;
        ch->sections = NULL;   /* komplett leerer Chunk = NULL (Plan-006) */
        ch->render_id = -1;
        ch->dirty = true;
        w->chunk_count++;
    }

    int32_t base_x = cx * MOO_VOXEL_CHUNK_DIM;
    int32_t base_y = cy * MOO_VOXEL_CHUNK_DIM;
    int32_t base_z = cz * MOO_VOXEL_CHUNK_DIM;

    /* Erst pruefen, ob der gesamte Chunk reine Luft ist (komplett ueber Terrain
     * und ueber Meeresspiegel) — dann bleibt sections==NULL (0 Datenbytes,
     * Plan-006-Invariante). gen_height ist eine reine Funktion der horizontalen
     * Koordinate; min ueber die 32x32 Saeulen reicht. */
    int32_t chunk_hmax = INT32_MIN;
    for (int32_t ly = 0; ly < MOO_VOXEL_CHUNK_DIM; ly++) {
        for (int32_t lx = 0; lx < MOO_VOXEL_CHUNK_DIM; lx++) {
            int32_t h = moo_voxel_gen_height(w->seed, base_x + lx, base_y + ly);
            if (h > chunk_hmax) chunk_hmax = h;
        }
    }
    if (base_z >= chunk_hmax && base_z >= MOO_VOXEL_GEN_SEA_LEVEL) {
        /* Gesamter Chunk ueber Terrain + ueber Meeresspiegel -> reine Luft. */
        ch->dirty = true;
        return true;
    }

    /* Section-Array lazy allokieren (memset(0) -> 64x EMPTY), dann jede Section
     * direkt klassifizieren/bauen. */
    if (!moo_voxel_chunk_ensure_sections(ch)) return false;
    for (int32_t sz = 0; sz < MOO_VOXEL_SECTIONS_PER_AXIS; sz++) {
        for (int32_t sy = 0; sy < MOO_VOXEL_SECTIONS_PER_AXIS; sy++) {
            for (int32_t sx = 0; sx < MOO_VOXEL_SECTIONS_PER_AXIS; sx++) {
                int32_t slot_s = (sz * MOO_VOXEL_SECTIONS_PER_AXIS + sy)
                                 * MOO_VOXEL_SECTIONS_PER_AXIS + sx;
                if (!moo_voxel_gen_section(w, ch, slot_s, base_x, base_y, base_z,
                                           sx, sy, sz)) {
                    return false;
                }
            }
        }
    }
    ch->dirty = true; /* Geometrie neu -> Remesh noetig (siehe aktualisieren) */
    return true;
}

/* ========================================================================
 * Oeffentliche API
 * ======================================================================== */

MooValue moo_voxel_welt_neu(MooValue seed) {
    int32_t s = 0;
    if (seed.tag == MOO_NUMBER) {
        s = (int32_t)moo_as_number(seed);
    }
    /* None/andere Typen -> seed 0 (kein Wurf; Worldgen folgt spaeter). */

    MooVoxelWorld* vw = (MooVoxelWorld*)moo_alloc(sizeof(MooVoxelWorld));
    if (!vw) {
        moo_throw(moo_error("Voxel-Fehler: Speicher fuer VoxelWorld erschoepft"));
        return moo_none();
    }
    vw->refcount = 1;
    vw->seed = (uint32_t)s;
    vw->chunk_cap = MOO_VOXEL_INITIAL_CAP;
    vw->chunk_count = 0;
    vw->jobs = NULL;          /* Job-Pool erst bei erstem async aktualisieren (Phase 3) */
    size_t chunks_bytes = (size_t)vw->chunk_cap * sizeof(MooVoxelChunk);
    vw->chunks = (MooVoxelChunk*)moo_alloc(chunks_bytes);
    if (!vw->chunks) {
        moo_free(vw);
        moo_throw(moo_error("Voxel-Fehler: Speicher fuer Chunk-Hashmap erschoepft"));
        return moo_none();
    }
    /* moo_alloc nutzt malloc und nullt NICHT -> Slots explizit leeren. */
    memset(vw->chunks, 0, chunks_bytes);

    MooValue result;
    result.tag = MOO_VOXELWORLD;
    moo_val_set_ptr(&result, vw);
    return result;
}

/* Generiert die vollstaendige vertikale (Z-)Chunk-Saeule fuer die horizontale
 * Chunk-Position (cx, cz). cx/cz sind hier die beiden HORIZONTALEN Chunk-
 * Koordinaten (X- und Y-Chunkindex); die vertikale Z-Achse wird ueber den
 * gesamten Terrain-Hoehenbereich automatisch aufgespannt. Seed-deterministisch
 * (siehe moo_voxel_gen_block). Idempotent: bereits generierte Chunks bleiben
 * erhalten (occupied). Rueckgabe: Anzahl generierter (neuer) Chunks. */
MooValue moo_voxel_generieren(MooValue welt, MooValue cx_v, MooValue cz_v) {
    MooVoxelWorld* vw = moo_voxel_check(welt, "voxel_generieren");
    if (!vw) return moo_none();

    int32_t cx = moo_voxel_coord(cx_v);  /* horizontaler X-Chunkindex */
    int32_t cy = moo_voxel_coord(cz_v);  /* horizontaler Y-Chunkindex */

    /* Vertikaler Z-Bereich: vom Boden (Welt-Z 0) bis ueber die hoechstmoegliche
     * Oberflaeche bzw. Wasserlinie. */
    int32_t max_z = MOO_VOXEL_GEN_BASE_HEIGHT + MOO_VOXEL_GEN_AMPLITUDE;
    if (MOO_VOXEL_GEN_SEA_LEVEL > max_z) max_z = MOO_VOXEL_GEN_SEA_LEVEL;
    int32_t cz_lo = moo_voxel_floordiv(0, MOO_VOXEL_CHUNK_DIM);
    int32_t cz_hi = moo_voxel_floordiv(max_z, MOO_VOXEL_CHUNK_DIM);

    int64_t neu = 0;
    for (int32_t cz = cz_lo; cz <= cz_hi; cz++) {
        if (moo_voxel_lookup(vw, cx, cy, cz)) continue; /* schon da */
        if (!moo_voxel_gen_chunk(vw, cx, cy, cz)) {
            moo_throw(moo_error("Voxel-Fehler in voxel_generieren: Worldgen-Speicher erschoepft"));
            return moo_none();
        }
        neu++;
    }
    return moo_number((double)neu);
}

MooValue moo_voxel_setzen(MooValue welt, MooValue x, MooValue y, MooValue z, MooValue block_id) {
    MooVoxelWorld* vw = moo_voxel_check(welt, "voxel_setzen");
    if (!vw) return moo_none();

    if (block_id.tag != MOO_NUMBER) {
        moo_throw(moo_error("Voxel-Fehler in voxel_setzen: Block-ID muss eine Zahl sein"));
        return moo_none();
    }
    double id_d = moo_as_number(block_id);
    int32_t id = (int32_t)id_d;
    if (id < 0 || id > MOO_VOXEL_MAX_BLOCK_ID) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "Voxel-Fehler in voxel_setzen: Block-ID %d ausserhalb [0,%d]",
                 id, MOO_VOXEL_MAX_BLOCK_ID);
        moo_throw(moo_error(msg));
        return moo_none();
    }

    int32_t wx = moo_voxel_coord(x);
    int32_t wy = moo_voxel_coord(y);
    int32_t wz = moo_voxel_coord(z);

    int32_t cx = moo_voxel_floordiv(wx, MOO_VOXEL_CHUNK_DIM);
    int32_t cy = moo_voxel_floordiv(wy, MOO_VOXEL_CHUNK_DIM);
    int32_t cz = moo_voxel_floordiv(wz, MOO_VOXEL_CHUNK_DIM);
    int32_t lx = moo_voxel_floormod(wx, MOO_VOXEL_CHUNK_DIM);
    int32_t ly = moo_voxel_floormod(wy, MOO_VOXEL_CHUNK_DIM);
    int32_t lz = moo_voxel_floormod(wz, MOO_VOXEL_CHUNK_DIM);

    /* Last-Factor 0.75: bei Bedarf vor dem Einfuegen wachsen, damit immer ein
     * freier Slot existiert (Open Addressing). */
    if ((vw->chunk_count + 1) * 4 >= vw->chunk_cap * 3) {
        if (!moo_voxel_grow(vw)) {
            moo_throw(moo_error("Voxel-Fehler in voxel_setzen: Hashmap-Wachstum fehlgeschlagen (OOM)"));
            return moo_none();
        }
    }

    int32_t slot = moo_voxel_find_slot(vw, cx, cy, cz);
    MooVoxelChunk* ch = &vw->chunks[slot];
    if (!ch->occupied) {
        ch->occupied = true;
        ch->cx = cx;
        ch->cy = cy;
        ch->cz = cz;
        ch->sections = NULL;  /* komplett leerer Chunk = NULL (Plan-006) */
        ch->render_id = -1;   /* noch kein GPU-Cache */
        ch->dirty = true;     /* neu -> braucht Mesh */
        ch->dirty_ao = false; /* P006-R4: frischer Chunk wird ohnehin voll gemesht */
        vw->chunk_count++;
    }

    /* Schreiben ueber die Palette-Schicht. Luft in einen leeren Chunk bleibt
     * No-op (Chunk bleibt NULL/leer). Bei OOM (Palette/Index/Widen) wird sauber
     * geworfen, kein stiller Korrupt-Zustand (Plan-Regel KEINE HACKS). */
    if (!moo_voxel_chunk_block_set(ch, moo_voxel_local_index(lx, ly, lz), (uint16_t)id)) {
        moo_throw(moo_error("Voxel-Fehler in voxel_setzen: Speicher fuer Chunk-Daten erschoepft"));
        return moo_none();
    }

    /* Dirty-Propagation (Plan-005 1b, Risiko 6): der geaenderte Chunk braucht
     * ein Remesh. Face-Meshing kuckt ueber die Chunk-Grenze auf die GENAU 6
     * direkten Nachbarn (nicht 26 — das braeuchte erst Vertex-AO in Phase 3).
     * Liegt das geaenderte Voxel auf einer Chunk-Randflaeche (lokale Koordinate
     * 0 oder DIM-1), aendert sich auch die Sichtbarkeit der zugewandten Faces
     * des angrenzenden Nachbar-Chunks -> diesen ebenfalls dirty markieren.
     * Nur BEREITS EXISTIERENDE Nachbarn werden markiert: ein nicht-allokierter
     * Nachbar ist komplett Luft, hat keine Geometrie und damit nichts zu remeshen
     * (er wird beim eigenen ersten Setzen ohnehin dirty angelegt). */
    ch->dirty = true;
    if (lx == 0)                          moo_voxel_mark_dirty(vw, cx - 1, cy, cz);
    if (lx == MOO_VOXEL_CHUNK_DIM - 1)    moo_voxel_mark_dirty(vw, cx + 1, cy, cz);
    if (ly == 0)                          moo_voxel_mark_dirty(vw, cx, cy - 1, cz);
    if (ly == MOO_VOXEL_CHUNK_DIM - 1)    moo_voxel_mark_dirty(vw, cx, cy + 1, cz);
    if (lz == 0)                          moo_voxel_mark_dirty(vw, cx, cy, cz - 1);
    if (lz == MOO_VOXEL_CHUNK_DIM - 1)    moo_voxel_mark_dirty(vw, cx, cy, cz + 1);

    /* --------------------------------------------------------------------
     * P006-R4 — AO-Dirty fuer DIAGONALE (Kanten-/Eck-)Nachbarn.
     *
     * Der Greedy+AO-Mesher liest pro Chunk ueber pad_fill einen
     * (DIM+2)^3-Rand: jeder Chunk samplet 1 Voxel TIEF in jeden seiner 26
     * Nachbarn hinein (fuer Face-Cull UND fuer Boundary-Vertex-AO). Aendert
     * sich ein RANDvoxel von Chunk C, ist nicht nur die zugewandte Flaeche der
     * 6 Face-Nachbarn betroffen (oben markiert), sondern auch die an der
     * Kante/Ecke ANLIEGENDE Boundary-AO der diagonalen Nachbarn.
     *
     * Welche Nachbarn ein Voxel ueberhaupt erreicht: ein Nachbar mit
     * Chunk-Offset (ox,oy,oz) in {-1,0,+1}^3 samplet dieses Voxel GENAU DANN,
     * wenn es in seiner +1-Pad-Schale liegt — pro Achse heisst das:
     *   ox=-1 nur wenn lx==0        (Voxel bildet die +DIM-Pad-Zelle von cx-1)
     *   ox=+1 nur wenn lx==DIM-1    (Voxel bildet die -1-Pad-Zelle  von cx+1)
     *   ox= 0 immer.
     * (analog y,z). Das ergibt die exakte, MINIMALE betroffene Nachbarmenge:
     * Innen-Voxel -> nur (0,0,0) = eigener Chunk (0 Nachbarn);
     * Flaechen-Voxel -> +1 Face-Nachbar (oben via dirty erledigt);
     * Kanten-Voxel -> +2 Face + 1 diagonaler Kanten-Nachbar (bis 18 gesamt);
     * Eck-Voxel    -> +3 Face + 3 Kanten + 1 Eck-Nachbar (bis 26 gesamt).
     *
     * Die 6 Face-Offsets (genau ein Nicht-Null-Anteil) sind oben bereits via
     * moo_voxel_mark_dirty abgedeckt; HIER nur die DIAGONALEN (>=2 Nicht-Null-
     * Anteile) zusaetzlich AO-dirty markieren. Damit bleibt der gepruefte
     * 6-Face-Dirty-Contract unveraendert und die AO-Dirty kommt ADDITIV obendrauf
     * — nur fuer die wirklich AO-betroffenen Diagonalen, kein pauschales 26er-
     * Remesh (Plan-006-Risiko 5: Remesh-Kaskaden vermeiden). */
    {
        const int ox_lo = (lx == 0)                       ? -1 : 0;
        const int ox_hi = (lx == MOO_VOXEL_CHUNK_DIM - 1) ?  1 : 0;
        const int oy_lo = (ly == 0)                       ? -1 : 0;
        const int oy_hi = (ly == MOO_VOXEL_CHUNK_DIM - 1) ?  1 : 0;
        const int oz_lo = (lz == 0)                       ? -1 : 0;
        const int oz_hi = (lz == MOO_VOXEL_CHUNK_DIM - 1) ?  1 : 0;
        for (int ox = ox_lo; ox <= ox_hi; ox++) {
            for (int oy = oy_lo; oy <= oy_hi; oy++) {
                for (int oz = oz_lo; oz <= oz_hi; oz++) {
                    /* nur DIAGONALE Offsets (>=2 Nicht-Null-Komponenten);
                     * (0,0,0)=eigener Chunk und die 6 Face-Offsets sind schon
                     * abgedeckt. */
                    int nonzero = (ox != 0) + (oy != 0) + (oz != 0);
                    if (nonzero < 2) continue;
                    moo_voxel_mark_dirty_ao(vw, cx + ox, cy + oy, cz + oz);
                }
            }
        }
    }

    return moo_bool(true);
}

MooValue moo_voxel_holen(MooValue welt, MooValue x, MooValue y, MooValue z) {
    MooVoxelWorld* vw = moo_voxel_check(welt, "voxel_holen");
    if (!vw) return moo_none();

    int32_t wx = moo_voxel_coord(x);
    int32_t wy = moo_voxel_coord(y);
    int32_t wz = moo_voxel_coord(z);

    int32_t cx = moo_voxel_floordiv(wx, MOO_VOXEL_CHUNK_DIM);
    int32_t cy = moo_voxel_floordiv(wy, MOO_VOXEL_CHUNK_DIM);
    int32_t cz = moo_voxel_floordiv(wz, MOO_VOXEL_CHUNK_DIM);

    int32_t slot = moo_voxel_find_slot(vw, cx, cy, cz);
    MooVoxelChunk* ch = &vw->chunks[slot];
    if (!ch->occupied || moo_voxel_chunk_is_empty(ch)) {
        /* Nie allokierter / leerer Chunk = Luft. voxel_holen ist ein REINER
         * Lesezugriff (P0.5-Contract): es generiert NICHT lazy, damit nie
         * beschriebene Chunks deterministisch Luft liefern (Pflichttests
         * test_voxel_empty_chunk_air / _refcount_release). Worldgen laeuft
         * explizit ueber moo_voxel_generieren. */
        return moo_number(0.0);
    }

    int32_t lx = moo_voxel_floormod(wx, MOO_VOXEL_CHUNK_DIM);
    int32_t ly = moo_voxel_floormod(wy, MOO_VOXEL_CHUNK_DIM);
    int32_t lz = moo_voxel_floormod(wz, MOO_VOXEL_CHUNK_DIM);
    return moo_number((double)moo_voxel_chunk_block_get(ch, moo_voxel_local_index(lx, ly, lz)));
}

MooValue moo_voxel_chunk_entladen(MooValue welt, MooValue x, MooValue y, MooValue z) {
    MooVoxelWorld* vw = moo_voxel_check(welt, "voxel_chunk_entladen");
    if (!vw) return moo_none();

    int32_t wx = moo_voxel_coord(x);
    int32_t wy = moo_voxel_coord(y);
    int32_t wz = moo_voxel_coord(z);

    int32_t cx = moo_voxel_floordiv(wx, MOO_VOXEL_CHUNK_DIM);
    int32_t cy = moo_voxel_floordiv(wy, MOO_VOXEL_CHUNK_DIM);
    int32_t cz = moo_voxel_floordiv(wz, MOO_VOXEL_CHUNK_DIM);

    uint32_t mask = (uint32_t)vw->chunk_cap - 1u;
    uint32_t idx = moo_voxel_hash3(cx, cy, cz) & mask;
    while (vw->chunks[idx].occupied) {
        if (vw->chunks[idx].cx == cx &&
            vw->chunks[idx].cy == cy &&
            vw->chunks[idx].cz == cz) {
            /* Gefunden -> GPU-Render-Cache + CPU-Blocks freigeben, Slot loeschen.
             * Render-ID-Lifetime (Plan-005 1b): GENAU EIN chunk_delete pro
             * entladenem Chunk. moo_3d_chunk_delete ist ohne aktives Backend ein
             * safe no-op; trotzdem nur aufrufen wenn render_id gesetzt war, und
             * danach auf -1 setzen, damit kein zweites Mal geloescht wird. */
            if (vw->chunks[idx].render_id >= 0) {
                moo_3d_chunk_delete(moo_number((double)vw->chunks[idx].render_id));
                vw->chunks[idx].render_id = -1;
            }
            moo_voxel_chunk_free_data(&vw->chunks[idx]);
            vw->chunks[idx].occupied = false;
            vw->chunk_count--;

            /* Knuth Backward-Shift-Deletion (Algorithm R, TAOCP Vol.3 6.4):
             * Beim Loeschen eines Slots in einer linearen Probe-Kette darf kein
             * "Loch" entstehen, das nachfolgende Eintraege derselben Kette
             * unauffindbar macht. Wir wandern vorwaerts und ziehen jeden
             * Eintrag, dessen Ideal-Position <= aktueller Luecke liegt, in die
             * Luecke nach. chunk_count bleibt unveraendert (nur Verschiebung). */
            uint32_t hole = idx;
            uint32_t j = (idx + 1u) & mask;
            while (vw->chunks[j].occupied) {
                uint32_t home = moo_voxel_hash3(vw->chunks[j].cx,
                                                vw->chunks[j].cy,
                                                vw->chunks[j].cz) & mask;
                /* chunks[j] darf in das Loch (hole) gezogen werden, solange home
                 * NICHT zyklisch im offenen Intervall (hole, j] liegt. Liegt home
                 * in (hole, j], dann sitzt der Eintrag bereits >= seiner
                 * Home-Position hinter dem Loch und darf nicht zurueckwandern. */
                bool home_in_range; /* home in (hole, j] zyklisch */
                if (hole < j) {
                    home_in_range = (home > hole) && (home <= j);
                } else { /* hole > j: Intervall laeuft ueber das Array-Ende */
                    home_in_range = (home > hole) || (home <= j);
                }
                if (!home_in_range) {
                    vw->chunks[hole] = vw->chunks[j];
                    vw->chunks[j].occupied = false;
                    hole = j;
                }
                j = (j + 1u) & mask;
            }
            return moo_bool(true);
        }
        idx = (idx + 1u) & mask;
    }
    return moo_bool(false); /* Chunk war nicht geladen. */
}

MooValue moo_voxel_ram_statistik(MooValue welt) {
    MooVoxelWorld* vw = moo_voxel_check(welt, "voxel_ram_statistik");
    if (!vw) return moo_none();

    int64_t chunks = 0;
    int64_t empty_chunks = 0;
    int64_t bytes_blocks = 0;    /* Section-Index-Arrays (eigentliche Voxel-Daten) */
    int64_t bytes_palette = 0;   /* Section-Paletten (distinkte-ID-Tabellen) */
    int64_t bytes_sections = 0;  /* Plan-006: Section-Header-Arrays (64 pro Chunk) */

    for (int32_t i = 0; i < vw->chunk_cap; i++) {
        if (!vw->chunks[i].occupied) continue;
        chunks++;
        MooVoxelChunk* ch = &vw->chunks[i];
        if (!ch->sections) {
            /* Komplett leerer Chunk: 0 Voxel-Datenbytes (Plan-005/006-Verhalten,
             * Review K1). Zaehlt als empty_chunk, traegt KEINE Section-Header. */
            empty_chunks++;
            continue;
        }
        /* Plan-006: Section-Header-Array (immer 64 Eintraege, sobald allokiert).
         * Diese Bytes fliessen ehrlich in bytes_total (Review K1/K2), neu als
         * additiver Key bytes_sections — bestehende Keys bleiben unveraendert. */
        bytes_sections += (int64_t)MOO_VOXEL_SECTIONS_PER_CHUNK
                          * (int64_t)sizeof(MooVoxelSection);
        for (int32_t s = 0; s < MOO_VOXEL_SECTIONS_PER_CHUNK; s++) {
            const MooVoxelSection* sec = &ch->sections[s];
            if (sec->mode != MOO_VOXEL_SECTION_PALETTE) continue; /* EMPTY/SOLID: 0 Datenbytes */
            /* bytes_blocks = bitgepacktes Index-Array der Section.
             * bytes_palette = die distinkte-ID-Tabelle der Section. Semantik wie
             * im alten chunk-weiten Layout (Review K2 — Keys nicht umdefiniert). */
            bytes_blocks  += (int64_t)moo_voxel_index_words(sec->bits)
                             * (int64_t)sizeof(uint32_t);
            bytes_palette += (int64_t)sec->palette_cap
                             * (int64_t)sizeof(uint16_t);
        }
    }

    /* Mesh-Geometrie liegt im GPU-Render-Cache (separates Modul) -> 0. */
    int64_t bytes_mesh = 0;
    /* Verwaltungs-Overhead (World-Struct + Hashmap-Tabelle) in bytes_total. */
    int64_t bytes_overhead =
        (int64_t)sizeof(MooVoxelWorld) +
        (int64_t)vw->chunk_cap * (int64_t)sizeof(MooVoxelChunk);
    /* bytes_total enthaelt ehrlich auch die Section-Header (Review K1/K2). */
    int64_t bytes_total = bytes_blocks + bytes_palette + bytes_sections
                          + bytes_mesh + bytes_overhead;

    MooValue d = moo_dict_new();
    moo_dict_set(d, moo_string_new("chunks"),        moo_number((double)chunks));
    moo_dict_set(d, moo_string_new("bytes_blocks"),  moo_number((double)bytes_blocks));
    moo_dict_set(d, moo_string_new("bytes_palette"), moo_number((double)bytes_palette));
    moo_dict_set(d, moo_string_new("bytes_mesh"),    moo_number((double)bytes_mesh));
    moo_dict_set(d, moo_string_new("bytes_total"),   moo_number((double)bytes_total));
    moo_dict_set(d, moo_string_new("empty_chunks"),  moo_number((double)empty_chunks));
    /* Additiver Plan-006-Key (Review K2): Section-Header-Bytes separat sichtbar. */
    moo_dict_set(d, moo_string_new("bytes_sections"), moo_number((double)bytes_sections));
    return d;
}

/* Expliziter Welt-Optimierungslauf (P006-R3): stuft alle PALETTE-Sections aller
 * belegten Chunks herab (PALETTE->SOLID/EMPTY wo wieder uniform) und kompaktiert
 * die uebrigen Paletten (ungenutzte IDs raus, Bitbreite runter). Komplett leer
 * gewordene Chunks kollabieren auf NULL (0 Datenbytes, K1).
 *
 * WANN nutzen: nach groesseren Bau-/Abbau-Sessions, vor dem Speichern, oder als
 * periodischer Wartungslauf. Im normalen Spielbetrieb passiert der Downgrade
 * ohnehin lazy in moo_voxel_aktualisieren (nur fuer dirty Chunks) — dieser
 * Builtin erzwingt ihn fuer ALLE Chunks unabhaengig vom dirty-Flag und ohne
 * 3D-Backend (rein CPU-seitig, daher auch headless / im Benchmark nutzbar).
 *
 * THREADING (K5): MUTIERT das Section-Layout -> ausschliesslich Main-Thread,
 * NIE waehrend aktiver Mesh-Worker. Im single-threaded Gameplay-Modell von moo
 * (setzen/aktualisieren/optimieren laufen seriell) ist das garantiert: zwischen
 * zwei aktualisieren-Aufrufen ist der Threadpool gejoint, es laufen keine Worker.
 *
 * Rueckgabe: Anzahl der Chunks, deren Layout sich geaendert hat. Markiert
 * geaenderte Chunks NICHT dirty — der Downgrade ist lese-resultat- und damit
 * geometrie-neutral, ein Remesh ist nicht noetig. */
MooValue moo_voxel_welt_optimieren(MooValue welt) {
    MooVoxelWorld* vw = moo_voxel_check(welt, "voxel_welt_optimieren");
    if (!vw) return moo_none();

    int64_t changed = 0;
    for (int32_t i = 0; i < vw->chunk_cap; i++) {
        MooVoxelChunk* ch = &vw->chunks[i];
        if (!ch->occupied || !ch->sections) continue;
        if (moo_voxel_chunk_optimize(ch)) changed++;
    }
    return moo_number((double)changed);
}



/* ========================================================================
 * Phase 3 (Plan-005 V3.2): Greedy-Mesher mit Vertex-AO -> CPU-Vertex-Buffer
 *
 * Diese Schicht ist VOLLSTAENDIG GPU-frei und moo-Heap-frei: sie schreibt nur
 * in einen MooVoxelMeshBuf (rohe float/uint32-Arrays auf moo_alloc-Heap). Damit
 * darf sie aus einem Worker-Thread laufen (der moo-Heap ist nicht thread-safe).
 * Der Main-Thread spielt den Buffer spaeter via moo_3d_chunk_begin/triangle/end
 * auf die GPU.
 *
 * Greedy: pro Achsenrichtung (6 Faces) werden koplanare, gleich-eingefaerbte
 * Faces (gleiche Block-ID UND gleiches AO-Muster) zu Rechtecken zusammengefasst
 * (Mikulik/0fps-Verfahren). AO: pro Quad-Ecke ein 0..3-Level aus den drei
 * diagonalen Nachbarn der Face; die vier Eck-Level werden zur Quad-Faerbung
 * gemittelt (eine Farbe pro Dreieck, da moo_3d_triangle nur eine Farbe je
 * Dreieck nimmt).
 * ======================================================================== */

/* Block-Basisfarben als RGB (0..255), Index = Block-ID. Entspricht
 * MOO_VOXEL_BLOCK_HEX, nur als Zahlen damit der Worker keinen String braucht. */
static const uint8_t MOO_VOXEL_BLOCK_RGB[MOO_VOXEL_MAX_BLOCK_ID + 1][3] = {
    {   0,   0,   0 }, /* 0 luft  (ungenutzt) */
    {  76, 175,  80 }, /* 1 gras  #4CAF50 */
    { 121,  85,  72 }, /* 2 erde  #795548 */
    { 158, 158, 158 }, /* 3 stein #9E9E9E */
    { 251, 192,  45 }, /* 4 sand  #FBC02D */
    {  33, 150, 243 }, /* 5 wasser #2196F3 (opak) */
};

/* CPU-Vertex-Buffer: pro Dreieck 9 Positions-Floats + 1 gepackte RGB-Farbe.
 * Wachstum via moo_alloc + memcpy + moo_free (kein moo_realloc, damit die
 * bestehenden ASan-Harnesses ohne realloc-Stub gruen bleiben). */
typedef struct {
    float*    verts;   /* 9 floats pro Dreieck (x1,y1,z1, x2,y2,z2, x3,y3,z3) */
    uint32_t* cols;    /* 1 gepackte 0x00RRGGBB-Farbe pro Dreieck */
    size_t    tri_count;
    size_t    tri_cap;
} MooVoxelMeshBuf;

static void mesh_buf_init(MooVoxelMeshBuf* b) {
    b->verts = NULL; b->cols = NULL; b->tri_count = 0; b->tri_cap = 0;
}

static void mesh_buf_free(MooVoxelMeshBuf* b) {
    if (b->verts) { moo_free(b->verts); b->verts = NULL; }
    if (b->cols)  { moo_free(b->cols);  b->cols  = NULL; }
    b->tri_count = 0; b->tri_cap = 0;
}

/* Stellt Platz fuer mindestens want zusaetzliche Dreiecke sicher. Liefert false
 * bei OOM (Aufrufer bricht das Meshing dann sauber ab). */
static bool mesh_buf_reserve(MooVoxelMeshBuf* b, size_t want) {
    if (b->tri_count + want <= b->tri_cap) return true;
    size_t ncap = b->tri_cap ? b->tri_cap * 2 : 256;
    while (ncap < b->tri_count + want) ncap *= 2;
    float* nv = (float*)moo_alloc(ncap * 9 * sizeof(float));
    if (!nv) return false;
    uint32_t* nc = (uint32_t*)moo_alloc(ncap * sizeof(uint32_t));
    if (!nc) { moo_free(nv); return false; }
    if (b->tri_count) {
        memcpy(nv, b->verts, b->tri_count * 9 * sizeof(float));
        memcpy(nc, b->cols,  b->tri_count * sizeof(uint32_t));
    }
    if (b->verts) moo_free(b->verts);
    if (b->cols)  moo_free(b->cols);
    b->verts = nv; b->cols = nc; b->tri_cap = ncap;
    return true;
}

static void mesh_buf_push_tri(MooVoxelMeshBuf* b,
                              float ax, float ay, float az,
                              float bx, float by, float bz,
                              float cx, float cy, float cz,
                              uint32_t col) {
    float* v = b->verts + b->tri_count * 9;
    v[0]=ax; v[1]=ay; v[2]=az; v[3]=bx; v[4]=by; v[5]=bz; v[6]=cx; v[7]=cy; v[8]=cz;
    b->cols[b->tri_count] = col;
    b->tri_count++;
}

/* Skaliert eine Block-Basisfarbe mit einem AO-Faktor (0.55..1.0) und packt sie
 * nach 0x00RRGGBB. ao01 in [0,1] (1 = unverschattet). */
static uint32_t mesh_color_ao(uint16_t id, float ao01) {
    int cid = (id <= MOO_VOXEL_MAX_BLOCK_ID) ? (int)id : 0;
    float f = 0.55f + 0.45f * ao01; /* nie ganz schwarz */
    int r = (int)(MOO_VOXEL_BLOCK_RGB[cid][0] * f + 0.5f);
    int g = (int)(MOO_VOXEL_BLOCK_RGB[cid][1] * f + 0.5f);
    int bl = (int)(MOO_VOXEL_BLOCK_RGB[cid][2] * f + 0.5f);
    if (r > 255)  r = 255;
    if (g > 255)  g = 255;
    if (bl > 255) bl = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

/* Liest die Block-ID an Welt-Voxel (wx,wy,wz). Self-contained (nutzt nur frueh
 * definierte Helfer) damit dieser Block keine Vorwaerts-Decl auf den spaeteren
 * moo_voxel_world_block braucht. Nicht-allokierter/leerer Chunk = Luft. */
static uint16_t mesh_world_block(MooVoxelWorld* w, int32_t wx, int32_t wy, int32_t wz) {
    int32_t cx = moo_voxel_floordiv(wx, MOO_VOXEL_CHUNK_DIM);
    int32_t cy = moo_voxel_floordiv(wy, MOO_VOXEL_CHUNK_DIM);
    int32_t cz = moo_voxel_floordiv(wz, MOO_VOXEL_CHUNK_DIM);
    MooVoxelChunk* ch = moo_voxel_lookup(w, cx, cy, cz);
    if (!ch || moo_voxel_chunk_is_empty(ch)) return 0;
    int32_t lx = moo_voxel_floormod(wx, MOO_VOXEL_CHUNK_DIM);
    int32_t ly = moo_voxel_floormod(wy, MOO_VOXEL_CHUNK_DIM);
    int32_t lz = moo_voxel_floormod(wz, MOO_VOXEL_CHUNK_DIM);
    return moo_voxel_chunk_block_get(ch, moo_voxel_local_index(lx, ly, lz));
}

/* AO-Level (0..3) fuer eine Vertex-Ecke nach dem 0fps/Mikola-Schema:
 *   side1, side2 = die zwei kantenseitigen Nachbarn, corner = der diagonale.
 *   side1 && side2  -> 0 (voll verschattet)
 *   sonst           -> 3 - (side1 + side2 + corner)
 * Eingaben sind \"belegt?\"-Flags (1 = fester Block, 0 = Luft). */
static inline int mesh_ao_level(int side1, int side2, int corner) {
    if (side1 && side2) return 0;
    return 3 - (side1 + side2 + corner);
}

/* Gepadderter lokaler Block-Cache: (DIM+2)^3 uint16, lokale Koordinate +1
 * versetzt (Index 0 = Welt-Koordinate base-1, Index DIM+1 = base+DIM). Wird
 * EINMAL pro Chunk-Mesh aus der Welt befuellt; danach liest der Greedy/AO-
 * Hot-Path mit reinem O(1)-Array-Zugriff statt floordiv+Hashmap-Lookup je
 * Sample (vorher ~2M Hashmap-Lookups/Chunk -> ms-Kosten). Die +1-Randschicht
 * traegt die direkten Nachbarn fuer Face-Cull UND die diagonalen Nachbarn fuer
 * korrektes Boundary-AO. */
#define MOO_VOXEL_PAD (MOO_VOXEL_CHUNK_DIM + 2)
typedef struct { uint16_t* cells; } MooVoxelPad;

static inline uint16_t pad_at(const MooVoxelPad* p, int x, int y, int z) {
    /* x,y,z in [-1, DIM] (lokal). */
    return p->cells[((size_t)(z + 1) * MOO_VOXEL_PAD + (y + 1)) * MOO_VOXEL_PAD + (x + 1)];
}

static bool pad_fill(MooVoxelWorld* w, const MooVoxelChunk* ch, MooVoxelPad* p) {
    const int DIM = MOO_VOXEL_CHUNK_DIM;
    p->cells = (uint16_t*)moo_alloc((size_t)MOO_VOXEL_PAD * MOO_VOXEL_PAD * MOO_VOXEL_PAD
                                    * sizeof(uint16_t));
    if (!p->cells) return false;
    int32_t bx = ch->cx * DIM, by = ch->cy * DIM, bz = ch->cz * DIM;
    for (int z = -1; z <= DIM; z++)
        for (int y = -1; y <= DIM; y++)
            for (int x = -1; x <= DIM; x++)
                p->cells[((size_t)(z + 1) * MOO_VOXEL_PAD + (y + 1)) * MOO_VOXEL_PAD + (x + 1)]
                    = mesh_world_block(w, bx + x, by + y, bz + z);
    return true;
}

static void pad_free(MooVoxelPad* p) {
    if (p->cells) { moo_free(p->cells); p->cells = NULL; }
}

/* Greedy-Vermaschung EINER der 6 Face-Richtungen fuer einen Chunk.
 *   d   = Hauptachse 0=X,1=Y,2=Z; back = false -> +Achse-Face, true -> -Achse.
 * Arbeitet schichtweise entlang der Hauptachse; pro Schicht wird eine 2D-Maske
 * aus (block_id, vier AO-Level) aufgebaut und greedy zu Rechtecken gemerged.
 * pad = gepadderter Block-Cache des Chunks (lokale Koordinaten); out: Ziel. */
static bool greedy_mesh_axis(const MooVoxelChunk* ch, const MooVoxelPad* pad,
                             int d, bool back, MooVoxelMeshBuf* out) {
    const int DIM = MOO_VOXEL_CHUNK_DIM;
    int u = (d + 1) % 3; /* erste Quer-Achse */
    int v = (d + 2) % 3; /* zweite Quer-Achse */
    int32_t base[3] = { ch->cx * DIM, ch->cy * DIM, ch->cz * DIM };
    int dn = back ? -1 : 1;             /* Richtung des Sicht-Nachbarn */

    /* Maske einer Schicht: id (0 = keine Face), + 4 AO-Level. */
    typedef struct { uint16_t id; uint8_t ao[4]; } MaskCell;
    MaskCell* mask = (MaskCell*)moo_alloc((size_t)DIM * DIM * sizeof(MaskCell));
    if (!mask) return false;

    for (int s = 0; s < DIM; s++) {
        /* Maske der Schicht s aufbauen. */
        for (int j = 0; j < DIM; j++) {
            for (int i = 0; i < DIM; i++) {
                int c[3]; c[d] = s; c[u] = i; c[v] = j; /* lokale Koordinaten */
                uint16_t here = pad_at(pad, c[0], c[1], c[2]);
                MaskCell* m = &mask[j * DIM + i];
                m->id = 0;
                if (here == 0) continue;            /* Luft: keine Face */
                /* Sicht-Nachbar auf der Face-Seite (lokal, +1-Pad deckt Rand ab). */
                int nb[3] = { c[0], c[1], c[2] }; nb[d] += dn;
                if (pad_at(pad, nb[0], nb[1], nb[2]) != 0) continue; /* verdeckt */
                m->id = here;
                /* AO: vier Ecken der Face. Die Face liegt auf der dn-Seite des
                 * Voxels. Wir betrachten die 8 Voxel auf der dn-Seitenebene. */
                int8_t du[3] = {0,0,0}, dv[3] = {0,0,0};
                du[u] = 1; dv[v] = 1;
                /* Belegt-Flags der 8 Nachbarn in der Face-Ebene (nb-Ebene). */
                #define OCC(au, av) (pad_at(pad, \
                    nb[0] + (au)*du[0] + (av)*dv[0], \
                    nb[1] + (au)*du[1] + (av)*dv[1], \
                    nb[2] + (au)*du[2] + (av)*dv[2]) != 0 ? 1 : 0)
                int n00=OCC(-1,-1), n01=OCC(0,-1), n02=OCC(1,-1);
                int n10=OCC(-1, 0),                n12=OCC(1, 0);
                int n20=OCC(-1, 1), n21=OCC(0, 1), n22=OCC(1, 1);
                #undef OCC
                /* Ecken (u,v)-lokal: (0,0),(1,0),(1,1),(0,1). */
                m->ao[0] = (uint8_t)mesh_ao_level(n10, n01, n00); /* -u,-v */
                m->ao[1] = (uint8_t)mesh_ao_level(n12, n01, n02); /* +u,-v */
                m->ao[2] = (uint8_t)mesh_ao_level(n12, n21, n22); /* +u,+v */
                m->ao[3] = (uint8_t)mesh_ao_level(n10, n21, n20); /* -u,+v */
            }
        }

        /* Greedy-Merge der Maske. */
        for (int j = 0; j < DIM; j++) {
            for (int i = 0; i < DIM;) {
                MaskCell c0 = mask[j * DIM + i];
                if (c0.id == 0) { i++; continue; }
                /* Breite (entlang u) ausdehnen solange id+AO gleich. */
                int wdt = 1;
                while (i + wdt < DIM) {
                    MaskCell* c = &mask[j * DIM + i + wdt];
                    if (c->id != c0.id ||
                        c->ao[0]!=c0.ao[0] || c->ao[1]!=c0.ao[1] ||
                        c->ao[2]!=c0.ao[2] || c->ao[3]!=c0.ao[3]) break;
                    wdt++;
                }
                /* Hoehe (entlang v) ausdehnen solange ganze Zeile passt. */
                int hgt = 1;
                bool grow = true;
                while (j + hgt < DIM && grow) {
                    for (int k = 0; k < wdt; k++) {
                        MaskCell* c = &mask[(j + hgt) * DIM + i + k];
                        if (c->id != c0.id ||
                            c->ao[0]!=c0.ao[0] || c->ao[1]!=c0.ao[1] ||
                            c->ao[2]!=c0.ao[2] || c->ao[3]!=c0.ao[3]) { grow = false; break; }
                    }
                    if (grow) hgt++;
                }

                /* Quad in Weltkoordinaten erzeugen. Die Face liegt bei der
                 * Hauptachsen-Koordinate s (+1 fuer +Achse-Face). */
                float face = (float)(base[d] + s + (back ? 0 : 1));
                float u0 = (float)(base[u] + i);
                float u1 = (float)(base[u] + i + wdt);
                float v0 = (float)(base[v] + j);
                float v1 = (float)(base[v] + j + hgt);

                /* Vier Eckpunkte (u,v) -> 3D, Hauptachse = face. */
                float p[4][3];
                #define SETP(idx, uu, vv) do { \
                    p[idx][d] = face; p[idx][u] = (uu); p[idx][v] = (vv); } while(0)
                SETP(0, u0, v0); SETP(1, u1, v0); SETP(2, u1, v1); SETP(3, u0, v1);
                #undef SETP

                /* AO-Farben pro Ecke -> Quad-Mittelfarbe (eine Farbe je Dreieck).
                 * ao-Level 0..3 -> Helligkeit 0.33..1.0. */
                float aol[4];
                for (int e = 0; e < 4; e++) aol[e] = (float)c0.ao[e] / 3.0f;
                /* Dreieck 1 = Ecken 0,1,2 ; Dreieck 2 = Ecken 0,2,3.
                 * Pro Dreieck den AO-Durchschnitt der 3 Ecken als Farbe. */
                float ao_t1 = (aol[0] + aol[1] + aol[2]) / 3.0f;
                float ao_t2 = (aol[0] + aol[2] + aol[3]) / 3.0f;
                uint32_t col1 = mesh_color_ao(c0.id, ao_t1);
                uint32_t col2 = mesh_color_ao(c0.id, ao_t2);

                if (!mesh_buf_reserve(out, 2)) { moo_free(mask); return false; }
                /* Wicklung: fuer +Achse-Faces CCW von aussen, fuer -Achse
                 * umgekehrt, damit Backface-Culling stimmt. */
                if (back) {
                    mesh_buf_push_tri(out, p[0][0],p[0][1],p[0][2],
                                            p[2][0],p[2][1],p[2][2],
                                            p[1][0],p[1][1],p[1][2], col1);
                    mesh_buf_push_tri(out, p[0][0],p[0][1],p[0][2],
                                            p[3][0],p[3][1],p[3][2],
                                            p[2][0],p[2][1],p[2][2], col2);
                } else {
                    mesh_buf_push_tri(out, p[0][0],p[0][1],p[0][2],
                                            p[1][0],p[1][1],p[1][2],
                                            p[2][0],p[2][1],p[2][2], col1);
                    mesh_buf_push_tri(out, p[0][0],p[0][1],p[0][2],
                                            p[2][0],p[2][1],p[2][2],
                                            p[3][0],p[3][1],p[3][2], col2);
                }

                /* Gemergte Zellen aus der Maske loeschen. */
                for (int dj = 0; dj < hgt; dj++)
                    for (int di = 0; di < wdt; di++)
                        mask[(j + dj) * DIM + i + di].id = 0;
                i += wdt;
            }
        }
    }
    moo_free(mask);
    return true;
}

/* Baut den kompletten Greedy+AO-Mesh eines Chunks in out (CPU-only, thread-safe
 * solange w waehrenddessen nicht mutiert wird). Liefert false bei OOM. */
static bool moo_voxel_build_mesh_cpu(MooVoxelWorld* w, const MooVoxelChunk* ch,
                                     MooVoxelMeshBuf* out) {
    MooVoxelPad pad; pad.cells = NULL;
    if (!pad_fill(w, ch, &pad)) return false;
    bool ok = true;
    for (int d = 0; d < 3 && ok; d++) {
        if (!greedy_mesh_axis(ch, &pad, d, false, out)) ok = false;
        if (ok && !greedy_mesh_axis(ch, &pad, d, true,  out)) ok = false;
    }
    pad_free(&pad);
    return ok;
}

/* Spielt einen fertigen CPU-Buffer auf die GPU (Main-Thread!). Erwartet
 * aktives Backend (Aufrufer prueft). Legt bei Bedarf die Render-ID an. */
static void moo_voxel_upload_mesh(MooVoxelChunk* ch, const MooVoxelMeshBuf* buf) {
    if (ch->render_id < 0) {
        MooValue rid = moo_3d_chunk_create();
        int32_t id = (int32_t)moo_as_number(rid);
        if (id < 0) { ch->render_id = -1; return; }
        ch->render_id = id;
    }
    moo_3d_chunk_begin(moo_number((double)ch->render_id));
    MooValue win = moo_none();
    char hex[8];
    for (size_t t = 0; t < buf->tri_count; t++) {
        const float* v = buf->verts + t * 9;
        uint32_t c = buf->cols[t];
        snprintf(hex, sizeof(hex), "#%06x", (unsigned)(c & 0xFFFFFFu));
        MooValue color = moo_string_new(hex);
        moo_3d_triangle(win,
            moo_number(v[0]), moo_number(v[1]), moo_number(v[2]),
            moo_number(v[3]), moo_number(v[4]), moo_number(v[5]),
            moo_number(v[6]), moo_number(v[7]), moo_number(v[8]), color);
    }
    moo_3d_chunk_end();
    ch->dirty = false;
}

/* ========================================================================
 * Phase 3 (Plan-005 V3.1): C-interne pthread Job-Queue
 *
 * EIN Pool pro VoxelWorld (lazy in vw->jobs). Worker holen Chunk-Slots aus der
 * Job-Queue, bauen via moo_voxel_build_mesh_cpu einen CPU-Buffer und legen ihn
 * im job->result ab. moo_voxel_aktualisieren (Main-Thread) enqueued alle dirty
 * Chunks, wartet bis alle Jobs fertig sind (Welt bleibt waehrenddessen stabil)
 * und lädt die Buffer dann seriell auf die GPU.
 *
 * KEINE moo-Channels (Risiko 7). GPU-Upload NUR im Main-Thread (Risiko 8).
 * Auf _WIN32 ist der Pool nicht kompiliert -> aktualisieren mesht synchron.
 * ======================================================================== */

typedef struct {
    int32_t         slot;     /* Hashmap-Slot des zu meshenden Chunks */
    MooVoxelMeshBuf result;   /* vom Worker befuellt */
    bool            ok;       /* false = OOM im Worker */
} MooVoxelJob;

#ifndef _WIN32
#define MOO_VOXEL_MAX_WORKERS 8

typedef struct {
    MooVoxelWorld*  world;
    pthread_t       threads[MOO_VOXEL_MAX_WORKERS];
    int             n_threads;
    pthread_mutex_t lock;
    pthread_cond_t  has_work;  /* Worker warten hier auf neue Jobs */
    pthread_cond_t  done;      /* Main wartet hier bis alle Jobs fertig */
    MooVoxelJob*    jobs;      /* aktueller Batch */
    int             n_jobs;    /* Anzahl Jobs im Batch */
    int             next_job;  /* naechster noch nicht gepoppter Job-Index */
    int             active;    /* gerade in Bearbeitung befindliche Jobs */
    bool            shutdown;  /* true -> Worker beenden sich */
} MooVoxelJobPool;

static void* moo_voxel_worker(void* arg) {
    MooVoxelJobPool* pool = (MooVoxelJobPool*)arg;
    for (;;) {
        pthread_mutex_lock(&pool->lock);
        while (!pool->shutdown && pool->next_job >= pool->n_jobs)
            pthread_cond_wait(&pool->has_work, &pool->lock);
        if (pool->shutdown) { pthread_mutex_unlock(&pool->lock); break; }
        int ji = pool->next_job++;
        pool->active++;
        MooVoxelWorld* w = pool->world;
        MooVoxelJob* job = &pool->jobs[ji];
        pthread_mutex_unlock(&pool->lock);

        /* Lockfreies Meshing: zwischen Enqueue und Join wird w nicht mutiert. */
        const MooVoxelChunk* ch = &w->chunks[job->slot];
        mesh_buf_init(&job->result);
        job->ok = moo_voxel_build_mesh_cpu(w, ch, &job->result);

        pthread_mutex_lock(&pool->lock);
        pool->active--;
        if (pool->active == 0 && pool->next_job >= pool->n_jobs)
            pthread_cond_signal(&pool->done);
        pthread_mutex_unlock(&pool->lock);
    }
    return NULL;
}

/* Lazy-Erzeugung des Pools. Liefert NULL bei Fehler (Aufrufer faellt dann auf
 * synchrones Meshing zurueck). */
static MooVoxelJobPool* moo_voxel_pool_get(MooVoxelWorld* w) {
    if (w->jobs) return (MooVoxelJobPool*)w->jobs;
    MooVoxelJobPool* pool = (MooVoxelJobPool*)moo_alloc(sizeof(MooVoxelJobPool));
    if (!pool) return NULL;
    memset(pool, 0, sizeof(*pool));
    pool->world = w;
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    int n = (ncpu > 1) ? (int)(ncpu - 1) : 1;
    if (n > MOO_VOXEL_MAX_WORKERS) n = MOO_VOXEL_MAX_WORKERS;
    if (n < 1) n = 1;
    if (pthread_mutex_init(&pool->lock, NULL) != 0) { moo_free(pool); return NULL; }
    if (pthread_cond_init(&pool->has_work, NULL) != 0) {
        pthread_mutex_destroy(&pool->lock); moo_free(pool); return NULL;
    }
    if (pthread_cond_init(&pool->done, NULL) != 0) {
        pthread_cond_destroy(&pool->has_work);
        pthread_mutex_destroy(&pool->lock); moo_free(pool); return NULL;
    }
    pool->shutdown = false;
    pool->n_threads = 0;
    for (int i = 0; i < n; i++) {
        if (pthread_create(&pool->threads[i], NULL, moo_voxel_worker, pool) != 0) break;
        pool->n_threads++;
    }
    if (pool->n_threads == 0) {
        pthread_cond_destroy(&pool->done);
        pthread_cond_destroy(&pool->has_work);
        pthread_mutex_destroy(&pool->lock);
        moo_free(pool);
        return NULL;
    }
    w->jobs = pool;
    return pool;
}

/* Faehrt den Pool herunter und gibt ihn frei (im Main-Thread, aus moo_voxel_free). */
static void moo_voxel_pool_destroy(MooVoxelWorld* w) {
    if (!w->jobs) return;
    MooVoxelJobPool* pool = (MooVoxelJobPool*)w->jobs;
    pthread_mutex_lock(&pool->lock);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->has_work);
    pthread_mutex_unlock(&pool->lock);
    for (int i = 0; i < pool->n_threads; i++) pthread_join(pool->threads[i], NULL);
    pthread_cond_destroy(&pool->done);
    pthread_cond_destroy(&pool->has_work);
    pthread_mutex_destroy(&pool->lock);
    moo_free(pool);
    w->jobs = NULL;
}
#endif /* !_WIN32 */

/* ========================================================================
 * Mesher (Plan-005 Phase 1b)
 *
 * Strategie: NAIV pro Voxel, nur sichtbare Faces. Fuer jeden festen Block
 * werden die 6 Achsen-Nachbarn geprueft; eine Face wird nur dann emittiert,
 * wenn der Nachbar Luft (0) ist. Nachbar-Culling laeuft ueber die Chunk-Grenze:
 * liegt der Nachbar ausserhalb [0,DIM), wird im ANGRENZENDEN Chunk derselben
 * Welt nachgeschaut (existiert er nicht oder ist leer -> Luft -> Face sichtbar).
 * KEIN Greedy-Meshing, KEIN Vertex-AO (Phase 3). Output ueber die 3D-Chunk-API
 * (begin -> triangle... -> end) als GENAU EIN Render-Chunk pro Voxel-Chunk.
 * ======================================================================== */

/* Block-Farben (Phase-1-Registry, Wasser opak). Index = Block-ID. Index 0
 * (Luft) wird nie gerendert. */
static const char* const MOO_VOXEL_BLOCK_HEX[MOO_VOXEL_MAX_BLOCK_ID + 1] = {
    "#000000", /* 0 luft  (ungenutzt) */
    "#4CAF50", /* 1 gras  */
    "#795548", /* 2 erde  */
    "#9E9E9E", /* 3 stein */
    "#FBC02D", /* 4 sand  */
    "#2196F3", /* 5 wasser (opak in Phase 1) */
};

/* Holt die Block-ID an Welt-Voxel (wx,wy,wz). Eigener Chunk wird direkt
 * uebergeben (Fast-Path fuer Nachbarn innerhalb des Chunks); fuer Nachbarn
 * jenseits der Chunk-Grenze wird der angrenzende Chunk in w nachgeschlagen.
 * Nicht-allokierter / leerer Chunk = Luft (0). */
static uint16_t moo_voxel_block_at(MooVoxelWorld* w, const MooVoxelChunk* self,
                                   int32_t self_cx, int32_t self_cy, int32_t self_cz,
                                   int32_t lx, int32_t ly, int32_t lz) {
    const MooVoxelChunk* ch = self;
    int32_t ix = lx, iy = ly, iz = lz;
    if (lx < 0 || lx >= MOO_VOXEL_CHUNK_DIM ||
        ly < 0 || ly >= MOO_VOXEL_CHUNK_DIM ||
        lz < 0 || lz >= MOO_VOXEL_CHUNK_DIM) {
        /* Ueber die Chunk-Grenze: in den Nachbar-Chunk umrechnen. */
        int32_t ncx = self_cx + moo_voxel_floordiv(lx, MOO_VOXEL_CHUNK_DIM);
        int32_t ncy = self_cy + moo_voxel_floordiv(ly, MOO_VOXEL_CHUNK_DIM);
        int32_t ncz = self_cz + moo_voxel_floordiv(lz, MOO_VOXEL_CHUNK_DIM);
        ch = moo_voxel_lookup(w, ncx, ncy, ncz);
        if (!ch || moo_voxel_chunk_is_empty(ch)) return 0; /* leerer/fehlender Nachbar = Luft */
        ix = moo_voxel_floormod(lx, MOO_VOXEL_CHUNK_DIM);
        iy = moo_voxel_floormod(ly, MOO_VOXEL_CHUNK_DIM);
        iz = moo_voxel_floormod(lz, MOO_VOXEL_CHUNK_DIM);
    }
    if (moo_voxel_chunk_is_empty(ch)) return 0;
    return moo_voxel_chunk_block_get(ch, moo_voxel_local_index(ix, iy, iz));
}

/* Emittiert ein achsenparalleles Voxel-Face (Einheitsquadrat) als 2 Dreiecke.
 * (x,y,z) ist die Min-Ecke des Voxels in Weltkoordinaten; die Faces sitzen an
 * der jeweiligen Voxel-Seite. Wicklung CCW von aussen gesehen (Front-Face).
 * win wird vom Backend ignoriert (zeichnet in den aktiven chunk_begin-Kontext);
 * wir uebergeben moo_none(). */
static void moo_voxel_emit_face(int dir, float x, float y, float z, MooValue color) {
    MooValue w = moo_none();
    float x0 = x, x1 = x + 1.0f;
    float y0 = y, y1 = y + 1.0f;
    float z0 = z, z1 = z + 1.0f;
    switch (dir) {
        case 0: /* +X (rechts) */
            moo_3d_triangle(w,
                moo_number(x1), moo_number(y0), moo_number(z0),
                moo_number(x1), moo_number(y1), moo_number(z0),
                moo_number(x1), moo_number(y1), moo_number(z1), color);
            moo_3d_triangle(w,
                moo_number(x1), moo_number(y0), moo_number(z0),
                moo_number(x1), moo_number(y1), moo_number(z1),
                moo_number(x1), moo_number(y0), moo_number(z1), color);
            break;
        case 1: /* -X (links) */
            moo_3d_triangle(w,
                moo_number(x0), moo_number(y0), moo_number(z0),
                moo_number(x0), moo_number(y1), moo_number(z1),
                moo_number(x0), moo_number(y1), moo_number(z0), color);
            moo_3d_triangle(w,
                moo_number(x0), moo_number(y0), moo_number(z0),
                moo_number(x0), moo_number(y0), moo_number(z1),
                moo_number(x0), moo_number(y1), moo_number(z1), color);
            break;
        case 2: /* +Y (oben) */
            moo_3d_triangle(w,
                moo_number(x0), moo_number(y1), moo_number(z0),
                moo_number(x0), moo_number(y1), moo_number(z1),
                moo_number(x1), moo_number(y1), moo_number(z1), color);
            moo_3d_triangle(w,
                moo_number(x0), moo_number(y1), moo_number(z0),
                moo_number(x1), moo_number(y1), moo_number(z1),
                moo_number(x1), moo_number(y1), moo_number(z0), color);
            break;
        case 3: /* -Y (unten) */
            moo_3d_triangle(w,
                moo_number(x0), moo_number(y0), moo_number(z0),
                moo_number(x1), moo_number(y0), moo_number(z1),
                moo_number(x0), moo_number(y0), moo_number(z1), color);
            moo_3d_triangle(w,
                moo_number(x0), moo_number(y0), moo_number(z0),
                moo_number(x1), moo_number(y0), moo_number(z0),
                moo_number(x1), moo_number(y0), moo_number(z1), color);
            break;
        case 4: /* +Z (vorne) */
            moo_3d_triangle(w,
                moo_number(x0), moo_number(y0), moo_number(z1),
                moo_number(x1), moo_number(y1), moo_number(z1),
                moo_number(x0), moo_number(y1), moo_number(z1), color);
            moo_3d_triangle(w,
                moo_number(x0), moo_number(y0), moo_number(z1),
                moo_number(x1), moo_number(y0), moo_number(z1),
                moo_number(x1), moo_number(y1), moo_number(z1), color);
            break;
        case 5: /* -Z (hinten) */
            moo_3d_triangle(w,
                moo_number(x0), moo_number(y0), moo_number(z0),
                moo_number(x0), moo_number(y1), moo_number(z0),
                moo_number(x1), moo_number(y1), moo_number(z0), color);
            moo_3d_triangle(w,
                moo_number(x0), moo_number(y0), moo_number(z0),
                moo_number(x1), moo_number(y1), moo_number(z0),
                moo_number(x1), moo_number(y0), moo_number(z0), color);
            break;
        default: break;
    }
}

/* Baut die Render-Geometrie EINES Chunks neu. Erwartet einen belegten Slot mit
 * blocks != NULL. Setzt voraus, dass ein Backend aktiv ist (Aufrufer prueft).
 * Verwendet/erzeugt die Render-ID des Chunks und befuellt sie via begin/end. */
static void moo_voxel_remesh_chunk(MooVoxelWorld* w, MooVoxelChunk* ch) {
    /* Render-ID anlegen falls noch keine vorhanden. moo_3d_chunk_create wirft
     * nur OHNE Backend; der Aufrufer hat backend_active bereits geprueft. */
    if (ch->render_id < 0) {
        MooValue rid = moo_3d_chunk_create();
        int32_t id = (int32_t)moo_as_number(rid);
        if (id < 0) {
            /* Backend konnte keinen Slot vergeben (z.B. Cache voll) -> kein
             * Crash, Chunk bleibt ungemesht aber dirty bleibt geloescht, damit
             * wir nicht in einer Endlosschleife remeshen. */
            ch->render_id = -1;
            return;
        }
        ch->render_id = id;
    }

    int32_t base_x = ch->cx * MOO_VOXEL_CHUNK_DIM;
    int32_t base_y = ch->cy * MOO_VOXEL_CHUNK_DIM;
    int32_t base_z = ch->cz * MOO_VOXEL_CHUNK_DIM;

    /* Nachbar-Offsets passend zu dir 0..5 (+X,-X,+Y,-Y,+Z,-Z). */
    static const int32_t NX[6] = { 1, -1, 0, 0, 0, 0 };
    static const int32_t NY[6] = { 0, 0, 1, -1, 0, 0 };
    static const int32_t NZ[6] = { 0, 0, 0, 0, 1, -1 };

    moo_3d_chunk_begin(moo_number((double)ch->render_id));
    for (int32_t lz = 0; lz < MOO_VOXEL_CHUNK_DIM; lz++) {
        for (int32_t ly = 0; ly < MOO_VOXEL_CHUNK_DIM; ly++) {
            for (int32_t lx = 0; lx < MOO_VOXEL_CHUNK_DIM; lx++) {
                uint16_t id = moo_voxel_chunk_block_get(ch, moo_voxel_local_index(lx, ly, lz));
                if (id == 0) continue; /* Luft */
                int cid = (id <= MOO_VOXEL_MAX_BLOCK_ID) ? (int)id : 0;
                if (cid == 0) continue; /* unbekannte ID defensiv ueberspringen */
                MooValue color = moo_string_new(MOO_VOXEL_BLOCK_HEX[cid]);
                float wx = (float)(base_x + lx);
                float wy = (float)(base_y + ly);
                float wz = (float)(base_z + lz);
                for (int dir = 0; dir < 6; dir++) {
                    uint16_t nb = moo_voxel_block_at(w, ch, ch->cx, ch->cy, ch->cz,
                                                     lx + NX[dir], ly + NY[dir], lz + NZ[dir]);
                    if (nb == 0) {
                        moo_voxel_emit_face(dir, wx, wy, wz, color);
                    }
                }
            }
        }
    }
    moo_3d_chunk_end();
    ch->dirty = false;
    ch->dirty_ao = false; /* P006-R4: voller Remesh -> auch AO-Flag konsumiert */
}

MooValue moo_voxel_mesh_bauen(MooValue welt, MooValue x, MooValue y, MooValue z) {
    MooVoxelWorld* vw = moo_voxel_check(welt, "voxel_mesh_bauen");
    if (!vw) return moo_none();

    int32_t cx = moo_voxel_coord(x);
    int32_t cy = moo_voxel_coord(y);
    int32_t cz = moo_voxel_coord(z);

    MooVoxelChunk* ch = moo_voxel_lookup(vw, cx, cy, cz);
    if (!ch) {
        /* Nicht geladener Chunk -> keine Geometrie, keine Render-ID. */
        return moo_number(-1.0);
    }

    /* Ohne aktives Backend kann/soll kein GPU-Cache angelegt werden
     * (GPU-Cache-Lifetime, Risiko 10). CPU bleibt konsistent; dirty bleibt
     * stehen, damit ein spaeterer Aufruf mit Backend nachmesht. */
    if (!moo_3d_backend_active()) {
        return moo_number(-1.0);
    }

    /* Chunk ohne Bloecke (komplett Luft): falls eine alte Render-ID existiert,
     * GENAU EINMAL freigeben (Chunk wurde geleert) und -1 zurueckgeben. */
    if (moo_voxel_chunk_is_empty(ch)) {
        if (ch->render_id >= 0) {
            moo_3d_chunk_delete(moo_number((double)ch->render_id));
            ch->render_id = -1;
        }
        ch->dirty = false;
        ch->dirty_ao = false; /* P006-R4 */
        return moo_number(-1.0);
    }

    moo_voxel_remesh_chunk(vw, ch);
    return moo_number((double)ch->render_id);
}

/* Synchroner Fallback: greedy+AO im Main-Thread (kein Pool). Genutzt auf
 * _WIN32 und wenn der Pool nicht erzeugt werden konnte. Liefert remesht-Count. */
static int64_t moo_voxel_aktualisieren_sync(MooVoxelWorld* vw) {
    int64_t remeshed = 0;
    for (int32_t i = 0; i < vw->chunk_cap; i++) {
        MooVoxelChunk* ch = &vw->chunks[i];
        /* P006-R4: dirty (Face/Inhalt) ODER dirty_ao (nur Boundary-AO stale)
         * loesen beide einen vollen Greedy+AO-Remesh aus. Der Mesher liest die
         * Diagonalen via pad_fill -> der AO-only-Pfad stellt korrekte AO her. */
        if (!ch->occupied || (!ch->dirty && !ch->dirty_ao)) continue;
        if (moo_voxel_chunk_is_empty(ch)) {
            if (ch->render_id >= 0) {
                moo_3d_chunk_delete(moo_number((double)ch->render_id));
                ch->render_id = -1;
            }
            ch->dirty = false;
            ch->dirty_ao = false;
            continue;
        }
        MooVoxelMeshBuf buf; mesh_buf_init(&buf);
        if (moo_voxel_build_mesh_cpu(vw, ch, &buf)) {
            moo_voxel_upload_mesh(ch, &buf);  /* loescht ch->dirty */
            ch->dirty_ao = false;
        } else {
            ch->dirty = false;     /* OOM: nicht endlos retryen */
            ch->dirty_ao = false;
        }
        mesh_buf_free(&buf);
        remeshed++;
    }
    return remeshed;
}

/* Sammelt fertige CPU-Buffer ein und lädt sie hoch (Main-Thread). slots[] sind
 * die Chunk-Slots in derselben Reihenfolge wie die Jobs. */
MooValue moo_voxel_aktualisieren(MooValue welt) {
    MooVoxelWorld* vw = moo_voxel_check(welt, "voxel_aktualisieren");
    if (!vw) return moo_none();

    /* Ohne Backend nichts tun (dirty bleibt fuer spaeter erhalten). */
    if (!moo_3d_backend_active()) {
        return moo_number(0.0);
    }

    /* --------------------------------------------------------------------
     * P006-R3 — Lazy Section-Downgrade (main-thread-only, K5-Invariante).
     *
     * GENAU HIER ist der einzige strukturell sichere Punkt fuer die Layout-
     * Mutation: wir sind im Main-Thread, der Threadpool des VORHERIGEN Frames
     * wurde am Ende des vorherigen aktualisieren-Aufrufs vollstaendig gejoint
     * (pool->active==0), und der Pool dieses Frames wird erst WEITER UNTEN nach
     * dieser Schleife per pthread_cond_broadcast geweckt. Zwischen Downgrade und
     * Job-Submission mutiert die Welt nicht. Damit liest NIE ein Worker eine
     * Section, die gerade von moo_voxel_section_downgrade umgebaut wird (Review-
     * Auflage K5). Downgrade laeuft nur fuer DIRTY Chunks — saubere Chunks haben
     * sich seit dem letzten Mesh nicht geaendert, ihr Layout ist bereits stabil.
     *
     * Der Downgrade ist lese-resultat-neutral (reine Repraesentations-Umstellung)
     * und nicht teuer pro voxel_setzen (er scannt nur einmal pro Remesh-Zyklus
     * die ohnehin neu zu meshenden Chunks, Plan-006-Risiko 4 vermieden). Wird ein
     * Chunk dabei komplett leer, kollabiert sein Section-Array auf NULL — die
     * nachfolgende Leerraum-Aufraeumschleife gibt dann seinen GPU-Cache frei. */
    for (int32_t i = 0; i < vw->chunk_cap; i++) {
        MooVoxelChunk* ch = &vw->chunks[i];
        if (!ch->occupied || !ch->dirty || !ch->sections) continue;
        moo_voxel_chunk_optimize(ch);
    }


    /* Geleerte Chunks (indices==NULL) zuerst seriell aufraeumen: alte Render-ID
     * freigeben, dirty loeschen. Diese gehen NICHT in die Worker-Queue (kein
     * Mesh zu bauen). P006-R4: auch dirty_ao-only-Eintraege hier abraeumen — ein
     * leerer Chunk hat keine Boundary-AO-Geometrie, das Flag waere sonst ein
     * Karteileiche (nie gezaehlt, nie geloescht). */
    for (int32_t i = 0; i < vw->chunk_cap; i++) {
        MooVoxelChunk* ch = &vw->chunks[i];
        if (!ch->occupied || (!ch->dirty && !ch->dirty_ao) || !moo_voxel_chunk_is_empty(ch)) continue;
        if (ch->render_id >= 0) {
            moo_3d_chunk_delete(moo_number((double)ch->render_id));
            ch->render_id = -1;
        }
        ch->dirty = false;
        ch->dirty_ao = false;
    }

#ifdef _WIN32
    /* POSIX-only Threadpool nicht verfuegbar -> synchron meshen (s. Datei-Header). */
    return moo_number((double)moo_voxel_aktualisieren_sync(vw));
#else
    /* Remesh-beduerftige (dirty ODER dirty_ao), nicht-leere Chunks zaehlen.
     * P006-R4: ein dirty_ao-only Chunk wurde inhaltlich nicht geaendert, braucht
     * aber wegen stale Boundary-AO einen vollen Greedy+AO-Remesh — dieselbe
     * Worker-Pipeline, dieselbe Behandlung wie dirty. */
    int n_dirty = 0;
    for (int32_t i = 0; i < vw->chunk_cap; i++) {
        MooVoxelChunk* ch = &vw->chunks[i];
        if (ch->occupied && (ch->dirty || ch->dirty_ao) && !moo_voxel_chunk_is_empty(ch)) n_dirty++;
    }
    if (n_dirty == 0) return moo_number(0.0);

    /* Async lohnt sich erst ab mehreren Chunks: das Aufwecken+Joinen der Worker
     * kostet pro Batch ~einige ms Scheduler-Latenz. Bei genau einem dirty Chunk
     * (haeufigster Fall: ein einzelner Block-Edit) ist synchrones Greedy+AO-
     * Meshing messbar schneller (kein Thread-Roundtrip). Schwelle = 2. */
    if (n_dirty < 2) {
        return moo_number((double)moo_voxel_aktualisieren_sync(vw));
    }

    MooVoxelJobPool* pool = moo_voxel_pool_get(vw);
    if (!pool) {
        /* Pool-Erzeugung fehlgeschlagen -> sauberer synchroner Fallback. */
        return moo_number((double)moo_voxel_aktualisieren_sync(vw));
    }

    MooVoxelJob* jobs = (MooVoxelJob*)moo_alloc((size_t)n_dirty * sizeof(MooVoxelJob));
    if (!jobs) {
        return moo_number((double)moo_voxel_aktualisieren_sync(vw));
    }
    int k = 0;
    for (int32_t i = 0; i < vw->chunk_cap; i++) {
        MooVoxelChunk* ch = &vw->chunks[i];
        if (ch->occupied && (ch->dirty || ch->dirty_ao) && !moo_voxel_chunk_is_empty(ch)) jobs[k++].slot = i;
    }

    /* Batch an die Worker uebergeben. Welt wird zwischen hier und dem Join NICHT
     * mutiert -> Worker lesen eine stabile Hashmap (Race-Modell, s. Header). */
    pthread_mutex_lock(&pool->lock);
    pool->jobs = jobs;
    pool->n_jobs = n_dirty;
    pool->next_job = 0;
    pool->active = 0;
    pthread_cond_broadcast(&pool->has_work);
    while (pool->next_job < pool->n_jobs || pool->active > 0)
        pthread_cond_wait(&pool->done, &pool->lock);
    pool->jobs = NULL;
    pool->n_jobs = 0;
    pthread_mutex_unlock(&pool->lock);

    /* Fertige Buffer im Main-Thread hochladen (GPU nur Main-Thread, Risiko 8). */
    int64_t remeshed = 0;
    for (int j = 0; j < n_dirty; j++) {
        MooVoxelChunk* ch = &vw->chunks[jobs[j].slot];
        if (jobs[j].ok) {
            moo_voxel_upload_mesh(ch, &jobs[j].result);  /* loescht ch->dirty */
            ch->dirty_ao = false;                        /* P006-R4: AO-Flag mit-loeschen */
        } else {
            ch->dirty = false;     /* OOM im Worker: nicht endlos retryen */
            ch->dirty_ao = false;
        }
        mesh_buf_free(&jobs[j].result);
        remeshed++;
    }
    moo_free(jobs);
    return moo_number((double)remeshed);
#endif
}

/* ========================================================================
 * Phase 1d: Zentraler World-Koordinaten-Block-Accessor
 *
 * EINZIGER Lese-Zugriffspunkt auf Blockdaten ueber Welt-Voxel-Koordinaten.
 * Raycast (DDA) und AABB-Overlap gehen ausschliesslich hierueber, statt
 * blocks[] verstreut selbst zu indexieren. Phase 1c (RT3) stellt das
 * Chunk-Datenlayout auf Palette/Bitpacking um und muss dann NUR diese eine
 * Funktion (plus moo_voxel_holen / moo_voxel_block_at) anpassen.
 *
 * Liefert die Block-ID am Welt-Voxel (wx,wy,wz); nicht-allokierter / leerer
 * Chunk = 0 (Luft). Reiner Lookup ohne Allokation.
 * ======================================================================== */
static inline uint16_t moo_voxel_world_block(MooVoxelWorld* w,
                                             int32_t wx, int32_t wy, int32_t wz) {
    int32_t cx = moo_voxel_floordiv(wx, MOO_VOXEL_CHUNK_DIM);
    int32_t cy = moo_voxel_floordiv(wy, MOO_VOXEL_CHUNK_DIM);
    int32_t cz = moo_voxel_floordiv(wz, MOO_VOXEL_CHUNK_DIM);
    MooVoxelChunk* ch = moo_voxel_lookup(w, cx, cy, cz);
    if (!ch || moo_voxel_chunk_is_empty(ch)) return 0; /* leerer/fehlender Chunk = Luft */
    int32_t lx = moo_voxel_floormod(wx, MOO_VOXEL_CHUNK_DIM);
    int32_t ly = moo_voxel_floormod(wy, MOO_VOXEL_CHUNK_DIM);
    int32_t lz = moo_voxel_floormod(wz, MOO_VOXEL_CHUNK_DIM);
    return moo_voxel_chunk_block_get(ch, moo_voxel_local_index(lx, ly, lz));
}

/* ========================================================================
 * Phase 1d: DDA-Raycast (Amanatides & Woo, 1987)
 *
 * "A Fast Voxel Traversal Algorithm for Ray Tracing". Der Strahl startet bei
 * (ox,oy,oz) in Welt-Koordinaten (Float, NICHT Voxel-Index) mit Richtung
 * (dx,dy,dz). Wir traversieren das Voxel-Gitter Zelle fuer Zelle ohne
 * Ueberspringen, bis ein solider Block (id != 0) getroffen wird oder die
 * zurueckgelegte Distanz max_dist ueberschreitet.
 *
 * Rueckgabe-Dict (P0.5-Contract): { hit, x, y, z, nx, ny, nz, id, dist }.
 *   - hit  : Bool. true = solider Block getroffen.
 *   - x,y,z: Number. Voxel-Koordinate des getroffenen Blocks (signed int, auch
 *            negativ). Bei Miss 0/0/0.
 *   - nx,ny,nz: Number. Face-Normale der EINSTIEGSSEITE (Einheitsvektor entlang
 *            einer Achse, z.B. (0,1,0) wenn der Strahl von oben einschlaegt).
 *            Bei Miss 0/0/0. Bei Start-im-Block (dist=0) ebenfalls 0/0/0
 *            (keine Einstiegsseite definierbar).
 *   - id   : Number. Block-ID des Treffers (1..MAX). Bei Miss 0.
 *   - dist : Number. Euklidische Distanz vom Ursprung bis zur getroffenen
 *            Face-Ebene (NICHT bis Zellmitte). Bei Start-im-Block 0.
 *
 * Fehlerpolitik (KEINE HACKS):
 *   - Invalider Handle -> moo_throw (ueber moo_voxel_check).
 *   - Richtungsvektor (0,0,0) oder nicht-endlich -> moo_throw (kein stilles
 *     Endlos-/Null-Verhalten).
 *   - max_dist <= 0 -> sofort hit=false (leerer Strahl).
 * ======================================================================== */

MooValue moo_voxel_strahl(MooValue welt,
                          MooValue ox, MooValue oy, MooValue oz,
                          MooValue dx, MooValue dy, MooValue dz,
                          MooValue max_dist) {
    MooVoxelWorld* vw = moo_voxel_check(welt, "voxel_strahl");
    if (!vw) return moo_none();

    double rox = moo_as_number(ox);
    double roy = moo_as_number(oy);
    double roz = moo_as_number(oz);
    double rdx = moo_as_number(dx);
    double rdy = moo_as_number(dy);
    double rdz = moo_as_number(dz);
    double maxd = moo_as_number(max_dist);

    /* Richtung normalisieren; Null-/nicht-endlicher Vektor ist ein Fehler. */
    double len = sqrt(rdx * rdx + rdy * rdy + rdz * rdz);
    if (!(len > 0.0) || !isfinite(len)) {
        moo_throw(moo_error("Voxel-Fehler in voxel_strahl: Richtungsvektor ist (0,0,0) oder ungueltig"));
        return moo_none();
    }
    rdx /= len; rdy /= len; rdz /= len;

    MooValue d = moo_dict_new();

    /* Hilfsmakro-frei: Miss-Dict zentral bauen. */
    if (!(maxd > 0.0)) {
        moo_dict_set(d, moo_string_new("hit"),  moo_bool(false));
        moo_dict_set(d, moo_string_new("x"),    moo_number(0.0));
        moo_dict_set(d, moo_string_new("y"),    moo_number(0.0));
        moo_dict_set(d, moo_string_new("z"),    moo_number(0.0));
        moo_dict_set(d, moo_string_new("nx"),   moo_number(0.0));
        moo_dict_set(d, moo_string_new("ny"),   moo_number(0.0));
        moo_dict_set(d, moo_string_new("nz"),   moo_number(0.0));
        moo_dict_set(d, moo_string_new("id"),   moo_number(0.0));
        moo_dict_set(d, moo_string_new("dist"), moo_number(0.0));
        return d;
    }

    /* Startzelle (Voxel, in dem der Ursprung liegt) per floor-Cast. */
    int32_t vx = (int32_t)floor(rox);
    int32_t vy = (int32_t)floor(roy);
    int32_t vz = (int32_t)floor(roz);

    /* Schrittrichtung pro Achse (-1 / 0 / +1). 0 = Strahl laeuft parallel zur
     * Achse, diese Achse loest nie eine Grenzueberschreitung aus. */
    int32_t step_x = (rdx > 0.0) ? 1 : (rdx < 0.0 ? -1 : 0);
    int32_t step_y = (rdy > 0.0) ? 1 : (rdy < 0.0 ? -1 : 0);
    int32_t step_z = (rdz > 0.0) ? 1 : (rdz < 0.0 ? -1 : 0);

    /* tDelta: Parameter-Distanz zwischen zwei Gitterebenen je Achse.
     * tMax:   Parameter-Distanz bis zur naechsten Gitterebene je Achse.
     * Achsen mit step==0 bekommen INFINITY, damit sie nie als Minimum gewinnen. */
    const double INF = (double)INFINITY;
    double t_delta_x = (step_x != 0) ? fabs(1.0 / rdx) : INF;
    double t_delta_y = (step_y != 0) ? fabs(1.0 / rdy) : INF;
    double t_delta_z = (step_z != 0) ? fabs(1.0 / rdz) : INF;

    double t_max_x, t_max_y, t_max_z;
    if (step_x > 0)      t_max_x = ((double)(vx + 1) - rox) / rdx;
    else if (step_x < 0) t_max_x = (rox - (double)vx) / -rdx;
    else                 t_max_x = INF;
    if (step_y > 0)      t_max_y = ((double)(vy + 1) - roy) / rdy;
    else if (step_y < 0) t_max_y = (roy - (double)vy) / -rdy;
    else                 t_max_y = INF;
    if (step_z > 0)      t_max_z = ((double)(vz + 1) - roz) / rdz;
    else if (step_z < 0) t_max_z = (roz - (double)vz) / -rdz;
    else                 t_max_z = INF;

    /* Face-Normale der Einstiegsseite (wird bei jedem Schritt aktualisiert). */
    int32_t nx = 0, ny = 0, nz = 0;
    double t_hit = 0.0;          /* Parameter-Distanz an der Einstiegs-Face */
    bool hit = false;
    uint16_t hit_id = 0;

    /* Sonderfall: Ursprung liegt bereits in einem soliden Block. Treffer mit
     * dist=0 und undefinierter Einstiegsseite (Normale 0,0,0). */
    uint16_t start_block = moo_voxel_world_block(vw, vx, vy, vz);
    if (start_block != 0) {
        hit = true;
        hit_id = start_block;
        t_hit = 0.0;
        /* nx/ny/nz bleiben 0 (keine Einstiegsseite). */
    } else {
        /* DDA-Hauptschleife: immer die Achse mit kleinstem tMax voranschreiten.
         * Abbruch sobald die Einstiegsdistanz max_dist ueberschreitet. */
        for (;;) {
            if (t_max_x < t_max_y) {
                if (t_max_x < t_max_z) {
                    vx += step_x;
                    t_hit = t_max_x;
                    t_max_x += t_delta_x;
                    nx = -step_x; ny = 0; nz = 0;
                } else {
                    vz += step_z;
                    t_hit = t_max_z;
                    t_max_z += t_delta_z;
                    nx = 0; ny = 0; nz = -step_z;
                }
            } else {
                if (t_max_y < t_max_z) {
                    vy += step_y;
                    t_hit = t_max_y;
                    t_max_y += t_delta_y;
                    nx = 0; ny = -step_y; nz = 0;
                } else {
                    vz += step_z;
                    t_hit = t_max_z;
                    t_max_z += t_delta_z;
                    nx = 0; ny = 0; nz = -step_z;
                }
            }

            if (t_hit > maxd) break; /* ueber Reichweite -> Miss */

            uint16_t b = moo_voxel_world_block(vw, vx, vy, vz);
            if (b != 0) {
                hit = true;
                hit_id = b;
                break;
            }
        }
    }

    if (hit) {
        moo_dict_set(d, moo_string_new("hit"),  moo_bool(true));
        moo_dict_set(d, moo_string_new("x"),    moo_number((double)vx));
        moo_dict_set(d, moo_string_new("y"),    moo_number((double)vy));
        moo_dict_set(d, moo_string_new("z"),    moo_number((double)vz));
        moo_dict_set(d, moo_string_new("nx"),   moo_number((double)nx));
        moo_dict_set(d, moo_string_new("ny"),   moo_number((double)ny));
        moo_dict_set(d, moo_string_new("nz"),   moo_number((double)nz));
        moo_dict_set(d, moo_string_new("id"),   moo_number((double)hit_id));
        moo_dict_set(d, moo_string_new("dist"), moo_number(t_hit));
    } else {
        moo_dict_set(d, moo_string_new("hit"),  moo_bool(false));
        moo_dict_set(d, moo_string_new("x"),    moo_number(0.0));
        moo_dict_set(d, moo_string_new("y"),    moo_number(0.0));
        moo_dict_set(d, moo_string_new("z"),    moo_number(0.0));
        moo_dict_set(d, moo_string_new("nx"),   moo_number(0.0));
        moo_dict_set(d, moo_string_new("ny"),   moo_number(0.0));
        moo_dict_set(d, moo_string_new("nz"),   moo_number(0.0));
        moo_dict_set(d, moo_string_new("id"),   moo_number(0.0));
        moo_dict_set(d, moo_string_new("dist"), moo_number(0.0));
    }
    return d;
}

/* ========================================================================
 * Phase 1d: AABB-Overlap (Box gegen solide Bloecke)
 *
 * Entscheidung (am P0.5-Dict-Stil orientiert, dokumentiert): Statt nur eines
 * Bool liefern wir ein Dict { hit, count, x, y, z } — symmetrisch zu
 * voxel_strahl und nuetzlicher fuer Gameplay (Kollisions-Aufloesung braucht oft
 * mehr als ja/nein):
 *   - hit  : Bool. true = mindestens ein solider Block ueberlappt die Box.
 *   - count: Number. Anzahl solider Voxel-Zellen, die die Box ueberlappen.
 *   - x,y,z: Number. Voxel-Koordinate des ERSTEN soliden Treffers (kleinster
 *            (z,y,x)-Index in Scan-Reihenfolge); bei Miss 0/0/0.
 *
 * Die Box ist achsenparallel und in Welt-Koordinaten (Float) gegeben:
 * [minx,maxx] x [miny,maxy] x [minz,maxz]. Wir testen jede Voxel-Zelle, deren
 * Einheitswuerfel [v, v+1) die Box schneidet. Eine Box mit max == min (Punkt)
 * deckt genau die Zelle floor(min) ab. min/max werden bei Bedarf vertauscht,
 * damit die Reihenfolge der Argumente egal ist (kein Wurf).
 *
 * Voxel-Bereich pro Achse:
 *   lo = floor(min)
 *   hi = ceil(max) - 1, mindestens lo (eine degenerierte/duenne Box deckt >=1 Zelle).
 * Damit ueberlappt Zelle v die Box, wenn v+1 > min und v < max.
 *
 * Fehlerpolitik: invalider Handle -> moo_throw. Nicht-endliche Grenzen ->
 * moo_throw (keine stille NaN-Schleife).
 * ======================================================================== */

MooValue moo_voxel_aabb(MooValue welt,
                        MooValue minx, MooValue miny, MooValue minz,
                        MooValue maxx, MooValue maxy, MooValue maxz) {
    MooVoxelWorld* vw = moo_voxel_check(welt, "voxel_aabb");
    if (!vw) return moo_none();

    double ax0 = moo_as_number(minx), ax1 = moo_as_number(maxx);
    double ay0 = moo_as_number(miny), ay1 = moo_as_number(maxy);
    double az0 = moo_as_number(minz), az1 = moo_as_number(maxz);

    if (!isfinite(ax0) || !isfinite(ax1) ||
        !isfinite(ay0) || !isfinite(ay1) ||
        !isfinite(az0) || !isfinite(az1)) {
        moo_throw(moo_error("Voxel-Fehler in voxel_aabb: Box-Grenze ist nicht endlich"));
        return moo_none();
    }

    /* min/max je Achse normalisieren (Argument-Reihenfolge egal). */
    if (ax1 < ax0) { double t = ax0; ax0 = ax1; ax1 = t; }
    if (ay1 < ay0) { double t = ay0; ay0 = ay1; ay1 = t; }
    if (az1 < az0) { double t = az0; az0 = az1; az1 = t; }

    int32_t lox = (int32_t)floor(ax0);
    int32_t loy = (int32_t)floor(ay0);
    int32_t loz = (int32_t)floor(az0);
    int32_t hix = (int32_t)ceil(ax1) - 1;
    int32_t hiy = (int32_t)ceil(ay1) - 1;
    int32_t hiz = (int32_t)ceil(az1) - 1;
    if (hix < lox) hix = lox;
    if (hiy < loy) hiy = loy;
    if (hiz < loz) hiz = loz;

    int64_t count = 0;
    bool hit = false;
    int32_t fx = 0, fy = 0, fz = 0; /* erste Treffer-Zelle */

    for (int32_t z = loz; z <= hiz; z++) {
        for (int32_t y = loy; y <= hiy; y++) {
            for (int32_t x = lox; x <= hix; x++) {
                if (moo_voxel_world_block(vw, x, y, z) != 0) {
                    if (!hit) {
                        hit = true;
                        fx = x; fy = y; fz = z;
                    }
                    count++;
                }
            }
        }
    }

    MooValue d = moo_dict_new();
    moo_dict_set(d, moo_string_new("hit"),   moo_bool(hit));
    moo_dict_set(d, moo_string_new("count"), moo_number((double)count));
    moo_dict_set(d, moo_string_new("x"),     moo_number((double)fx));
    moo_dict_set(d, moo_string_new("y"),     moo_number((double)fy));
    moo_dict_set(d, moo_string_new("z"),     moo_number((double)fz));
    return d;
}

/* ========================================================================
 * Freigabe (von moo_release ueber MOO_VOXELWORLD aufgerufen, refcount==0)
 *
 * Gibt ALLE CPU-Allokationen frei (Chunk-Blocks, Hashmap-Tabelle, World).
 * GPU-Render-Cache existiert in Phase 1a noch nicht; die spaetere Trennung
 * (1b/RT2) gibt CPU hier IMMER frei und ruft moo_3d_chunk_delete nur bei
 * aktivem Backend (sonst safe no-op) -> World darf Backend ueberleben.
 * ======================================================================== */

void moo_voxel_free(void* ptr) {
    if (!ptr) return;
    MooVoxelWorld* vw = (MooVoxelWorld*)ptr;
    /* Threadpool zuerst herunterfahren (Worker joinen) — danach kann kein Worker
     * mehr auf vw->chunks zugreifen. Auf _WIN32 existiert kein Pool. */
#ifndef _WIN32
    moo_voxel_pool_destroy(vw);
#endif
    if (vw->chunks) {
        for (int32_t i = 0; i < vw->chunk_cap; i++) {
            if (!vw->chunks[i].occupied) continue;
            /* GPU-Render-Cache-Lifetime (Plan-005 1b, Risiko 10): jede gesetzte
             * Render-ID GENAU EINMAL freigeben. moo_3d_chunk_delete ist ohne
             * aktives Backend ein safe no-op (if !g_backend||!g_ctx return) ->
             * die VoxelWorld darf das Fenster/Backend ueberleben. CPU-Daten
             * werden IMMER freigegeben, unabhaengig vom Backend-Zustand. */
            if (vw->chunks[i].render_id >= 0) {
                moo_3d_chunk_delete(moo_number((double)vw->chunks[i].render_id));
                vw->chunks[i].render_id = -1;
            }
            moo_voxel_chunk_free_data(&vw->chunks[i]);
        }
        moo_free(vw->chunks);
        vw->chunks = NULL;
    }
    moo_free(vw);
}
