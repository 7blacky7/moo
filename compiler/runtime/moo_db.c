#include "moo_runtime.h"
#include <sqlite3.h>
#include <math.h>
extern MooValue moo_json_string(MooValue value);

// === Datenbank-Anbindung (SQLite) ===

typedef struct {
    sqlite3* db;
    char* url;
} MooDatabase;

MooValue moo_db_connect(MooValue url) {
    if (url.tag != MOO_STRING) {
        moo_throw(moo_error("DB-Fehler: URL muss ein String sein"));
        return moo_none();
    }
    const char* url_str = MV_STR(url)->chars;

    // Schema erkennen
    const char* path = NULL;
    if (strncmp(url_str, "sqlite://memory", 15) == 0) {
        path = ":memory:";
    } else if (strncmp(url_str, "sqlite:///", 10) == 0) {
        path = url_str + 9; // nach "sqlite://"
    } else if (strncmp(url_str, "sqlite://", 9) == 0) {
        path = url_str + 9;
    } else {
        char msg[512];
        snprintf(msg, sizeof(msg),
            "DB-Fehler: Unbekanntes URL-Schema in '%s'. "
            "Unterstuetzte Formate: 'sqlite:///pfad/zur/datei.db' oder 'sqlite://memory'",
            url_str);
        moo_throw(moo_error(msg));
        return moo_none();
    }

    MooDatabase* mdb = (MooDatabase*)moo_alloc(sizeof(MooDatabase));
    mdb->url = strdup(url_str);

    int rc = sqlite3_open(path, &mdb->db);
    if (rc != SQLITE_OK) {
        char msg[512];
        snprintf(msg, sizeof(msg), "DB-Fehler: Konnte Datenbank nicht oeffnen: %s",
                 sqlite3_errmsg(mdb->db));
        sqlite3_close(mdb->db);
        free(mdb->url);
        moo_free(mdb);
        moo_throw(moo_error(msg));
        return moo_none();
    }

    MooValue result;
    result.tag = MOO_DATABASE;
    moo_val_set_ptr(&result, mdb);
    return result;
}

static MooDatabase* check_db(MooValue db, const char* fn_name) {
    if (db.tag != MOO_DATABASE) {
        char msg[256];
        snprintf(msg, sizeof(msg), "DB-Fehler in %s: Erster Parameter ist keine Datenbank-Verbindung", fn_name);
        moo_throw(moo_error(msg));
        return NULL;
    }
    MooDatabase* mdb = (MooDatabase*)moo_val_as_ptr(db);
    if (!mdb || !mdb->db) {
        char msg[256];
        snprintf(msg, sizeof(msg), "DB-Fehler in %s: Datenbank-Verbindung ist bereits geschlossen", fn_name);
        moo_throw(moo_error(msg));
        return NULL;
    }
    return mdb;
}

static void format_sql_error(char* buf, size_t bufsize, const char* sql, const char* sqlite_msg) {
    snprintf(buf, bufsize, "SQL-Fehler in '%s': %s", sql, sqlite_msg);
}

MooValue moo_db_execute(MooValue db, MooValue sql) {
    MooDatabase* mdb = check_db(db, "db_ausfuehren");
    if (!mdb) return moo_none();

    if (sql.tag != MOO_STRING) {
        moo_throw(moo_error("DB-Fehler in db_ausfuehren: SQL muss ein String sein"));
        return moo_none();
    }

    const char* sql_str = MV_STR(sql)->chars;
    char* errmsg = NULL;
    int rc = sqlite3_exec(mdb->db, sql_str, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        char msg[1024];
        format_sql_error(msg, sizeof(msg), sql_str, errmsg ? errmsg : "Unbekannter Fehler");
        sqlite3_free(errmsg);
        moo_throw(moo_error(msg));
        return moo_none();
    }

    // Anzahl betroffener Zeilen zurueckgeben
    int changes = sqlite3_changes(mdb->db);
    return moo_number((double)changes);
}

