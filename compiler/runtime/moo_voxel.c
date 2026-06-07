/**
 * moo_voxel.c - Voxel-Welt-Runtime fuer moo (Plan 005, Phase 1a).
 *
 * Phase 1a = "NAIV": Korrektheit vor Speichereffizienz.
 *   - Jeder belegte Chunk haelt ein volles uint16_t blocks[CHUNK_DIM^3] (64 KB bei 32^3).
 *   - Palette-Kompression kommt erst in Phase 1c (RT3), Mesher in 1b (RT2).
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
 */

#include "moo_runtime.h"

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
typedef struct {
    int32_t   cx, cy, cz;   /* Chunk-Koordinaten (signed, negative first-class) */
    uint16_t* blocks;       /* MOO_VOXEL_CHUNK_VOL Eintraege, oder NULL = leer */
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
        ch->blocks = NULL;
        ch->render_id = -1;   /* noch kein GPU-Cache */
        ch->dirty = true;     /* neu -> braucht Mesh */
        vw->chunk_count++;
    }

    if (!ch->blocks) {
        /* Setzen von Luft (0) in einen nie-allokierten Chunk: keine Allokation
         * noetig, der Chunk bleibt logisch leer. */
        if (id == 0) {
            return moo_bool(true);
        }
        size_t blocks_bytes = (size_t)MOO_VOXEL_CHUNK_VOL * sizeof(uint16_t);
        ch->blocks = (uint16_t*)moo_alloc(blocks_bytes);
        if (!ch->blocks) {
            moo_throw(moo_error("Voxel-Fehler in voxel_setzen: Speicher fuer Chunk-Blocks erschoepft"));
            return moo_none();
        }
        /* moo_alloc nutzt malloc und nullt NICHT -> Chunk explizit auf Luft (0). */
        memset(ch->blocks, 0, blocks_bytes);
    }

    ch->blocks[moo_voxel_local_index(lx, ly, lz)] = (uint16_t)id;

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
    if (!ch->occupied || !ch->blocks) {
        /* Nie allokierter / leerer Chunk = Luft. */
        return moo_number(0.0);
    }

    int32_t lx = moo_voxel_floormod(wx, MOO_VOXEL_CHUNK_DIM);
    int32_t ly = moo_voxel_floormod(wy, MOO_VOXEL_CHUNK_DIM);
    int32_t lz = moo_voxel_floormod(wz, MOO_VOXEL_CHUNK_DIM);
    return moo_number((double)ch->blocks[moo_voxel_local_index(lx, ly, lz)]);
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
            if (vw->chunks[idx].blocks) {
                moo_free(vw->chunks[idx].blocks);
                vw->chunks[idx].blocks = NULL;
            }
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

    for (int32_t i = 0; i < vw->chunk_cap; i++) {
        if (!vw->chunks[i].occupied) continue;
        chunks++;
        if (vw->chunks[i].blocks) {
            bytes_blocks += (int64_t)MOO_VOXEL_CHUNK_VOL * (int64_t)sizeof(uint16_t);
        } else {
            empty_chunks++;
        }
    }

    /* Phase 1a hat weder Palette noch Mesh -> stabile Keys, Werte 0. */
    int64_t bytes_palette = 0;
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
        if (!ch || !ch->blocks) return 0; /* leerer/fehlender Nachbar = Luft */
        ix = moo_voxel_floormod(lx, MOO_VOXEL_CHUNK_DIM);
        iy = moo_voxel_floormod(ly, MOO_VOXEL_CHUNK_DIM);
        iz = moo_voxel_floormod(lz, MOO_VOXEL_CHUNK_DIM);
    }
    if (!ch->blocks) return 0;
    return ch->blocks[moo_voxel_local_index(ix, iy, iz)];
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
                uint16_t id = ch->blocks[moo_voxel_local_index(lx, ly, lz)];
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
    if (!ch->blocks) {
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

MooValue moo_voxel_aktualisieren(MooValue welt) {
    MooVoxelWorld* vw = moo_voxel_check(welt, "voxel_aktualisieren");
    if (!vw) return moo_none();

    /* Ohne Backend nichts tun (dirty bleibt fuer spaeter erhalten). */
    if (!moo_3d_backend_active()) {
        return moo_number(0.0);
    }

    int64_t remeshed = 0;
    for (int32_t i = 0; i < vw->chunk_cap; i++) {
        MooVoxelChunk* ch = &vw->chunks[i];
        if (!ch->occupied || !ch->dirty) continue;
        if (!ch->blocks) {
            /* Geleerter Chunk: alte Render-ID einmal freigeben. */
            if (ch->render_id >= 0) {
                moo_3d_chunk_delete(moo_number((double)ch->render_id));
                ch->render_id = -1;
            }
            ch->dirty = false;
            continue;
        }
        moo_voxel_remesh_chunk(vw, ch);
        remeshed++;
    }
    return moo_number((double)remeshed);
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
            if (vw->chunks[i].blocks) {
                moo_free(vw->chunks[i].blocks);
                vw->chunks[i].blocks = NULL;
            }
        }
        moo_free(vw->chunks);
        vw->chunks = NULL;
    }
    moo_free(vw);
}
