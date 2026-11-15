#define _POSIX_C_SOURCE 200809L /* setenv */
#include "../moo_runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct { int refs; } Fake;
static int live_objects;

MooValue moo_none(void) { return (MooValue){MOO_NONE, 0}; }
void moo_print(MooValue v) { (void)v; }
void moo_retain(MooValue v) {
    if (v.tag == MOO_STRING) ((Fake*)moo_val_as_ptr(v))->refs++;
}
void moo_release(MooValue v) {
    if (v.tag == MOO_STRING) {
        Fake* p = moo_val_as_ptr(v);
        assert(p->refs > 0);
        if (--p->refs == 0) { --live_objects; free(p); }
    }
}
static MooValue make_error(void) {
    Fake* p = malloc(sizeof(*p));
    assert(p);
    p->refs = 1;
    ++live_objects;
    MooValue v = {MOO_STRING, 0};
    moo_val_set_ptr(&v, p);
    return v;
}
/* moo_native_fehler (moo_error.c) erzeugt seine Meldung per moo_string_new;
   der Stub zaehlt sie wie jeden anderen Fehlerwert. */
MooValue moo_string_new(const char* chars) { (void)chars; return make_error(); }
/* P2-T95: moo_error.c formatiert Hinweistexte ueber moo_to_string; dieser Test
   loest nur den strengen Pfad aus (MOO_STRENG=1), der keinen Text formatiert. */
MooValue moo_to_string(MooValue v) { (void)v; assert(!"moo_to_string unerwartet"); return moo_none(); }

int main(void) {
    assert(moo_try_take_error().tag == MOO_NONE);
    moo_try_enter();
    moo_try_enter();
    assert(moo_try_depth == 2);
    MooValue first = make_error();
    moo_throw(first);
    assert(moo_try_check() == 1);
    MooValue held = moo_try_take_error();
    assert(held.tag == MOO_STRING && held.data == first.data);
    assert(moo_try_check() == 0 && moo_try_depth == 2 && live_objects == 1);
    assert(moo_try_take_error().tag == MOO_NONE);
    moo_try_leave();
    assert(moo_try_depth == 1 && live_objects == 1);
    MooValue second = make_error();
    moo_throw(second);
    assert(moo_try_check() == 1 && live_objects == 2);
    MooValue outer = moo_try_take_error();
    assert(outer.data == second.data && moo_try_check() == 0);
    moo_try_leave();
    assert(moo_try_depth == 0 && live_objects == 2);
    moo_release(held);
    moo_release(outer);
    assert(live_objects == 0);

    /* Preserve the first error across a synthetic cleanup handler. */
    moo_try_enter();
    MooValue first_error = make_error();
    moo_throw(first_error);
    MooValue saved = moo_try_take_error();
    int has_first = 1;
    moo_try_enter();
    moo_throw(make_error());
    MooValue later = moo_try_take_error();
    if (!has_first) { saved = later; has_first = 1; } else moo_release(later);
    assert(live_objects == 1 && !moo_try_check());
    moo_try_leave();
    moo_throw(saved); /* transfers the saved +1 to the outer handler */
    assert(moo_try_check());
    MooValue propagated = moo_try_take_error();
    assert(propagated.data == first_error.data);
    moo_try_leave();
    moo_release(propagated);
    assert(live_objects == 0);

    /* NONE itself can be thrown: a separate flag carries its presence. */
    moo_try_enter();
    moo_throw(moo_none());
    assert(moo_try_check());
    saved = moo_try_take_error();
    has_first = 1;
    moo_try_enter();
    moo_throw(make_error());
    later = moo_try_take_error();
    if (!has_first) { saved = later; has_first = 1; } else moo_release(later);
    moo_try_leave();
    moo_throw(saved);
    assert(moo_try_check() && moo_try_take_error().tag == MOO_NONE);
    moo_try_leave();
    assert(live_objects == 0);

    /* P2-T58 A3: erster Fehler gewinnt. Ein Folgefehler eines nativen Aufrufers,
       der mit dem Nullwert des gescheiterten Callees weiterrechnet, legt keine
       neue Meldung an und ueberschreibt die erste nicht. */
    moo_try_enter();
    moo_native_fehler("erster");
    assert(moo_try_check() && live_objects == 1);
    MooValue erster = moo_last_error;
    moo_native_fehler("zweiter");
    assert(moo_try_check() && live_objects == 1 && moo_last_error.data == erster.data);
    MooValue gewinner = moo_try_take_error();
    assert(gewinner.data == erster.data && !moo_try_check());
    moo_release(gewinner);
    /* Nach dem Abholen gilt wieder: ein neuer Fehler wird gemeldet. */
    moo_native_fehler("danach");
    assert(moo_try_check() && live_objects == 1);
    moo_release(moo_try_take_error());
    moo_try_leave();
    assert(moo_try_depth == 0 && live_objects == 0);

    /* With no outer handler, cleanup completes before rethrow exits. */
    int fd[2];
    assert(pipe(fd) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(fd[0]);
        moo_try_enter();
        moo_throw(make_error());
        MooValue saved_child = moo_try_take_error();
        moo_throw(make_error());
        MooValue later_child = moo_try_take_error();
        moo_release(later_child);
        moo_try_leave();
        assert(write(fd[1], "C", 1) == 1);
        close(fd[1]);
        /* P2-T95: ohne Handler bricht nur MOO_STRENG=1 ab (Standard: Hinweis). */
        setenv("MOO_STRENG", "1", 1);
        moo_throw(saved_child); /* no outer handler: exits with code 1 */
        _exit(99);
    }
    close(fd[1]);
    char marker = 0;
    assert(read(fd[0], &marker, 1) == 1 && marker == 'C');
    close(fd[0]);
    int child_status = 0;
    assert(waitpid(child, &child_status, 0) == child);
    assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 1);
    assert(moo_try_depth == 0 && live_objects == 0);
    return 0;
}
