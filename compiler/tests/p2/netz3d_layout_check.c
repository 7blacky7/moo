/* P2-T25 (Plan 962e249a E3/C1): Eckpunkt (stdlib/netz3d.moos, vom Compiler
 * erzeugter Header) ist bytegleich zu MooVertex (GL33-Vertexformat). */
#include <stddef.h>
#include <stdbool.h>
#include "netz3d_layout.h"
#include "moo_3d_gl33_mesh.h"

_Static_assert(sizeof(Eckpunkt) == sizeof(MooVertex), "Eckpunkt/MooVertex: Groesse");
_Static_assert(sizeof(Eckpunkt) == MOO_VERTEX_STRIDE, "Eckpunkt: Stride");
_Static_assert(offsetof(Eckpunkt, position) == MOO_VERTEX_POS_OFFSET, "position");
_Static_assert(offsetof(Eckpunkt, farbe) == MOO_VERTEX_COLOR_OFFSET, "farbe");
_Static_assert(offsetof(Eckpunkt, normale) == MOO_VERTEX_NORMAL_OFFSET, "normale");
_Static_assert(offsetof(MooVertex, r) == MOO_VERTEX_COLOR_OFFSET, "MooVertex.r");
_Static_assert(offsetof(MooVertex, nx) == MOO_VERTEX_NORMAL_OFFSET, "MooVertex.nx");
int netz3d_layout_ok(void) { return 1; }
