#ifndef P2_MAT_REF_H
#define P2_MAT_REF_H
/* Matrices use column-major storage, column vectors; output may alias input. */
void p2_mat4_identity(float out[16]);
void p2_mat4_mul(float out[16], const float a[16], const float b[16]);
void p2_mat4_vec4(float out[4], const float m[16], const float v[4]);
#endif
