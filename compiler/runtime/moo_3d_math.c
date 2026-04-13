/**
 * moo_3d_math.c — Shared matrix utilities for all 3D backends.
 * No OpenGL dependency. Pure C math.
 */

#include "moo_3d_math.h"
#include <math.h>
#include <string.h>

void mat4_identity(float* m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void mat4_multiply(float* result, const float* a, const float* b) {
    float tmp[16];
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            tmp[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    memcpy(result, tmp, 16 * sizeof(float));
}

void mat4_perspective(float* m, float fov_deg, float aspect, float near_val, float far_val) {
    memset(m, 0, 16 * sizeof(float));
    float rad = fov_deg * (float)M_PI / 180.0f;
    float tanHalf = tanf(rad / 2.0f);
    m[0]  = 1.0f / (aspect * tanHalf);
    m[5]  = 1.0f / tanHalf;
    m[10] = -(far_val + near_val) / (far_val - near_val);
    m[11] = -1.0f;
    m[14] = -(2.0f * far_val * near_val) / (far_val - near_val);
}

void mat4_lookat(float* m,
                 float ex, float ey, float ez,
                 float lx, float ly, float lz,
                 float upx, float upy, float upz) {
    /* forward = normalize(look - eye) */
    float fx = lx - ex, fy = ly - ey, fz = lz - ez;
    float len = sqrtf(fx*fx + fy*fy + fz*fz);
    if (len > 0) { fx /= len; fy /= len; fz /= len; }

    /* side = normalize(cross(forward, up)) */
    float sx = fy * upz - fz * upy;
    float sy = fz * upx - fx * upz;
    float sz = fx * upy - fy * upx;
    len = sqrtf(sx*sx + sy*sy + sz*sz);
    if (len > 0) { sx /= len; sy /= len; sz /= len; }

    /* recompute up = cross(side, forward) */
    float ux = sy * fz - sz * fy;
    float uy = sz * fx - sx * fz;
    float uz = sx * fy - sy * fx;

    /* column-major: m[col*4 + row] */
    m[0]  =  sx; m[1]  =  ux; m[2]  = -fx; m[3]  = 0;
    m[4]  =  sy; m[5]  =  uy; m[6]  = -fy; m[7]  = 0;
    m[8]  =  sz; m[9]  =  uz; m[10] = -fz; m[11] = 0;
    m[12] = -(sx*ex + sy*ey + sz*ez);
    m[13] = -(ux*ex + uy*ey + uz*ez);
    m[14] =  (fx*ex + fy*ey + fz*ez);
    m[15] = 1.0f;
}

void mat4_translate(float* m, float x, float y, float z) {
    float t[16];
    mat4_identity(t);
    t[12] = x; t[13] = y; t[14] = z;
    float tmp[16];
    mat4_multiply(tmp, m, t);
    memcpy(m, tmp, 16 * sizeof(float));
}

void mat4_rotate(float* m, float angle_deg, float ax, float ay, float az) {
    float rad = angle_deg * (float)M_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    float len = sqrtf(ax*ax + ay*ay + az*az);
    if (len > 0) { ax /= len; ay /= len; az /= len; }

    float r[16];
    mat4_identity(r);
    r[0]  = c + ax*ax*(1-c);
    r[1]  = ay*ax*(1-c) + az*s;
    r[2]  = az*ax*(1-c) - ay*s;
    r[4]  = ax*ay*(1-c) - az*s;
    r[5]  = c + ay*ay*(1-c);
    r[6]  = az*ay*(1-c) + ax*s;
    r[8]  = ax*az*(1-c) + ay*s;
    r[9]  = ay*az*(1-c) - ax*s;
    r[10] = c + az*az*(1-c);

    float tmp[16];
    mat4_multiply(tmp, m, r);
    memcpy(m, tmp, 16 * sizeof(float));
}

void moo_matrix_stack_init(MooMatrixStack* s) {
    s->depth = 0;
    mat4_identity(s->current);
}

void moo_matrix_stack_push(MooMatrixStack* s) {
    if (s->depth >= MAX_MATRIX_DEPTH) return;
    memcpy(s->stack[s->depth], s->current, 16 * sizeof(float));
    s->depth++;
}

void moo_matrix_stack_pop(MooMatrixStack* s) {
    if (s->depth <= 0) return;
    s->depth--;
    memcpy(s->current, s->stack[s->depth], 16 * sizeof(float));
}