MooValue moo_db_query(MooValue db, MooValue sql) {
    MooDatabase* mdb = check_db(db, "db_abfrage");
    if (!mdb) return moo_none();

    if (sql.tag != MOO_STRING) {
        moo_throw(moo_error("DB-Fehler in db_abfrage: SQL muss ein String sein"));
        return moo_none();
    }

    const char* sql_str = MV_STR(sql)->chars;
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(mdb->db, sql_str, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        char msg[1024];
        format_sql_error(msg, sizeof(msg), sql_str, sqlite3_errmsg(mdb->db));
        moo_throw(moo_error(msg));
        return moo_none();
    }

    MooValue result_list = moo_list_new(8);
    int col_count = sqlite3_column_count(stmt);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        MooValue row = moo_dict_new();

        for (int i = 0; i < col_count; i++) {
            const char* col_name = sqlite3_column_name(stmt, i);
            MooValue key = moo_string_new(col_name);
            MooValue val;

            int col_type = sqlite3_column_type(stmt, i);
            switch (col_type) {
                case SQLITE_INTEGER:
                    val = moo_number((double)sqlite3_column_int64(stmt, i));
                    break;
                case SQLITE_FLOAT:
                    val = moo_number(sqlite3_column_double(stmt, i));
                    break;
                case SQLITE_TEXT:
                    val = moo_string_new((const char*)sqlite3_column_text(stmt, i));
                    break;
                case SQLITE_NULL:
                    val = moo_none();
                    break;
                case SQLITE_BLOB:
                    val = moo_string_new("<BLOB>");
                    break;
                default:
                    val = moo_none();
                    break;
            }
            moo_dict_set(row, key, val);
        }
        moo_list_append(result_list, row);
    }

    if (rc != SQLITE_DONE) {
        char msg[1024];
        format_sql_error(msg, sizeof(msg), sql_str, sqlite3_errmsg(mdb->db));
        sqlite3_finalize(stmt);
        moo_throw(moo_error(msg));
        return moo_none();
    }

    sqlite3_finalize(stmt);
    return result_list;
}

void moo_db_close(MooValue db) {
    MooDatabase* mdb = check_db(db, "db_schliessen");
    if (!mdb) return;

    sqlite3_close(mdb->db);
    mdb->db = NULL;
    free(mdb->url);
    mdb->url = NULL;
}

// === Prepared Statements mit Parameter-Binding ===
//
// Bindet eine MooList<value> positional an sqlite3_stmt (1-based).
// Typ-Dispatch: MOO_NUMBER -> int64 (wenn ganzzahlig) bzw double, MOO_STRING -> text,
// MOO_NONE -> NULL, MOO_BOOL -> int64. Alles andere (Liste/Dict/Objekt) fliegt als
// JSON-Text rein, damit z.B. blobfreundlich speicherbar. sqlite bindet referenz-
// kopierend (SQLITE_TRANSIENT), daher muessen die Strings waehrend des Calls leben.
static int bind_list_params(sqlite3_stmt* stmt, MooValue params, char* errbuf, size_t errsize) {
    if (params.tag != MOO_LIST) {
        snprintf(errbuf, errsize, "params muss eine Liste sein");
        return 0;
    }
    MooList* lst = MV_LIST(params);
    int expected = sqlite3_bind_parameter_count(stmt);
    if (lst->length != expected) {
        snprintf(errbuf, errsize,
            "Anzahl Parameter stimmt nicht: SQL erwartet %d, Liste hat %d", expected, lst->length);
        return 0;
    }
    for (int i = 0; i < lst->length; i++) {
        MooValue v = lst->items[i];
        int idx = i + 1;
        int rc;
        switch (v.tag) {
            case MOO_NONE:
                rc = sqlite3_bind_null(stmt, idx);
                break;
            case MOO_BOOL:
                rc = sqlite3_bind_int64(stmt, idx, MV_BOOL(v) ? 1 : 0);
                break;
            case MOO_NUMBER: {
                double d = MV_NUM(v);
                double ipart;
                if (modf(d, &ipart) == 0.0 && ipart >= -9.2233720368547758e18 && ipart <= 9.2233720368547758e18) {
                    rc = sqlite3_bind_int64(stmt, idx, (sqlite3_int64)ipart);
                } else {
                    rc = sqlite3_bind_double(stmt, idx, d);
                }
                break;
            }
            case MOO_STRING: {
                MooString* s = MV_STR(v);
                rc = sqlite3_bind_text(stmt, idx, s->chars, s->length, SQLITE_TRANSIENT);
                break;
            }
            default: {
                // Fallback: als JSON serialisieren, dann als TEXT binden.
                MooValue js = moo_json_string(v);
                MooString* s = MV_STR(js);
                rc = sqlite3_bind_text(stmt, idx, s->chars, s->length, SQLITE_TRANSIENT);
                break;
            }
        }
        if (rc != SQLITE_OK) {
            snprintf(errbuf, errsize, "bind(%d) fehlgeschlagen: rc=%d", idx, rc);
            return 0;
        }
    }
    return 1;
}

