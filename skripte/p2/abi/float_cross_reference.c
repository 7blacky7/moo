extern float sinf(float);
extern float powf(float, float);
extern double pow(double, double);
float p2_ref_sinf(float x) { return sinf(x); }
float p2_ref_powf(float x, float y) { return powf(x, y); }
double p2_ref_pow(double x, double y) { return pow(x, y); }
