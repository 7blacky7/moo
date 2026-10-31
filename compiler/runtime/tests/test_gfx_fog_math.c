/* Independent quadrature and transport identities, no graphics context needed. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../moo_3d_fog_math.h"

#define abs fabsf
#define min fminf
#define exp expf
#define log logf
MOO_FOG_TAU_FUNCTION
#undef abs
#undef min
#undef exp
#undef log

static unsigned checks;
static void close_to(const char *name, double actual, double expected) {
    ++checks;
    double tolerance = 3e-5 * fabs(expected) + 2e-7;
    if (!isfinite(actual) || fabs(actual - expected) > tolerance) {
        fprintf(stderr, "%s: got %.12g expected %.12g tolerance %.12g\n",
                name, actual, expected, tolerance);
        exit(1);
    }
}
static void require(const char *name, int condition) {
    ++checks;
    if (!condition) { fprintf(stderr, "FAIL %s\n", name); exit(1); }
}
/* Composite Simpson integration of the defining density, deliberately not the
 * closed form used by the production kernel. Inputs are rounded to float first. */
static double quadrature(float d, float len, float y0, float y1, float h, float k) {
    const int n = 16384;
    double sum = 0;
    for (int i = 0; i <= n; ++i) {
        double t = (double)i / n;
        double y = (double)y0 + ((double)y1 - y0) * t;
        double rho = (double)d * exp(-(double)k * (y - h));
        sum += (i == 0 || i == n ? 1 : (i & 1 ? 4 : 2)) * rho;
    }
    return fmin(80.0, (double)len * sum / (3.0 * n));
}
int main(void) {
    const float ys[] = {-8, 0, 0.00001f, 1, 12, 80};
    const float ks[] = {0, 0.000001f, 0.01f, 0.04999f, 0.05f, 0.05001f, 0.4f, 2};
    const float ds[] = {0, 0.00001f, 0.001f, 0.2f};
    for (unsigned i = 0; i < sizeof ys / sizeof *ys; ++i)
    for (unsigned j = 0; j < sizeof ys / sizeof *ys; ++j)
    for (unsigned q = 0; q < sizeof ks / sizeof *ks; ++q)
    for (unsigned p = 0; p < sizeof ds / sizeof *ds; ++p) {
        float len = 2 + fabsf(ys[j] - ys[i]);
        float tau = moo_fog_tau(ds[p], len, ys[i], ys[j], 2, ks[q]);
        close_to("quadrature", tau, quadrature(ds[p],len,ys[i],ys[j],2,ks[q]));
        close_to("reciprocity", moo_fog_tau(ds[p],len,ys[j],ys[i],2,ks[q]),tau);
        require("T in [0,1]", expf(-tau) >= 0 && expf(-tau) <= 1);
        if (tau < 60) {
            for (int split = 1; split < 4; ++split) {
                float t = 0.25f * split, mid = ys[i] + (ys[j] - ys[i]) * t;
                double first = moo_fog_tau(ds[p],len*t,ys[i],mid,2,ks[q]);
                double second = moo_fog_tau(ds[p],len*(1-t),mid,ys[j],2,ks[q]);
                close_to("path additivity", first+second,tau);
                close_to("T multiplicativity", exp(-first)*exp(-second),exp(-tau));
                require("append path nondecreasing", first <= tau + 2e-5*fabs(tau)+1e-7);
            }
        }
    }
    close_to("zero density exact",moo_fog_tau(0,1e6f,-1e6f,1e6f,0,1000),0);
    close_to("zero length exact",moo_fog_tau(1e6f,0,-1e6f,1e6f,0,1000),0);
    close_to("constant density",moo_fog_tau(.02f,100,800,-800,0,0),2);
    close_to("horizontal",moo_fog_tau(.1f,50,3,3,2,.4f),5*exp(-.4));
    close_to("opaque extreme",moo_fog_tau(1e6f,1e6f,-1e6f,1e6f,0,1000),80);
    close_to("clear extreme",moo_fog_tau(1e-20f,1e-10f,1e6f,1e6f,0,1000),0);
    /* Distances below the former legacy .001 density floor remain distinct. */
    require("small positive density is not clamped",
            moo_fog_tau(.0001f,100,0,0,0,0) < moo_fog_tau(.001f,100,0,0,0,0));
    puts(MOO_FOG_TAU_GLSL[0] ? "PASS shader source generated" : "FAIL empty shader source");
    printf("PASS fog math: %u checks\n",checks);
    return 0;
}
