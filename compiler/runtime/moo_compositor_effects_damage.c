#include "moo_compositor_effects_damage.h"

#define MOO_COMP_DAMAGE_FNV_OFFSET UINT64_C(1469598103934665603)
#define MOO_COMP_DAMAGE_FNV_PRIME UINT64_C(1099511628211)

static int moo_comp_damage_empty(MooCompRect rect) {
    return rect.width == 0 || rect.height == 0;
}

static int moo_comp_damage_ranges_overlap(
    const MooCompRect *a, uint32_t a_capacity,
    const MooCompRect *b, uint32_t b_capacity) {
    uint64_t a_bytes =
        (uint64_t)a_capacity * (uint64_t)sizeof(MooCompRect);
    uint64_t b_bytes =
        (uint64_t)b_capacity * (uint64_t)sizeof(MooCompRect);
    uintptr_t a_begin = (uintptr_t)a;
    uintptr_t b_begin = (uintptr_t)b;
    uintptr_t a_end;
    uintptr_t b_end;

    if (a_bytes > (uint64_t)UINTPTR_MAX - (uint64_t)a_begin ||
        b_bytes > (uint64_t)UINTPTR_MAX - (uint64_t)b_begin)
        return 1;
    a_end = a_begin + (uintptr_t)a_bytes;
    b_end = b_begin + (uintptr_t)b_bytes;
    return a_begin < b_end && b_begin < a_end;
}

static MooCompResult moo_comp_damage_rect_valid(MooCompRect rect) {
    MooCompRect checked;
    return moo_comp_effect_rect_expand(rect, 0, &checked);
}

static MooCompResult moo_comp_damage_clip(
    MooCompRect full, MooCompRect rect, MooCompRect *out) {
    return moo_comp_effect_rect_intersect(full, rect, out);
}

static int moo_comp_damage_add(
    MooCompRect rect, MooCompRect *regions,
    uint32_t capacity, uint32_t *count) {
    if (moo_comp_damage_empty(rect)) return 1;
    if (*count >= capacity) return 0;
    regions[*count] = rect;
    ++*count;
    return 1;
}

static void moo_comp_damage_collapse(
    MooCompRect full, MooCompRect *regions,
    uint32_t *count, uint32_t *full_damage) {
    regions[0] = full;
    *count = 1u;
    *full_damage = 1u;
}

MooCompResult moo_comp_effect_damage_propagate_backdrop(
    MooCompRect lower_z_dirty,
    MooCompRect backdrop_coverage,
    uint16_t kernel_radius,
    MooCompRect *out_damage) {
    MooCompRect expanded;
    MooCompRect clipped;
    MooCompResult result;
    if (!out_damage) return MOO_COMP_INVALID;
    result = moo_comp_damage_rect_valid(lower_z_dirty);
    if (result != MOO_COMP_OK) return result;
    result = moo_comp_damage_rect_valid(backdrop_coverage);
    if (result != MOO_COMP_OK) return result;
    result = moo_comp_effect_rect_expand(
        lower_z_dirty, (int32_t)kernel_radius, &expanded);
    if (result != MOO_COMP_OK) return result;
    result = moo_comp_effect_rect_intersect(
        expanded, backdrop_coverage, &clipped);
    if (result != MOO_COMP_OK) return result;
    *out_damage = clipped;
    return MOO_COMP_OK;
}

static MooCompResult moo_comp_damage_validate_surface(
    const MooCompEffectDamageSurface *surface) {
    MooCompResult result;
    if (!surface || surface->surface == MOO_COMP_HANDLE_INVALID ||
        surface->reserved16 != 0u || surface->reserved != 0u ||
        surface->changed > 1u ||
        (surface->enabled_mask & ~(uint64_t)MOO_COMP_EFFECTS_V2) != 0u)
        return MOO_COMP_INVALID;
    if ((surface->enabled_mask & MOO_COMP_EFFECT_BACKDROP_BLUR) == 0u &&
        surface->backdrop_blur_radius != 0u)
        return MOO_COMP_INVALID;
    result = moo_comp_damage_rect_valid(surface->old_visual_bounds);
    if (result != MOO_COMP_OK) return result;
    result = moo_comp_damage_rect_valid(surface->new_visual_bounds);
    if (result != MOO_COMP_OK) return result;
    return moo_comp_damage_rect_valid(surface->backdrop_coverage_bounds);
}

