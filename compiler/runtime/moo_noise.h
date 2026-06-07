/**
 * moo_noise.h — Seed-parametrisierte Noise-Primitiven (Perlin + fBm) fuer moo.
 *
 * Extrahiert aus moo_world.c (Plan-005 P0.3). Im Gegensatz zur frueheren
 * Implementierung gibt es KEINEN globalen Seed mehr: jeder Aufruf erhaelt den
 * Seed explizit. Damit koennen mehrere Welten (z.B. VoxelWorld + klassische
 * Welt) deterministisch und ohne Seed-Leak nebeneinander existieren.
 *
 * Build-Gating: Diese Datei wird ueber denselben 3D/Game-Featureblock in
 * compiler/build.rs gebaut wie moo_world.c (cfg any(gl21,gl33,vulkan)).
 */
#ifndef MOO_NOISE_H
#define MOO_NOISE_H

/**
 * Streut einen rohen Integer-Seed per xorshift, damit benachbarte Seeds
 * (z.B. 1,2,3) deutlich unterschiedliche Noise-Felder erzeugen. Rueckgabe ist
 * stets im positiven Bereich [0, 0x7FFFFFFF].
 */
int moo_noise_scramble_seed(int seed);

/**
 * Integer-Hash fuer Gitterpunkt (ix,iy) unter gegebenem Seed.
 * Deterministisch; Rueckgabe im Bereich [0, 0x7FFFFFFF].
 */
int moo_noise_hash2(int seed, int ix, int iy);

/**
 * 2D-Wert-Noise (Perlin-artig, fade/lerp-interpoliert) im Bereich ca. [-1, 1].
 */
float moo_noise_perlin2(int seed, float x, float y);

/**
 * Fraktales 2D-fBm ueber mehrere Oktaven. amp skaliert das normalisierte
 * Resultat. octaves <= 0 liefert 0.
 */
float moo_noise_fbm(int seed, float x, float y, int octaves, float freq, float amp);

#endif /* MOO_NOISE_H */
