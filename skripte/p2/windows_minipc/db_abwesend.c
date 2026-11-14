/* P2-T73 hosted Probe: SQLite ist im MinGW-Sysroot nicht vorhanden, moo_db.c
 * faellt daher aus der Windows-Staticlib (runtime_mingw.sh meldet das). Der
 * Release-Dispatch in compiler/runtime/moo_memory.c (MOO_DATABASE/MOO_DB_STMT)
 * und die stdlib referenzieren diese vier Symbole trotzdem. Ohne
 * moo_db_connect kann in der Probe kein Datenbankwert entstehen; die Frees
 * sind unerreichbar, die Close-Aufrufe melden einen definierten Moo-Fehler
 * statt still zu tun. Testharness-Stub wie compiler/runtime/tests/test_sft_asan.c,
 * kein Produktcode. */
#include "moo_runtime.h"

static void db_fehlt(void) {
    moo_throw(moo_string_new("SQLite ist in der Windows-hosted Probe nicht gelinkt (db_abwesend.c)"));
}

void moo_db_close(MooValue db) { (void)db; db_fehlt(); }
void moo_db_stmt_close(MooValue stmt) { (void)stmt; db_fehlt(); }
void moo_db_free(void* ptr) { (void)ptr; db_fehlt(); }
void moo_db_stmt_free(void* ptr) { (void)ptr; db_fehlt(); }
