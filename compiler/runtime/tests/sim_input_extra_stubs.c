/* Schwache Stubs fuer test_sim_input_asan: moo_3d.c referenziert seit den
 * Textur-/Wasser-Funktionen SDL_image, Surface- und Dict-Runtime. Der Harness
 * prueft nur die Eingabe-Simulation; diese Pfade werden dort nie aufgerufen.
 * Signaturen sind bewusst generisch (nur zur Symbolaufloesung). */
#if defined(__GNUC__) || defined(__clang__)
#define SIM_WEAK __attribute__((weak))
#else
#define SIM_WEAK
#endif
typedef struct { long long a, b; } SimStubValue;
SIM_WEAK void* IMG_Load(const char* p) { (void)p; return 0; }
SIM_WEAK void* SDL_ConvertSurfaceFormat(void* s, unsigned f, unsigned fl) { (void)s; (void)f; (void)fl; return 0; }
SIM_WEAK void SDL_FreeSurface(void* s) { (void)s; }
SIM_WEAK const unsigned char* moo_surface_rgba(SimStubValue v, int* w, int* h) { (void)v; (void)w; (void)h; return 0; }
SIM_WEAK SimStubValue moo_dict_get(SimStubValue d, SimStubValue k) { (void)d; (void)k; SimStubValue n = {0, 0}; return n; }
SIM_WEAK void moo_release(SimStubValue v) { (void)v; }
