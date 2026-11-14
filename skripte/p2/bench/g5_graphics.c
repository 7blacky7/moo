#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "mat_ref.h"
#include "mesh_ref.h"
static double now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) exit(2);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
int main(void) {
    const size_t n = 300000;
    P2Vertex *v = calloc(n, sizeof(*v));
    uint32_t *indices = malloc(n * sizeof(*indices));
    float *out = malloc(n * 4 * sizeof(*out));
    if (!v || !indices || !out) return 3;
    for (size_t i=0; i<n; ++i) {
        v[i].position[0]=(float)(i%100);
        v[i].position[1]=2.0f; v[i].position[2]=3.0f; indices[i]=(uint32_t)i;
    }
    float m[16], p[4], w[4]; p2_mat4_identity(m); m[0]=2; m[5]=3; m[10]=4;
    double start=now(), sum=0;
    for (size_t i=0; i<1000000; ++i) {
        p[0]=(float)(i%100); p[1]=2; p[2]=3; p[3]=1;
        p2_mat4_vec4(w,m,p);
        sum+=(double)w[0]+w[1]+w[2]+w[3];
    }
    double matrix_s=now()-start;
    P2Mesh *mesh=NULL;
    start=now();
    P2MeshStatus rc=p2_mesh_create(&mesh,v,n,indices,n);
    double create_s=now()-start;
    if (rc != P2_MESH_OK || !mesh) return 4;
    start=now();
    rc=p2_mesh_transform(mesh,m,out,n*4);
    double transform_s=now()-start, check=0;
    if (rc != P2_MESH_OK) return 5;
    for(size_t i=0;i<n*4;++i) check+=out[i];
    printf("%.9f\n%.0f\n%.9f\n%.9f\n%.0f\n",matrix_s,sum,create_s,transform_s,check);
    p2_mesh_destroy(mesh); free(v); free(indices); free(out);
    return sum==118000000.0 && check==35400000.0 ? 0 : 6;
}
