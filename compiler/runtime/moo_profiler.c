/**
 * moo_profiler.c — Laufzeit-Profiler fuer moo.
 * Misst Ausfuehrungszeit und Aufrufhaeufigkeit pro Funktion.
 */

#include "moo_runtime.h"
#include <time.h>

#define MAX_PROFILE_ENTRIES 256

typedef struct {
    const char* name;
    double total_time;
    int64_t call_count;
    double start_time; // Zeitstempel des letzten Enter
} ProfileEntry;

static ProfileEntry profile_table[MAX_PROFILE_ENTRIES];
static int profile_count = 0;

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static ProfileEntry* find_or_create(const char* name) {
    // Suche bestehenden Eintrag
    for (int i = 0; i < profile_count; i++) {
        if (strcmp(profile_table[i].name, name) == 0) {
            return &profile_table[i];
        }
    }
    // Neuer Eintrag
    if (profile_count >= MAX_PROFILE_ENTRIES) return NULL;
    ProfileEntry* e = &profile_table[profile_count++];
    e->name = name;
    e->total_time = 0.0;
    e->call_count = 0;
    e->start_time = 0.0;
    return e;
}

// Wird am Anfang jeder profilierten Funktion aufgerufen
void moo_profile_enter(MooValue name_val) {
    if (name_val.tag != MOO_STRING) return;
    const char* name = MV_STR(name_val)->chars;
    ProfileEntry* e = find_or_create(name);
    if (!e) return;
    e->call_count++;
    e->start_time = get_time_sec();
}

// Wird am Ende jeder profilierten Funktion aufgerufen (vor Return)
void moo_profile_exit(MooValue name_val) {
    if (name_val.tag != MOO_STRING) return;
    const char* name = MV_STR(name_val)->chars;
    ProfileEntry* e = find_or_create(name);
    if (!e) return;
    double elapsed = get_time_sec() - e->start_time;
    e->total_time += elapsed;
}

// Sortier-Helfer: nach total_time absteigend
static int cmp_profile(const void* a, const void* b) {
    double ta = ((ProfileEntry*)a)->total_time;
    double tb = ((ProfileEntry*)b)->total_time;
    if (tb > ta) return 1;
    if (tb < ta) return -1;
    return 0;
}

// Gibt den Profiling-Report aus
void moo_profile_report(void) {
    if (profile_count == 0) return;

    // Nach Zeit sortieren
    qsort(profile_table, profile_count, sizeof(ProfileEntry), cmp_profile);

    printf("\n=== moo Profiler ===\n");
    printf("%-30s %12s %10s %12s\n", "Funktion", "Gesamt (ms)", "Aufrufe", "Avg (ms)");
    printf("%-30s %12s %10s %12s\n", "--------", "-----------", "-------", "--------");

    for (int i = 0; i < profile_count; i++) {
        ProfileEntry* e = &profile_table[i];
        if (e->call_count == 0) continue;
        double total_ms = e->total_time * 1000.0;
        double avg_ms = total_ms / (double)e->call_count;
        printf("%-30s %12.3f %10lld %12.3f\n",
            e->name, total_ms, (long long)e->call_count, avg_ms);
    }
    printf("====================\n");
}
