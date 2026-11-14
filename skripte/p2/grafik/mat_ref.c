#include "mat_ref.h"
#include <string.h>

void p2_mat4_identity(float out[16]) {
    memset(out, 0, 16 * sizeof(*out));
    out[0] = out[5] = out[10] = out[15] = 1.0f;
}
void p2_mat4_mul(float out[16], const float a[16], const float b[16]) {
    float tmp[16];
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            tmp[col*4+row] = a[row]*b[col*4] + a[4+row]*b[col*4+1]
                           + a[8+row]*b[col*4+2] + a[12+row]*b[col*4+3];
    memcpy(out, tmp, sizeof tmp);
}
void p2_mat4_vec4(float out[4], const float m[16], const float v[4]) {
    float tmp[4];
    for (int row = 0; row < 4; ++row)
        tmp[row] = m[row]*v[0] + m[4+row]*v[1]
                 + m[8+row]*v[2] + m[12+row]*v[3];
    memcpy(out, tmp, sizeof tmp);
}
