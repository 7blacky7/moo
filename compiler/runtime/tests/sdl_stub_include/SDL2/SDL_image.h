/* Test-Ersatzheader NUR fuer test_sim_input_asan, siehe SDL.h daneben. */
#ifndef MOO_TEST_SDL_IMAGE_STUB_H
#define MOO_TEST_SDL_IMAGE_STUB_H
#include "SDL.h"
SDL_Surface* IMG_Load(const char* file);
#endif
