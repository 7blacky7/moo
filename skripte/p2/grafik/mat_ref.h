#ifndef P2_MAT_REF_H
#define P2_MAT_REF_H
/* Matrices use column-major storage, column vectors; output may alias input. */
void p2_mat4_identity(float out[16]);
void p2_mat4_mul(float out[16], const float a[16], const float b[16]);
void p2_mat4_vec4(float out[4], const float m[16], const float v[4]);
/* P2-T89: dieselbe Operationsfolge in f64 (dmat4/dvec4). */
void p2_dmat4_mul(double out[16], const double a[16], const double b[16]);
void p2_dmat4_vec4(double out[4], const double m[16], const double v[4]);
#endif
