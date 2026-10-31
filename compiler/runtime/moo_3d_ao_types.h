#ifndef MOO_3D_AO_TYPES_H
#define MOO_3D_AO_TYPES_H
/* Per-context, opt-in resources. 11 bytes/full pixel plus odd-half rounding. */
typedef struct {
    int on, debug, opaque_done, view_locked;
    float radius, strength, bias, projection[16], view[16];
    GLuint ambient, tex[3], fbo[3], composite_fbo, program[5];
    int w, h;
    GLuint scene_fbo, scene_color;
} GL33AOState;
#endif
