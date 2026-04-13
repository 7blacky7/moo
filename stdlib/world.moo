# ============================================================
# stdlib/world.moo — World Module (English)
#
# Creates procedural 3D worlds with a single call.
# Usage: import world
#
# Example:
#   let w = world_create("My World", 800, 600)
#   world_seed(w, 42)
#   while world_is_open(w):
#       world_update(w)
#   world_close(w)
# ============================================================

# === Core Functions ===

function world_create(title, width, height):
    return __world_create(title, width, height)

function world_is_open(w):
    return __world_is_open(w)

function world_update(w):
    __world_update(w)

function world_close(w):
    __world_close(w)

# === Configuration ===

function world_seed(w, seed):
    __world_seed(w, seed)

function world_biome(w, name, h_min, h_max, color, trees):
    __world_biome(w, name, h_min, h_max, color, trees)

function world_height_at(w, x, z):
    return __world_height_at(w, x, z)

function world_sun(w, x, y, z):
    __world_sun(w, x, y, z)

function world_fog(w, distance):
    __world_fog(w, distance)

function world_sea_level(w, level):
    __world_sea_level(w, level)

function world_render_dist(w, distance):
    __world_render_dist(w, distance)
