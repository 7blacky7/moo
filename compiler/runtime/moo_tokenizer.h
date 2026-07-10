/**
 * moo_tokenizer.h — Byte-level BPE-Tokenizer (KIP-T2, Task 0f4118c2).
 * ============================================================================
 * ZWECK
 *   Trainierbarer Byte-level Byte-Pair-Encoding-Tokenizer fuer die KIP-
 *   LLM-Runtime. Merges laufen auf ROHEN UTF-8-Bytes (kein Codepoint-,
 *   kein Wort-Pretokenizer in v1) — daraus folgt inhaerenter Byte-Fallback:
 *   jedes der 256 Byte ist ein Basis-Token, ergo braucht der Encoder NIE
 *   ein UNK-Token (Negativ-Gate). decode(encode(x)) == x ist byte-exakt,
 *   auch fuer ungueltiges UTF-8, Emoji, Umlaute und Null-Bytes.
 *
 * DETERMINISMUS (GPT-Review 7b5eb6e2 Pkt. 7)
 *   * KEIN Seed. Nichtdeterminismus wuerde durch einen Seed nur verdeckt.
 *   * Kanonische Tie-Break-Regel beim Training (dokumentiert, reproduzierbar):
 *       1. hoechste Paar-Haeufigkeit
 *       2. bei Gleichstand: lexikographisch kleinste MERGE-Bytefolge
 *          (bytes(links) ++ bytes(rechts))
 *       3. letzte Absicherung: kleinere (links_id, rechts_id) numerisch
 *     Damit sind zwei Trainingslaeufe auf identischem Korpus bit-identisch.
 *
 * UNICODE / BYTE-ROUNDTRIP
 *   * DEFAULT: KEINE Unicode-Normalisierung. Byte-Roundtrip ist garantiert.
 *   * Eine optionale NFC/NFKC-Policy waere ein versioniertes Artefakt-Flag
 *     (Bit im flags-Feld) und HEBT das Byte-Roundtrip-Versprechen explizit
 *     auf. In v1 ist flags == 0 (kein Normalisieren).
 *
 * DAS ARTEFAKT IST DER TOKENIZER
 *   Der Tokenizer wird NICHT als neuer MooValue-Tag modelliert (das haette
 *   moo_runtime.h + moo_memory.c-Dispatch beruehrt — moo_runtime.h ist ein
 *   geteiltes Kernartefakt). Stattdessen IST der Tokenizer exakt seine
 *   versionierte, selbstbeschreibende Binaerform, gehalten in einem
 *   MooString (Bytes + Laenge, vertraegt beliebige Bytes inkl. 0x00).
 *   Folge: In-Memory-Form und On-Disk-Artefakt sind BYTE-IDENTISCH —
 *   speichern/laden ist ein reiner Byte-Transfer, Roundtrip trivial exakt.
 *
 * BINAERFORMAT (Little-Endian, Version 1)
 *   Offset 0   : magic  "MOOBPE01"                        (8 Bytes)
 *   Offset 8   : u32 format_version                       (== 1)
 *   Offset 12  : u32 flags        (Bit0: unicode-normalisiert; v1 == 0)
 *   Offset 16  : u32 V   Gesamt-Vokabular                 (== 256 + M + S)
 *   Offset 20  : u32 M   Anzahl gelernter Merges
 *   Offset 24  : u32 S   Anzahl Spezial-Tokens (KIP-T3; v1 == 0)
 *   Offset 28  : u32 reserved                              (== 0)
 *   Merges     : M * { u32 links_id ; u32 rechts_id }
 *                (neue Id des Merges i ist implizit 256 + i)
 *   Vokab      : V * { u32 len ; byte[len] }   fuer Id 0..V-1 in Reihenfolge
 *                (Id 0..255 = Einzelbyte; 256..255+M = Merge-Bytefolge;
 *                 danach Spezial-Token-Bytes)
 *   Spezial    : S * { u32 id ; u32 namelen ; byte[namelen] }   (KIP-T3)
 *
 * REFCOUNT-KONVENTION (wie Tensor/Dataset)
 *   Alle Argumente GELIEHEN (kein retain/release auf Args); Rueckgaben +1.
 *   Die Codegen-Arms erledigen den Post-Call-Release der Heap-Args.
 *
 * FEHLER
 *   Ungueltige Eingaben/Artefakte werfen erklaerend (moo_throw) und geben
 *   moo_none() zurueck — kein stilles Fehlverhalten. Artefakt-Parsing ist
 *   voll bounds-gecheckt (untrusted-Artefakt-Haertung).
 * ============================================================================
 */
#ifndef MOO_TOKENIZER_H
#define MOO_TOKENIZER_H

#include "moo_runtime.h"

/* Trainiere einen Byte-level BPE auf `korpus` (Text/Bytes) bis das Vokabular
 * `vokab_groesse` erreicht (>=256, <=65536). Gibt das versionierte Artefakt
 * als MooString (+1) zurueck. Deterministisch, ohne Seed. */
MooValue moo_tok_trainiere(MooValue korpus, MooValue vokab_groesse);

/* encode(tok, text) -> Tensor[n] der Token-Ids (float, exakt < 2^24).
 * Leerer Text wirft (Tensoren brauchen >=1 Element). */
MooValue moo_tok_kodiere(MooValue tok, MooValue text);

/* decode(ids) -> String der exakten Bytes. ids: Tensor[n] oder Liste von
 * Zahlen. Ungueltige Id (>=V) wirft erklaerend. */
MooValue moo_tok_dekodiere(MooValue tok, MooValue ids);

/* Batch-encode: texte = Liste von Strings -> Liste von Tensor[n]. */
MooValue moo_tok_kodiere_stapel(MooValue tok, MooValue texte);

/* Artefakt in Datei schreiben (byte-identisch zum MooString). -> Bool. */
MooValue moo_tok_speichern(MooValue tok, MooValue pfad);

/* Artefakt aus Datei laden -> MooString (+1). Validiert den Header. */
MooValue moo_tok_laden(MooValue pfad);

/* Introspektion -> Dict { version, vokab, merges, spezial, flags, hash }. */
MooValue moo_tok_info(MooValue tok);

/* Stabiler 64-bit-Artefakt-Hash (FNV-1a) als Hex-String (16 Zeichen).
 * X3-§3-Shard-Header nutzt ihn als tokenizer_version. */
MooValue moo_tok_hash(MooValue tok);

#endif /* MOO_TOKENIZER_H */
