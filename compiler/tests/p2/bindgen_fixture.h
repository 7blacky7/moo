#ifndef P2_BINDGEN_FIXTURE_H
#define P2_BINDGEN_FIXTURE_H
#define P2_LIMIT 42
#define P2_FLOATING 1.25
typedef struct P2Pair { int x; float y; } P2Pair;
enum P2Mode { P2_ZERO = 0, P2_TWO = 2, P2_THREE };
int p2_add(int left, int right);
double p2_scale(double x, float factor);
void p2_reset(void);
int p2_unsupported_variadic(int n, ...);
P2Pair p2_unsupported_aggregate(P2Pair value);
#endif
