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
 *     OFFENE GRENZE (dokumentiert, sauberer Folge-Task statt Hack): die
 *     Dirty-Propagation in moo_voxel_setzen markiert weiterhin nur die 6 FACE-
 *     Nachbarn (Plan-1b-Contract, von den QA-Harnesses fest geprueft). Aendert
 *     sich ein DIAGONALER Nachbar, wird die Boundary-AO des Nachbarchunks nicht
 *     automatisch neu getriggert (Staleness, kein Crash/Leak). Die Erweiterung
 *     auf 18/26-Nachbarn-Dirty wuerde den geprueften 6-Nachbarn-Contract aendern
 *     und ist als Folge-Task empfohlen.
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
typedef struct {
    int32_t   cx, cy, cz;   /* Chunk-Koordinaten (signed, negative first-class) */
    uint16_t* palette;      /* distinkte Block-IDs; [0]=Luft. NULL = leerer Chunk */
    uint32_t* indices;      /* bitgepackte Palette-Indizes, NULL = leerer Chunk */
    uint16_t  palette_count;/* genutzte Palette-Eintraege (>=1 wenn allokiert) */
    uint16_t  palette_cap;  /* allokierte Palette-Eintraege */
    uint8_t   bits;         /* Bit pro Index: 0=leer, sonst 1/2/4/8/16 */
    bool      occupied;     /* Hashmap-Slot belegt? */
    int32_t   render_id;    /* GPU-Render-Chunk-ID, -1 = kein Cache (CPU/GPU getrennt) */
    bool      dirty;        /* true = Geometrie veraltet, remesh noetig */
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
 * Phase 1c: Palette + bitgepacktes Index-Array — Daten-Layout-Schicht
 *
 * ZENTRAL: alle Block-Lese-/Schreibzugriffe auf einen Chunk laufen ueber
 * chunk_block_get / chunk_block_set. Hoehere Pfade (holen, setzen, Mesher,
 * world_block) kennen das Bitpacking NICHT — sie sehen nur Block-IDs. Damit
 * bleibt die Palette ein reines Implementierungsdetail dieser Schicht.
 *
 * Bitbreiten-Tiers: 1 Bit (2 IDs), 2 (4), 4 (16), 8 (256), 16 (65536). Die
 * Index-Bits passen IMMER glatt in 32-Bit-Woerter (32 % bits == 0 fuer alle
 * Tiers), daher kreuzt kein Index eine Wortgrenze -> einfaches Pack/Unpack.
 * ======================================================================== */

/* Anzahl 32-Bit-Woerter fuer VOL Eintraege a bits Bit. */
static inline size_t moo_voxel_index_words(uint8_t bits) {
    size_t per_word = 32u / bits;                 /* Eintraege pro Wort */
    return ((size_t)MOO_VOXEL_CHUNK_VOL + per_word - 1) / per_word;
}

/* Kleinste Bitbreite, die >= n distinkte Palette-Eintraege fasst. */
static inline uint8_t moo_voxel_bits_for(int32_t n) {
    if (n <= 2)   return 1;
    if (n <= 4)   return 2;
    if (n <= 16)  return 4;
    if (n <= 256) return 8;
    return 16;
}

/* Liest den Palette-Index an Voxel lidx aus dem bitgepackten Array. */
static inline uint32_t moo_voxel_idx_get(const uint32_t* indices, uint8_t bits, int32_t lidx) {
    uint32_t per_word = 32u / bits;
    uint32_t mask = (bits == 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    uint32_t word = indices[(uint32_t)lidx / per_word];
    uint32_t shift = ((uint32_t)lidx % per_word) * bits;
    return (word >> shift) & mask;
}

/* Schreibt den Palette-Index pidx an Voxel lidx ins bitgepackte Array. */
static inline void moo_voxel_idx_set(uint32_t* indices, uint8_t bits, int32_t lidx, uint32_t pidx) {
    uint32_t per_word = 32u / bits;
    uint32_t mask = (bits == 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    uint32_t w = (uint32_t)lidx / per_word;
    uint32_t shift = ((uint32_t)lidx % per_word) * bits;
    indices[w] = (indices[w] & ~(mask << shift)) | ((pidx & mask) << shift);
}

/* Liefert die Block-ID an Voxel lidx (lidx in [0,VOL)). Leerer Chunk = Luft. */
static inline uint16_t moo_voxel_chunk_block_get(const MooVoxelChunk* ch, int32_t lidx) {
    if (!ch->indices || ch->bits == 0) return 0;  /* leerer Chunk = Luft */
    uint32_t pidx = moo_voxel_idx_get(ch->indices, ch->bits, lidx);
    if (pidx >= ch->palette_count) return 0;       /* defensiv */
    return ch->palette[pidx];
}

/* Allokiert Palette + Index-Array fuer einen bislang leeren Chunk. palette[0]
 * ist Luft. bits ist die Start-Bitbreite. Gibt false bei OOM. */
static bool moo_voxel_chunk_alloc(MooVoxelChunk* ch, uint8_t bits) {
    uint16_t pcap = (uint16_t)((1u << bits) < 4u ? 4u : (1u << bits));
    if (pcap > 256 && bits < 16) pcap = 256;       /* 8-Bit-Tier deckelt bei 256 */
    if (bits == 16) pcap = 256;                    /* 16-Bit-Palette waechst dynamisch ab 256 */
    uint16_t* pal = (uint16_t*)moo_alloc((size_t)pcap * sizeof(uint16_t));
    if (!pal) return false;
    memset(pal, 0, (size_t)pcap * sizeof(uint16_t)); /* moo_alloc nullt nicht */
    size_t iwords = moo_voxel_index_words(bits);
    uint32_t* idx = (uint32_t*)moo_alloc(iwords * sizeof(uint32_t));
    if (!idx) { moo_free(pal); return false; }
    memset(idx, 0, iwords * sizeof(uint32_t));     /* alle Voxel -> Palette[0] = Luft */
    ch->palette = pal;
    ch->palette_cap = pcap;
    ch->palette_count = 1;                          /* nur Luft */
    ch->indices = idx;
    ch->bits = bits;
    return true;
}

/* Packt das Index-Array auf eine groessere Bitbreite um. Gibt false bei OOM. */
static bool moo_voxel_chunk_widen(MooVoxelChunk* ch, uint8_t new_bits) {
    size_t new_words = moo_voxel_index_words(new_bits);
    uint32_t* fresh = (uint32_t*)moo_alloc(new_words * sizeof(uint32_t));
    if (!fresh) return false;
    memset(fresh, 0, new_words * sizeof(uint32_t));
    for (int32_t i = 0; i < MOO_VOXEL_CHUNK_VOL; i++) {
        uint32_t pidx = moo_voxel_idx_get(ch->indices, ch->bits, i);
        moo_voxel_idx_set(fresh, new_bits, i, pidx);
    }
    moo_free(ch->indices);
    ch->indices = fresh;
    ch->bits = new_bits;
    return true;
}

/* Findet die Palette-Position von id oder fuegt sie hinzu (lazy bit-upgrade
 * + Palette-Wachstum bei Bedarf). Gibt den Palette-Index zurueck, oder -1 bei
 * OOM (Aufrufer wirft). Voraussetzung: Chunk ist bereits allokiert. */
static int32_t moo_voxel_palette_intern(MooVoxelChunk* ch, uint16_t id) {
    for (uint16_t i = 0; i < ch->palette_count; i++) {
        if (ch->palette[i] == id) return (int32_t)i;
    }
    /* Neue ID: ggf. Index-Bitbreite hochziehen (lazy upgrade). */
    int32_t need = (int32_t)ch->palette_count + 1;
    uint8_t need_bits = moo_voxel_bits_for(need);
    if (need_bits > ch->bits) {
        if (!moo_voxel_chunk_widen(ch, need_bits)) return -1;
    }
    /* Palette-Tabelle ggf. vergroessern. */
    if (ch->palette_count >= ch->palette_cap) {
        uint32_t new_cap = (uint32_t)ch->palette_cap * 2u;
        if (new_cap > 65536u) new_cap = 65536u;
        uint16_t* fresh = (uint16_t*)moo_alloc((size_t)new_cap * sizeof(uint16_t));
        if (!fresh) return -1;
        memset(fresh, 0, (size_t)new_cap * sizeof(uint16_t));
        memcpy(fresh, ch->palette, (size_t)ch->palette_count * sizeof(uint16_t));
        moo_free(ch->palette);
        ch->palette = fresh;
        ch->palette_cap = (uint16_t)new_cap;
    }
    int32_t pos = (int32_t)ch->palette_count;
    ch->palette[pos] = id;
    ch->palette_count++;
    return pos;
}

/* Setzt Block-ID id an Voxel lidx. Allokiert den Chunk lazy beim ersten
 * Festblock; Luft in einen leeren Chunk ist ein No-op. Gibt false bei OOM. */
static bool moo_voxel_chunk_block_set(MooVoxelChunk* ch, int32_t lidx, uint16_t id) {
    if (!ch->indices) {
        if (id == 0) return true;                  /* Luft in leeren Chunk: nichts tun */
        if (!moo_voxel_chunk_alloc(ch, 1)) return false;
    }
    int32_t pidx = moo_voxel_palette_intern(ch, id);
    if (pidx < 0) return false;
    moo_voxel_idx_set(ch->indices, ch->bits, lidx, (uint32_t)pidx);
    return true;
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

/* Generiert genau EINEN Chunk (cx,cy,cz) aus der Heightmap und claimt seinen
 * Hashmap-Slot. occupied wird IMMER gesetzt (auch fuer reine Luft-Chunks),
 * damit der Lese-Pfad (moo_voxel_holen) einen bereits generierten Chunk nicht
 * erneut generiert. Schreibzugriffe laufen ueber die Palette-Schicht
 * (chunk_block_set) — kein direkter Array-Zugriff. Gibt false bei OOM. */
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
        ch->palette = NULL;
        ch->indices = NULL;
        ch->palette_count = 0;
        ch->palette_cap = 0;
        ch->bits = 0;
        ch->render_id = -1;
        ch->dirty = true;
        w->chunk_count++;
    }

    int32_t base_x = cx * MOO_VOXEL_CHUNK_DIM;
    int32_t base_y = cy * MOO_VOXEL_CHUNK_DIM;
    int32_t base_z = cz * MOO_VOXEL_CHUNK_DIM;

    for (int32_t lx = 0; lx < MOO_VOXEL_CHUNK_DIM; lx++) {
        for (int32_t ly = 0; ly < MOO_VOXEL_CHUNK_DIM; ly++) {
            int32_t wx = base_x + lx;
            int32_t wy = base_y + ly;
            for (int32_t lz = 0; lz < MOO_VOXEL_CHUNK_DIM; lz++) {
                uint16_t id = moo_voxel_gen_block(w->seed, wx, wy, base_z + lz);
                if (id == 0) continue; /* Luft: nichts schreiben (Chunk bleibt sparsam) */
                if (!moo_voxel_chunk_block_set(ch, moo_voxel_local_index(lx, ly, lz), id)) {
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
        ch->palette = NULL;   /* leerer Chunk = NULL (Phase 1c) */
        ch->indices = NULL;
        ch->palette_count = 0;
        ch->palette_cap = 0;
        ch->bits = 0;
        ch->render_id = -1;   /* noch kein GPU-Cache */
        ch->dirty = true;     /* neu -> braucht Mesh */
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
    if (!ch->occupied || !ch->indices) {
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
            if (vw->chunks[idx].palette) {
                moo_free(vw->chunks[idx].palette);
                vw->chunks[idx].palette = NULL;
            }
            if (vw->chunks[idx].indices) {
                moo_free(vw->chunks[idx].indices);
                vw->chunks[idx].indices = NULL;
            }
            vw->chunks[idx].palette_count = 0;
            vw->chunks[idx].palette_cap = 0;
            vw->chunks[idx].bits = 0;
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
    int64_t bytes_blocks = 0;
    int64_t bytes_palette = 0;

    for (int32_t i = 0; i < vw->chunk_cap; i++) {
        if (!vw->chunks[i].occupied) continue;
        chunks++;
        if (vw->chunks[i].indices) {
            /* bytes_blocks = bitgepacktes Index-Array (die eigentlichen Voxel-
             * Daten). bytes_palette = die distinkte-ID-Tabelle. */
            bytes_blocks  += (int64_t)moo_voxel_index_words(vw->chunks[i].bits)
                             * (int64_t)sizeof(uint32_t);
            bytes_palette += (int64_t)vw->chunks[i].palette_cap
                             * (int64_t)sizeof(uint16_t);
        } else {
            empty_chunks++;
        }
    }

    /* Mesh-Geometrie liegt im GPU-Render-Cache (separates Modul) -> 0. */
    int64_t bytes_mesh = 0;
    /* Verwaltungs-Overhead (World-Struct + Hashmap-Tabelle) in bytes_total. */
    int64_t bytes_overhead =
        (int64_t)sizeof(MooVoxelWorld) +
        (int64_t)vw->chunk_cap * (int64_t)sizeof(MooVoxelChunk);
    int64_t bytes_total = bytes_blocks + bytes_palette + bytes_mesh + bytes_overhead;

    MooValue d = moo_dict_new();
    moo_dict_set(d, moo_string_new("chunks"),        moo_number((double)chunks));
    moo_dict_set(d, moo_string_new("bytes_blocks"),  moo_number((double)bytes_blocks));
    moo_dict_set(d, moo_string_new("bytes_palette"), moo_number((double)bytes_palette));
    moo_dict_set(d, moo_string_new("bytes_mesh"),    moo_number((double)bytes_mesh));
    moo_dict_set(d, moo_string_new("bytes_total"),   moo_number((double)bytes_total));
    moo_dict_set(d, moo_string_new("empty_chunks"),  moo_number((double)empty_chunks));
    return d;
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
    if (!ch || !ch->indices) return 0;
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
        if (!ch || !ch->indices) return 0; /* leerer/fehlender Nachbar = Luft */
        ix = moo_voxel_floormod(lx, MOO_VOXEL_CHUNK_DIM);
        iy = moo_voxel_floormod(ly, MOO_VOXEL_CHUNK_DIM);
        iz = moo_voxel_floormod(lz, MOO_VOXEL_CHUNK_DIM);
    }
    if (!ch->indices) return 0;
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
    if (!ch->indices) {
        if (ch->render_id >= 0) {
            moo_3d_chunk_delete(moo_number((double)ch->render_id));
            ch->render_id = -1;
        }
        ch->dirty = false;
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
        if (!ch->occupied || !ch->dirty) continue;
        if (!ch->indices) {
            if (ch->render_id >= 0) {
                moo_3d_chunk_delete(moo_number((double)ch->render_id));
                ch->render_id = -1;
            }
            ch->dirty = false;
            continue;
        }
        MooVoxelMeshBuf buf; mesh_buf_init(&buf);
        if (moo_voxel_build_mesh_cpu(vw, ch, &buf)) {
            moo_voxel_upload_mesh(ch, &buf);
        } else {
            ch->dirty = false; /* OOM: nicht endlos retryen */
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

    /* Geleerte Chunks (indices==NULL) zuerst seriell aufraeumen: alte Render-ID
     * freigeben, dirty loeschen. Diese gehen NICHT in die Worker-Queue (kein
     * Mesh zu bauen). */
    for (int32_t i = 0; i < vw->chunk_cap; i++) {
        MooVoxelChunk* ch = &vw->chunks[i];
        if (!ch->occupied || !ch->dirty || ch->indices) continue;
        if (ch->render_id >= 0) {
            moo_3d_chunk_delete(moo_number((double)ch->render_id));
            ch->render_id = -1;
        }
        ch->dirty = false;
    }

#ifdef _WIN32
    /* POSIX-only Threadpool nicht verfuegbar -> synchron meshen (s. Datei-Header). */
    return moo_number((double)moo_voxel_aktualisieren_sync(vw));
#else
    /* Dirty, nicht-leere Chunks zaehlen. */
    int n_dirty = 0;
    for (int32_t i = 0; i < vw->chunk_cap; i++) {
        MooVoxelChunk* ch = &vw->chunks[i];
        if (ch->occupied && ch->dirty && ch->indices) n_dirty++;
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
        if (ch->occupied && ch->dirty && ch->indices) jobs[k++].slot = i;
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
            moo_voxel_upload_mesh(ch, &jobs[j].result);
        } else {
            ch->dirty = false; /* OOM im Worker: nicht endlos retryen */
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
    if (!ch || !ch->indices) return 0; /* leerer/fehlender Chunk = Luft */
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
            if (vw->chunks[i].palette) {
                moo_free(vw->chunks[i].palette);
                vw->chunks[i].palette = NULL;
            }
            if (vw->chunks[i].indices) {
                moo_free(vw->chunks[i].indices);
                vw->chunks[i].indices = NULL;
            }
        }
        moo_free(vw->chunks);
        vw->chunks = NULL;
    }
    moo_free(vw);
}
