/* P2-T44: Runtime-Ausgabe bricht nach einem Konvertierungsfehler ab.
 * Gelinkt werden nur moo_print.c und moo_error.c; Strings und Verkettung sind
 * hier nachgebaut (Semantik wie moo_string_concat in moo_string.c), damit die
 * Verkettung ab einer kleinen Laengengrenze einen echten moo_throw ausloest:
 * Fehlerinjektion ueber den Produktpfad, ohne Hook im Produktcode. Geprueft:
 * genau ein Wurf (Erstfehler bleibt), keine Ausgabe nach dem Fehler,
 * Zwischenwerte genau einmal frei, und ein beim Eintritt gesetztes Flag
 * (unbehandelter Wurf mit MOO_STRENG=1) unterdrueckt die Fehlermeldung nicht. */
#define _POSIX_C_SOURCE 200809L
#include "../moo_runtime.h"
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int live_strings;
static int wuerfe;
static int32_t grenze = INT32_MAX;

MooValue moo_none(void) { return (MooValue){MOO_NONE, 0}; }
void moo_retain(MooValue v) { if (v.tag == MOO_STRING) MV_STR(v)->refcount++; }
void moo_release(MooValue v) {
    if (v.tag != MOO_STRING) return;
    MooString* s = MV_STR(v);
    assert(s->refcount > 0);
    if (--s->refcount == 0) { free(s->chars); free(s); --live_strings; }
}
MooValue moo_string_new_len(const char* chars, int32_t len) {
    MooString* s = malloc(sizeof(*s));
    assert(s);
    s->refcount = 1;
    s->length = len;
    s->capacity = len + 1;
    s->chars = malloc((size_t)len + 1);
    assert(s->chars);
    if (len > 0) memcpy(s->chars, chars, (size_t)len);
    s->chars[len] = '\0';
    ++live_strings;
    MooValue v = {MOO_STRING, 0};
    moo_val_set_ptr(&v, s);
    return v;
}
MooValue moo_string_new(const char* chars) { return moo_string_new_len(chars, (int32_t)strlen(chars)); }
MooValue moo_tensor_to_string(MooValue tv) { (void)tv; return moo_string_new("<Tensor>"); }
/* moo_print.c gibt Griffe (P2-T53) und Strukturwerte (P2-T49) aus; hier ungenutzt. */
const char* moo_handle_text(MooValue v) { (void)v; return "<Griff>"; }
MooValue moo_struct_text(MooValue v) { (void)v; return moo_string_new("<Struktur>"); }

/* Wie moo_string_concat im Produkt, nur mit einstellbarer Laengengrenze. */
MooValue moo_string_concat(MooValue a, MooValue b) {
    MooValue sa = moo_to_string(a);
    MooValue sb = moo_to_string(b);
    int32_t la = MV_STR(sa)->length, lb = MV_STR(sb)->length;
    if ((int64_t)la + lb > grenze) {
        ++wuerfe;
        moo_throw(moo_string_new(wuerfe == 1 ? "Erstfehler" : "Folgefehler"));
    }
    if (moo_error_flag) {
        if (a.tag != MOO_STRING) moo_release(sa);
        if (b.tag != MOO_STRING) moo_release(sb);
        return moo_string_new("");
    }
    char* buf = malloc((size_t)la + (size_t)lb + 1);
    assert(buf);
    memcpy(buf, MV_STR(sa)->chars, (size_t)la);
    memcpy(buf + la, MV_STR(sb)->chars, (size_t)lb);
    MooValue r = moo_string_new_len(buf, la + lb);
    free(buf);
    if (a.tag != MOO_STRING) moo_release(sa);
    if (b.tag != MOO_STRING) moo_release(sb);
    return r;
}

static MooValue zahl(double d) { MooValue v = {MOO_NUMBER, 0}; moo_val_set_double(&v, d); return v; }

static char ausgabe[256];
/* Fuehrt f(arg) mit umgeleiteter Standardausgabe aus; '#' markiert das Ende. */
static const char* gefangen(void (*f)(MooValue), MooValue arg) {
    int fd[2];
    assert(pipe(fd) == 0);
    fflush(stdout);
    int alt = dup(1);
    assert(alt >= 0 && dup2(fd[1], 1) == 1);
    f(arg);
    fflush(stdout);
    assert(dup2(alt, 1) == 1);
    close(alt);
    assert(write(fd[1], "#", 1) == 1);
    close(fd[1]);
    ssize_t n = read(fd[0], ausgabe, sizeof(ausgabe) - 1);
    assert(n > 0);
    ausgabe[n] = '\0';
    close(fd[0]);
    return ausgabe;
}

