#ifndef MOO_GLFW_PLATFORM_H
#define MOO_GLFW_PLATFORM_H

/**
 * moo_glfw_platform.h — Plattformwahl fuer GLFW vor glfwInit() (nach <GLFW/glfw3.h> einbinden).
 *
 * Sicherheits-Gate (claude-erde, 2026-09-22): GLFW 3.4 waehlt unter Linux
 * Wayland auch dann, wenn WAYLAND_DISPLAY fehlt — libwayland faellt dann auf
 * $XDG_RUNTIME_DIR/wayland-0 zurueck, also auf den echten Desktop. Ein
 * Xvfb-Lauf (DISPLAY=:N ohne WAYLAND_DISPLAY) oeffnete so ein Fenster auf dem
 * Desktop des Users. Regel: ohne WAYLAND_DISPLAY wird X11 erzwungen.
 * MOO_GLFW_PLATFORM=x11|wayland ueberschreibt die Wahl ausdruecklich.
 */

#include <stdlib.h>
#include <string.h>

static inline void moo_glfw_platform_hint(void) {
#if (defined(__linux__) || defined(__FreeBSD__)) && defined(GLFW_PLATFORM) && defined(GLFW_PLATFORM_X11) && defined(GLFW_PLATFORM_WAYLAND)
    const char* force = getenv("MOO_GLFW_PLATFORM");
    const char* wl = getenv("WAYLAND_DISPLAY");
    if (force && strcmp(force, "wayland") == 0) {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
    } else if ((force && strcmp(force, "x11") == 0) || !wl || !wl[0]) {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    }
#endif
}

#endif
