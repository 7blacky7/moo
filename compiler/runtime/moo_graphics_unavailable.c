#include "moo_runtime.h"

#define MOO_FRAME_BACKEND_UNAVAILABLE_MESSAGE \
    "Frame-Backend ist in diesem Build nicht verfuegbar (kein 3D-/SDL-Backend mitgebaut)"

void moo_draw_rect(MooValue win, MooValue x, MooValue y,
                   MooValue w, MooValue h, MooValue color) {
    (void)win;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
    moo_throw(moo_error(MOO_FRAME_BACKEND_UNAVAILABLE_MESSAGE));
}

void moo_draw_circle(MooValue win, MooValue cx, MooValue cy,
                     MooValue r, MooValue color) {
    (void)win;
    (void)cx;
    (void)cy;
    (void)r;
    (void)color;
    moo_throw(moo_error(MOO_FRAME_BACKEND_UNAVAILABLE_MESSAGE));
}

void moo_draw_line(MooValue win, MooValue x1, MooValue y1,
                   MooValue x2, MooValue y2, MooValue color) {
    (void)win;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)color;
    moo_throw(moo_error(MOO_FRAME_BACKEND_UNAVAILABLE_MESSAGE));
}
