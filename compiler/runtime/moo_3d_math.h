#ifndef MOO_3D_MATH_H
#define MOO_3D_MATH_H

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_MATRIX_DEPTH 32

typedef struct {
    float stack[MAX_MATRIX_DEPTH][16];
    int depth;
    float current[16];
} MooMatrixStack;

/* Matrix operations (column-major, OpenGL-compatible layout) */
void mat4_identity(float* m);
void mat4_multiply(float* result, const float* a, const float* b);
void mat4_perspective(float* m, float fov_deg, float aspect, float near_val, float far_val);
void mat4_lookat(float* m,
                 float ex, float ey, float ez,
                 float lx, float ly, float lz,
                 float ux, float uy, float uz);
void mat4_translate(float* m, float x, float y, float z);
void mat4_rotate(float* m, float angle_deg, float ax, float ay, float az);

/* Software matrix stack (for backends without built-in stack) */
void moo_matrix_stack_init(MooMatrixStack* s);
void moo_matrix_stack_push(MooMatrixStack* s);
void moo_matrix_stack_pop(MooMatrixStack* s);

#endif