MooCompResult moo_comp_effect_damage_build(
    MooCompRect full_output,
    const MooCompRect *background_damage,
    uint32_t background_damage_count,
    const MooCompEffectDamageSurface *surfaces,
    uint32_t surface_count,
    MooCompEffectDamageWorkspace *workspace,
    MooCompEffectDamageOutput *out_damage) {
    MooCompResult result;
    uint32_t i;
    uint32_t count = 0u;
    uint32_t full_damage = 0u;

    if (!workspace || !out_damage || !workspace->regions ||
        workspace->capacity == 0u || workspace->reserved != 0u ||
        !out_damage->regions || out_damage->capacity == 0u ||
        out_damage->reserved != 0u ||
        (background_damage_count != 0u && !background_damage) ||
        (surface_count != 0u && !surfaces) ||
        moo_comp_damage_ranges_overlap(
            workspace->regions, workspace->capacity,
            out_damage->regions, out_damage->capacity))
        return MOO_COMP_INVALID;
    result = moo_comp_damage_rect_valid(full_output);
    if (result != MOO_COMP_OK || moo_comp_damage_empty(full_output))
        return MOO_COMP_INVALID;

    for (i = 0u; i < background_damage_count; ++i) {
        result = moo_comp_damage_rect_valid(background_damage[i]);
        if (result != MOO_COMP_OK) return result;
    }
    for (i = 0u; i < surface_count; ++i) {
        result = moo_comp_damage_validate_surface(&surfaces[i]);
        if (result != MOO_COMP_OK) return result;
    }

    for (i = 0u; i < background_damage_count && !full_damage; ++i) {
        MooCompRect clipped;
        result = moo_comp_damage_clip(
            full_output, background_damage[i], &clipped);
        if (result != MOO_COMP_OK) return result;
        if (!moo_comp_damage_add(
                clipped, workspace->regions, workspace->capacity, &count))
            moo_comp_damage_collapse(
                full_output, workspace->regions, &count, &full_damage);
    }

    for (i = 0u; i < surface_count && !full_damage; ++i) {
        const MooCompEffectDamageSurface *surface = &surfaces[i];
        uint32_t lower_count = count;
        uint32_t dirty_index;

        if ((surface->enabled_mask &
             MOO_COMP_EFFECT_BACKDROP_BLUR) != 0u) {
            for (dirty_index = 0u;
                 dirty_index < lower_count && !full_damage;
                 ++dirty_index) {
                MooCompRect propagated;
                result = moo_comp_effect_damage_propagate_backdrop(
                    workspace->regions[dirty_index],
                    surface->backdrop_coverage_bounds,
                    surface->backdrop_blur_radius,
                    &propagated);
                if (result != MOO_COMP_OK) return result;
                if (!moo_comp_damage_add(
                        propagated, workspace->regions,
                        workspace->capacity, &count))
                    moo_comp_damage_collapse(
                        full_output, workspace->regions,
                        &count, &full_damage);
            }
        }

        if (surface->changed && !full_damage) {
            MooCompRect changed;
            MooCompRect clipped;
            result = moo_comp_effect_rect_union(
                surface->old_visual_bounds,
                surface->new_visual_bounds, &changed);
            if (result != MOO_COMP_OK) return result;
            result = moo_comp_damage_clip(full_output, changed, &clipped);
            if (result != MOO_COMP_OK) return result;
            if (!moo_comp_damage_add(
                    clipped, workspace->regions,
                    workspace->capacity, &count))
                moo_comp_damage_collapse(
                    full_output, workspace->regions,
                    &count, &full_damage);
        }
    }

    if (count > out_damage->capacity)
        moo_comp_damage_collapse(
            full_output, workspace->regions, &count, &full_damage);

    for (i = 0u; i < count; ++i)
        out_damage->regions[i] = workspace->regions[i];
    out_damage->count = count;
    out_damage->full_damage = full_damage;
    out_damage->reserved = 0u;
    return MOO_COMP_OK;
}

static uint64_t moo_comp_damage_mix_u32(uint64_t hash, uint32_t value) {
    uint32_t i;
    for (i = 0u; i < 4u; ++i) {
        hash ^= (uint8_t)(value >> (i * 8u));
        hash *= MOO_COMP_DAMAGE_FNV_PRIME;
    }
    return hash;
}

uint64_t moo_comp_effect_damage_hash(
    const MooCompEffectDamageOutput *damage) {
    uint64_t hash = MOO_COMP_DAMAGE_FNV_OFFSET;
    uint32_t i;
    if (!damage || !damage->regions ||
        damage->count > damage->capacity ||
        damage->full_damage > 1u || damage->reserved != 0u)
        return 0u;
    hash = moo_comp_damage_mix_u32(hash, damage->count);
    hash = moo_comp_damage_mix_u32(hash, damage->full_damage);
    for (i = 0u; i < damage->count; ++i) {
        hash = moo_comp_damage_mix_u32(
            hash, (uint32_t)damage->regions[i].x);
        hash = moo_comp_damage_mix_u32(
            hash, (uint32_t)damage->regions[i].y);
        hash = moo_comp_damage_mix_u32(
            hash, (uint32_t)damage->regions[i].width);
        hash = moo_comp_damage_mix_u32(
            hash, (uint32_t)damage->regions[i].height);
    }
    return hash;
}