int main(void) {
    /* Liste [12, "ab", 34]: ohne Grenze normale Ausgabe. */
    MooValue items[3] = { zahl(12), moo_string_new("ab"), zahl(34) };
    MooList liste = { .refcount = 1, .frozen = false, .items = items, .length = 3, .capacity = 3 };
    MooValue lv = {MOO_LIST, 0};
    moo_val_set_ptr(&lv, &liste);
    assert(strcmp(gefangen(moo_print, lv), "[12, \"ab\", 34]\n#") == 0);
    assert(live_strings == 1 && wuerfe == 0);

    /* Grenze 6: "[12" + ", " + "\"" passt, + "ab" wirft. Danach kein weiterer
     * Wurf (Erstfehler bleibt), keine Ausgabe, alle Zwischenwerte frei. */
    moo_try_enter();
    grenze = 6;
    assert(strcmp(gefangen(moo_print, lv), "#") == 0);
    assert(wuerfe == 1 && moo_try_check() == 1);
    MooValue fehler = moo_try_take_error();
    assert(fehler.tag == MOO_STRING && strcmp(MV_STR(fehler)->chars, "Erstfehler") == 0);
    moo_release(fehler);
    assert(live_strings == 1);

    /* Direkte Konvertierung: leeres (+1) Ergebnis, ebenfalls genau ein Wurf. */
    wuerfe = 0;
    MooValue leer = moo_to_string(lv);
    assert(wuerfe == 1 && moo_try_check() == 1 && MV_STR(leer)->length == 0);
    moo_release(leer);
    moo_release(moo_try_take_error());
    assert(live_strings == 1);

    /* Dict {"k": 5, "l": 6}: Fehler beim Trenner vor dem zweiten Eintrag. */
    MooValue k1 = moo_string_new("k"), k2 = moo_string_new("l");
    MooDictEntry eintraege[4] = {
        { .key = MV_STR(k1), .value = zahl(5), .occupied = true, .deleted = false },
        { .key = NULL, .value = {MOO_NONE, 0}, .occupied = false, .deleted = false },
        { .key = MV_STR(k2), .value = zahl(6), .occupied = true, .deleted = false },
        { .key = NULL, .value = {MOO_NONE, 0}, .occupied = false, .deleted = false },
    };
    MooDict dict = { .refcount = 1, .frozen = false, .entries = eintraege, .count = 2, .tombs = 0, .capacity = 4 };
    MooValue dv = {MOO_DICT, 0};
    moo_val_set_ptr(&dv, &dict);
    grenze = INT32_MAX;
    wuerfe = 0;
    assert(strcmp(gefangen(moo_print, dv), "{\"k\": 5, \"l\": 6}\n#") == 0);
    grenze = 8;
    wuerfe = 0;
    assert(strcmp(gefangen(moo_print, dv), "#") == 0);
    assert(wuerfe == 1 && moo_try_check() == 1);
    moo_release(moo_try_take_error());
    moo_try_leave();
    assert(moo_try_depth == 0 && live_strings == 3);
    moo_release(items[1]);
    moo_release(k1);
    moo_release(k2);
    assert(live_strings == 0);

    /* Unbehandelt mit MOO_STRENG=1 (P2-T95): moo_throw druckt die Meldung ueber
     * moo_print, obwohl das Flag beim Eintritt schon gesetzt ist; Exitcode 1. */
    int fd[2];
    assert(pipe(fd) == 0);
    pid_t kind = fork();
    assert(kind >= 0);
    if (kind == 0) {
        close(fd[0]);
        dup2(fd[1], 1);
        int nichts = open("/dev/null", O_WRONLY);
        if (nichts >= 0) dup2(nichts, 2);
        setenv("MOO_STRENG", "1", 1);
        grenze = INT32_MAX;
        moo_throw(moo_string_new("Unbehandelt-Erstfehler"));
        _exit(99);
    }
    close(fd[1]);
    char puffer[128];
    ssize_t n = read(fd[0], puffer, sizeof(puffer) - 1);
    assert(n > 0);
    puffer[n] = '\0';
    close(fd[0]);
    int status = 0;
    assert(waitpid(kind, &status, 0) == kind);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    assert(strcmp(puffer, "Unbehandelt-Erstfehler\n") == 0);
    return 0;
}
