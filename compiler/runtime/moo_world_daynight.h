#ifndef MOO_WORLD_DAYNIGHT_H
#define MOO_WORLD_DAYNIGHT_H

/**
 * moo_world_daynight.h — Tag-Nacht-Zyklus Mathe.
 * Reine Berechnungen, keine GL/Vulkan Abhaengigkeit.
 * time_of_day: 0.0=Mitternacht, 0.25=Sonnenaufgang, 0.5=Mittag, 0.75=Sonnenuntergang
 */

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Licht-Richtung (normalisiert, zeigt ZUR Sonne) */
static inline void daynight_light_dir(float t, float* x, float* y, float* z) {
    float angle = t * 2.0f * (float)M_PI;
    /* Sonne dreht sich Ost→Sued→West (x-Achse), steigt/sinkt (y-Achse) */
    *x = -cosf(angle);
    *y = sinf(angle);
    *z = 0.3f;
    /* Normalisieren */
    float len = sqrtf((*x)*(*x) + (*y)*(*y) + (*z)*(*z));
    if (len > 0) { *x /= len; *y /= len; *z /= len; }
}

/* Ist Sonne ueber dem Horizont? (y > 0) */
static inline int daynight_sun_visible(float t) {
    float angle = t * 2.0f * (float)M_PI;
    return sinf(angle) > -0.05f; /* Leicht unter Horizont fuer Daemmerung */
}

/* Ambient-Licht: Tag hell, Nacht dunkel, sanfter Uebergang */
static inline float daynight_ambient(float t) {
    float angle = t * 2.0f * (float)M_PI;
    float sun_height = sinf(angle); /* -1..1, positiv = Tag */

    /* Smoothstep-artige Interpolation */
    float day_factor;
    if (sun_height > 0.1f) {
        day_factor = 1.0f; /* Voller Tag */
    } else if (sun_height < -0.1f) {
        day_factor = 0.0f; /* Volle Nacht */
    } else {
        /* Daemmerung: Linear zwischen -0.1 und 0.1 */
        day_factor = (sun_height + 0.1f) / 0.2f;
    }

    /* Ambient: 0.03 (Nacht/Mondlicht) bis 0.15 (Tag) */
    return 0.03f + day_factor * 0.12f;
}

/* Sonne-Farbe: Mittag=Gold, Morgen/Abend=Orange-Rot, Nacht=aus */
static inline void daynight_sun_color(float t, float* r, float* g, float* b) {
    float angle = t * 2.0f * (float)M_PI;
    float sun_height = sinf(angle);

    if (sun_height < -0.05f) {
        /* Unter Horizont — unsichtbar */
        *r = 0; *g = 0; *b = 0;
        return;
    }

    /* Hoehe 0..1 bestimmt Farbe */
    float h = sun_height;
    if (h > 1.0f) h = 1.0f;
    if (h < 0.0f) h = 0.0f;

    if (h < 0.15f) {
        /* Tief: Rot-Orange (Sonnenauf-/untergang) */
        float f = h / 0.15f;
        *r = 1.0f;
        *g = 0.3f + f * 0.35f; /* 0.3 → 0.65 */
        *b = 0.1f + f * 0.1f;  /* 0.1 → 0.2 */
    } else {
        /* Hoch: Orange → Gold → Weiss-Gold */
        float f = (h - 0.15f) / 0.85f;
        *r = 1.0f;
        *g = 0.65f + f * 0.2f;  /* 0.65 → 0.85 */
        *b = 0.2f + f * 0.15f;  /* 0.2 → 0.35 */
    }
}

/* Fog/Himmel-Farbe: Tag=hell, Daemmerung=orange, Nacht=dunkelblau */
static inline void daynight_fog_color(float t, float* r, float* g, float* b) {
    float angle = t * 2.0f * (float)M_PI;
    float sun_height = sinf(angle);

    /* Nacht-Farbe (dunkelblau) */
    float nr = 0.05f, ng = 0.05f, nb = 0.15f;
    /* Tag-Farbe (hell-grau, passend zu fogColor) */
    float dr = 0.85f, dg = 0.87f, db = 0.90f;
    /* Daemmerung-Farbe (warm orange) */
    float sr = 0.90f, sg = 0.55f, sb = 0.30f;

    if (sun_height > 0.15f) {
        /* Voller Tag */
        *r = dr; *g = dg; *b = db;
    } else if (sun_height > -0.05f) {
        /* Daemmerung: Nacht→Orange→Tag */
        float f = (sun_height + 0.05f) / 0.2f; /* 0..1 */

        if (f < 0.5f) {
            /* Nacht → Orange (f: 0→0.5) */
            float t2 = f * 2.0f;
            *r = nr + (sr - nr) * t2;
            *g = ng + (sg - ng) * t2;
            *b = nb + (sb - nb) * t2;
        } else {
            /* Orange → Tag (f: 0.5→1) */
            float t2 = (f - 0.5f) * 2.0f;
            *r = sr + (dr - sr) * t2;
            *g = sg + (dg - sg) * t2;
            *b = sb + (db - sb) * t2;
        }
    } else {
        /* Volle Nacht */
        *r = nr; *g = ng; *b = nb;
    }
}

/* Clear-Color = Fog-Color (fuer nahtlosen Horizont) */
static inline void daynight_clear_color(float t, float* r, float* g, float* b) {
    daynight_fog_color(t, r, g, b);
}

#endif
