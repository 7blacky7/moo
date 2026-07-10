/**
 * moo_shard.h — Streaming-Token-Shards + Dataloader (KIP-E1, Task f91d782c).
 * ============================================================================
 * Shard-Format fuer VORTOKENISIERTE Korpora nach X3-§3-Vertrag. Der Loader
 * liest gefenstert (fseek+fread) OHNE das ganze Shard in den RAM zu ziehen —
 * nur Header + Blockreihenfolge (klein) leben im Speicher, die Token-Daten
 * bleiben auf der Platte.
 *
 * BINAERFORMAT (Little-Endian, Version 1) — fester 48-Byte-Header:
 *   [0]  magic "MOOSHARD"          (8)
 *   [8]  u32 format_version         (== 1)
 *   [12] u32 flags                  (Bit0: hat_loss_maske / SFT)
 *   [16] u32 datenart               (0 = pretrain, 1 = sft)
 *   [20] u32 n_docs
 *   [24] u64 n_tokens
 *   [32] u64 tokenizer_version      (T2-Artefakt-Hash, siehe tokenizer_hash)
 *   [40] u64 crc                    (FNV-1a-64 ueber Bytes [48 .. EOF])
 *   -- danach --
 *   doc_offsets : (n_docs+1) * u64   kumulative Token-Grenzen (Packing);
 *                                    [0]=0, [n_docs]=n_tokens
 *   token_ids   : n_tokens * u32
 *   loss_maske  : (nur SFT) ceil(n_tokens/8) Bytes, 1 Bit je Token
 * Reserviert (X3, NICHT Pflicht in E1): quelle, lizenz_tag — spaeter.
 *
 * REFCOUNT: Args geliehen, Rueckgabe +1 (Codegen-Arms releasen Args).
 * ============================================================================
 */
#ifndef MOO_SHARD_H
#define MOO_SHARD_H

#include "moo_runtime.h"

/* Schreibt ein Shard aus einer Liste von Token-Id-Tensoren (ein Tensor je
 * Dokument). datenart = "pretrain" | "sft". tokenizer_version = Hex-String
 * (tokenizer_hash) oder Zahl. masken (nur sft): Liste von 0/1-Tensoren,
 * gleiche Anzahl/Laenge wie docs. -> Bool. */
MooValue moo_shard_schreiben(MooValue pfad, MooValue docs, MooValue datenart,
                             MooValue tokenizer_version, MooValue masken);

/* Header lesen -> Dict { version, datenart, n_docs, n_tokens,
 * tokenizer_version(hex), hat_maske }. Kein Vollladen. */
MooValue moo_shard_info(MooValue pfad);

/* Prueft die CRC des Shards. -> Bool (false bei Beschaedigung). */
MooValue moo_shard_pruefen(MooValue pfad);

/* Gefenstertes Lesen der Token-Ids [start, start+laenge) -> Tensor[laenge]. */
MooValue moo_shard_fenster(MooValue pfad, MooValue start, MooValue laenge);

/* Gefenstertes Lesen der Loss-Maske (nur SFT) -> Tensor[laenge] aus 0/1. */
MooValue moo_shard_fenster_maske(MooValue pfad, MooValue start, MooValue laenge);

/* Seed-deterministische Blockreihenfolge: gibt die permutierten START-Offsets
 * aller seq_len-Bloecke als Liste von Zahlen (f64-exakt, auch > 2^24) zurueck.
 * Gleicher Seed -> identische Reihenfolge. Rest-Tokens (n_tokens % seq_len)
 * werden verworfen. */
MooValue moo_shard_reihenfolge(MooValue pfad, MooValue seed, MooValue seq_len);

/* Seed-deterministischer Train/Val-Split auf SHARD-Ebene: partitioniert eine
 * Liste von Shard-Pfaden -> Dict { train: Liste, val: Liste }. */
MooValue moo_shard_split(MooValue pfad_liste, MooValue val_anteil, MooValue seed);

#endif /* MOO_SHARD_H */