MooValue moo_db_execute_with_params(MooValue db, MooValue sql, MooValue params) {
    MooDatabase* mdb = check_db(db, "db_ausfuehren_mit_params");
    if (!mdb) return moo_none();
    if (sql.tag != MOO_STRING) {
        moo_throw(moo_error("DB-Fehler in db_ausfuehren_mit_params: SQL muss ein String sein"));
        return moo_none();
    }
    const char* sql_str = MV_STR(sql)->chars;
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(mdb->db, sql_str, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        char msg[1024];
        format_sql_error(msg, sizeof(msg), sql_str, sqlite3_errmsg(mdb->db));
        moo_throw(moo_error(msg));
        return moo_none();
    }
    char berr[256];
    if (!bind_list_params(stmt, params, berr, sizeof(berr))) {
        sqlite3_finalize(stmt);
        char msg[1024];
        snprintf(msg, sizeof(msg), "db_ausfuehren_mit_params: %s (SQL: '%s')", berr, sql_str);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        char msg[1024];
        format_sql_error(msg, sizeof(msg), sql_str, sqlite3_errmsg(mdb->db));
        sqlite3_finalize(stmt);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    int changes = sqlite3_changes(mdb->db);
    sqlite3_finalize(stmt);
    return moo_number((double)changes);
}

MooValue moo_db_query_with_params(MooValue db, MooValue sql, MooValue params) {
    MooDatabase* mdb = check_db(db, "db_abfrage_mit_params");
    if (!mdb) return moo_none();
    if (sql.tag != MOO_STRING) {
        moo_throw(moo_error("DB-Fehler in db_abfrage_mit_params: SQL muss ein String sein"));
        return moo_none();
    }
    const char* sql_str = MV_STR(sql)->chars;
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(mdb->db, sql_str, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        char msg[1024];
        format_sql_error(msg, sizeof(msg), sql_str, sqlite3_errmsg(mdb->db));
        moo_throw(moo_error(msg));
        return moo_none();
    }
    char berr[256];
    if (!bind_list_params(stmt, params, berr, sizeof(berr))) {
        sqlite3_finalize(stmt);
        char msg[1024];
        snprintf(msg, sizeof(msg), "db_abfrage_mit_params: %s (SQL: '%s')", berr, sql_str);
        moo_throw(moo_error(msg));
        return moo_none();
    }

    MooValue result_list = moo_list_new(8);
    int col_count = sqlite3_column_count(stmt);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        MooValue row = moo_dict_new();
        for (int i = 0; i < col_count; i++) {
            const char* col_name = sqlite3_column_name(stmt, i);
            MooValue key = moo_string_new(col_name);
            MooValue val;
            int col_type = sqlite3_column_type(stmt, i);
            switch (col_type) {
                case SQLITE_INTEGER: val = moo_number((double)sqlite3_column_int64(stmt, i)); break;
                case SQLITE_FLOAT:   val = moo_number(sqlite3_column_double(stmt, i)); break;
                case SQLITE_TEXT:    val = moo_string_new((const char*)sqlite3_column_text(stmt, i)); break;
                case SQLITE_NULL:    val = moo_none(); break;
                case SQLITE_BLOB:    val = moo_string_new("<BLOB>"); break;
                default:             val = moo_none(); break;
            }
            moo_dict_set(row, key, val);
        }
        moo_list_append(result_list, row);
    }
    if (rc != SQLITE_DONE) {
        char msg[1024];
        format_sql_error(msg, sizeof(msg), sql_str, sqlite3_errmsg(mdb->db));
        sqlite3_finalize(stmt);
        moo_throw(moo_error(msg));
        return moo_none();
    }
    sqlite3_finalize(stmt);
    return result_list;
}
