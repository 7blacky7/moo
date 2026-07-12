#ifndef MOO_COMPOSITOR_EFFECTS_PROTOCOL_H
#define MOO_COMPOSITOR_EFFECTS_PROTOCOL_H

#include <stdint.h>

/*
 * Moo compositor effects protocol V2.
 *
 * All declarations in this file are pointer-free protocol values.  They are
 * not a wire encoding: codecs must encode every field explicitly and must not
 * copy C padding or depend on host byte order, enum width, or alignment.
 */
#define MOO_COMP_EFFECTS_PROTOCOL_VERSION 2u
#define MOO_COMP_Q16_ONE INT32_C(65536)
#define MOO_COMP_SATURATION_ONE UINT16_C(256)
#define MOO_COMP_EFFECT_ANIMATION_VALUE_WORDS 8u

/* Safe defaults for untrusted clients; init may only lower these limits. */
#define MOO_COMP_EFFECT_DEFAULT_MAX_CORNER_RADIUS UINT16_C(4096)
#define MOO_COMP_EFFECT_DEFAULT_MAX_SHADOW_BLUR_RADIUS UINT16_C(64)
#define MOO_COMP_EFFECT_DEFAULT_MAX_SHADOW_SPREAD_RADIUS UINT16_C(64)
#define MOO_COMP_EFFECT_DEFAULT_MAX_BACKDROP_BLUR_RADIUS UINT16_C(32)
#define MOO_COMP_EFFECT_DEFAULT_MAX_SATURATION_Q8_8 UINT16_C(1024)
#define MOO_COMP_EFFECT_DEFAULT_MAX_ABS_SHADOW_OFFSET INT32_C(4096)
#define MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_COEFFICIENT \
    INT32_C(1048576)
#define MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_TRANSLATION \
    INT32_C(2147418112)
#define MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_ORIGIN \
    INT32_C(2147418112)
#define MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATIONS_PER_SURFACE UINT32_C(8)
#define MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATIONS_PER_CLIENT UINT32_C(64)
#define MOO_COMP_EFFECT_DEFAULT_MIN_ANIMATION_DURATION_NS \
    UINT64_C(1000000)
#define MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATION_DURATION_NS \
    UINT64_C(600000000000)
#define MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATION_TIMELINE_NS \
    UINT64_C(3600000000000)

typedef int32_t MooCompQ16;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} MooCompRgba8;

typedef struct {
    uint16_t top_left;
    uint16_t top_right;
    uint16_t bottom_right;
    uint16_t bottom_left;
} MooCompCorners;

typedef struct {
    int32_t offset_x;
    int32_t offset_y;
    uint16_t blur_radius;
    uint16_t spread_radius;
    MooCompRgba8 color;
} MooCompShadow;

typedef struct {
    uint16_t blur_radius;
    uint16_t saturation_q8_8;
    MooCompRgba8 tint;
    uint8_t tint_mix;
    uint8_t noise;
    uint16_t reserved;
    uint32_t noise_seed;
} MooCompBackdrop;

typedef struct {
    MooCompQ16 m11;
    MooCompQ16 m12;
    MooCompQ16 m21;
    MooCompQ16 m22;
    MooCompQ16 tx;
    MooCompQ16 ty;
    MooCompQ16 origin_x;
    MooCompQ16 origin_y;
} MooCompAffine2D;

enum {
    MOO_COMP_EFFECT_CORNER_CLIP = UINT64_C(1) << 0,
    MOO_COMP_EFFECT_SHADOW = UINT64_C(1) << 1,
    MOO_COMP_EFFECT_BACKDROP_BLUR = UINT64_C(1) << 2,
    MOO_COMP_EFFECT_SATURATION = UINT64_C(1) << 3,
    MOO_COMP_EFFECT_TINT = UINT64_C(1) << 4,
    MOO_COMP_EFFECT_NOISE = UINT64_C(1) << 5,
    MOO_COMP_EFFECT_AFFINE_2D = UINT64_C(1) << 6,
    MOO_COMP_EFFECT_ANIMATION = UINT64_C(1) << 7
};

#define MOO_COMP_EFFECTS_V2 \
    (MOO_COMP_EFFECT_CORNER_CLIP | MOO_COMP_EFFECT_SHADOW | \
     MOO_COMP_EFFECT_BACKDROP_BLUR | MOO_COMP_EFFECT_SATURATION | \
     MOO_COMP_EFFECT_TINT | MOO_COMP_EFFECT_NOISE | \
     MOO_COMP_EFFECT_AFFINE_2D | MOO_COMP_EFFECT_ANIMATION)

typedef enum {
    MOO_COMP_EFFECT_FALLBACK_REQUIRE = 0,
    MOO_COMP_EFFECT_FALLBACK_ALLOW_DISABLE = 1,
    MOO_COMP_EFFECT_FALLBACK_ALLOW_APPROXIMATE = 2
} MooCompEffectFallbackPolicy;

typedef struct {
    uint64_t enabled_mask;
    uint64_t required_mask;
    uint32_t fallback_policy;
    uint32_t reserved;
    MooCompCorners corners;
    MooCompShadow shadow;
    MooCompBackdrop backdrop;
    MooCompAffine2D affine;
} MooCompEffectState;

/*
 * Observable result of capability and limit resolution.
 * requested is the exact committed client value. effective is the value used
 * by the portable renderer. degraded_mask is requested.enabled_mask XOR
 * effective.enabled_mask plus any bit whose parameters were normatively
 * approximated. It is zero when requested and effective are semantically
 * identical. Unknown bits and all reserved fields must be zero on input.
 */
typedef struct {
    MooCompEffectState requested;
    MooCompEffectState effective;
    uint64_t degraded_mask;
} MooCompEffectStatus;

typedef enum {
    MOO_COMP_ANIMATION_PROPERTY_INVALID = 0,
    MOO_COMP_ANIMATION_PROPERTY_OPACITY = 1,
    MOO_COMP_ANIMATION_PROPERTY_CORNERS = 2,
    MOO_COMP_ANIMATION_PROPERTY_SHADOW = 3,
    MOO_COMP_ANIMATION_PROPERTY_BACKDROP = 4,
    MOO_COMP_ANIMATION_PROPERTY_AFFINE_2D = 5
} MooCompAnimationProperty;

typedef enum {
    MOO_COMP_ANIMATION_EASING_LINEAR = 0,
    MOO_COMP_ANIMATION_EASING_IN_QUAD = 1,
    MOO_COMP_ANIMATION_EASING_OUT_QUAD = 2,
    MOO_COMP_ANIMATION_EASING_IN_OUT_QUAD = 3
} MooCompAnimationEasing;

typedef enum {
    MOO_COMP_ANIMATION_DIRECTION_NORMAL = 0,
    MOO_COMP_ANIMATION_DIRECTION_REVERSE = 1,
    MOO_COMP_ANIMATION_DIRECTION_ALTERNATE = 2,
    MOO_COMP_ANIMATION_DIRECTION_ALTERNATE_REVERSE = 3
} MooCompAnimationDirection;

/*
 * AnimationValue encoding is numeric and endian-independent. Unused words are
 * zero. Signed words decode as raw when raw<=INT32_MAX, otherwise raw-2^32 in
 * int64_t; never use an implementation-defined uint32_t-to-int32_t cast.
 * Packed color is exactly 0xRRGGBBAA: R bits31..24, G23..16, B15..8, A7..0.
 *
 * property     word mapping and structural validation
 * OPACITY      [0]=unsigned Q16.16 in [0,65536]; [1..7]=0.
 * CORNERS      [0..3]=TL,TR,BR,BL, each <=UINT16_MAX; [4..7]=0.
 * SHADOW       [0]=signed offset_x, [1]=signed offset_y, [2]=blur_radius,
 *              [3]=spread_radius (words 2/3 <=UINT16_MAX), [4]=0xRRGGBBAA,
 *              [5..7]=0.
 * BACKDROP     [0]=blur_radius <=UINT16_MAX, [1]=saturation_q8_8
 *              <=UINT16_MAX, [2]=tint 0xRRGGBBAA, [3]=tint_mix <=255,
 *              [4]=noise <=255, [5]=full uint32 noise_seed, [6..7]=0.
 * AFFINE_2D    [0..7]=signed Q16.16 in m11,m12,m21,m22,tx,ty,origin_x,
 *              origin_y order.
 *
 * A malformed range, nonzero unused word, invalid property/easing/direction,
 * zero token/repeat_count, or flags/reserved is MOO_COMP_INVALID. Both from
 * and to are then checked against EffectLimits; excess is MOO_COMP_LIMIT.
 * OPACITY is independent. CORNERS, SHADOW and AFFINE require their matching
 * enabled effect bit. BACKDROP requires at least one of BLUR/SATURATION/TINT/
 * NOISE; disabled components must be neutral in both endpoints: blur=0,
 * saturation=256, tint=0 and mix=0, noise=0 and seed=0. from must equal the
 * currently evaluated property at acceptance; mismatch is INVALID. All checks
 * happen before token/event reservation or mutation.
 */
typedef struct {
    uint32_t word[MOO_COMP_EFFECT_ANIMATION_VALUE_WORDS];
} MooCompAnimationValue;

/*
 * Animation lifecycle:
 * - repeat_count is total plays and is >=1; V2 has no infinite sentinel.
 *   checked delay_ns + duration_ns*repeat_count must not exceed the configured
 *   max timeline. duration must be within the configured min/max.
 * - token is nonzero client-local correlation, never a handle or authority.
 *   A token must be unique among that client's active animations; duplicate
 *   active tokens are INVALID with no mutation. Reuse after DoneV2 is allowed.
 * - Accepted animations start at the next accepted monotonic frame timestamp
 *   plus delay. A backward timestamp is INVALID and changes no state.
 * - A new animation for the same surface/property retargets from the currently
 *   evaluated value, emits REPLACED once for the old token, and starts the new
 *   token at the next accepted frame. Explicit cancel emits CANCELLED once;
 *   surface destruction emits SURFACE_DESTROYED once. Normal end emits
 *   COMPLETED once. An accepted token always emits exactly one terminal event.
 * - Under reduced motion, the to value is published at the next accepted frame
 *   with no intermediate value and REDUCED_MOTION is emitted exactly once.
 * - flags and reserved are zero in V2.
 */
typedef struct {
    uint64_t token;
    uint64_t delay_ns;
    uint64_t duration_ns;
    uint32_t repeat_count;
    uint32_t property;
    uint32_t easing;
    uint32_t direction;
    uint32_t flags;
    uint32_t reserved;
    MooCompAnimationValue from;
    MooCompAnimationValue to;
} MooCompAnimationDesc;

/*
 * Normative animation evaluation (no accumulated frame deltas):
 * Let Q=65535, D=duration_ns, N=repeat_count, S=accepted_frame_ns+delay_ns;
 * S and D*N are checked. For t<S evaluate from. For t>=S+D*N evaluate the
 * endpoint of play N-1 at local=D. Otherwise E=t-S, play=floor(E/D), and
 * local=E-play*D, so an exact internal boundary starts the next play at zero.
 * Skipped frames use this same direct calculation.
 *
 * Compute p=round(local*Q/D), nonnegative half-up. Default limits guarantee
 * the uint64 product; all products are checked. Eased e in [0,Q] is:
 * LINEAR p;
 * IN_QUAD round(p*p/Q);
 * OUT_QUAD Q-round((Q-p)*(Q-p)/Q);
 * IN_OUT_QUAD round(2*p*p/Q) when 2*p<=Q, otherwise
 * Q-round(2*(Q-p)*(Q-p)/Q).
 * Every displayed division here rounds nonnegative half-up.
 *
 * A play runs forward for NORMAL, backward for REVERSE, forward on even play
 * indices for ALTERNATE, and backward on even indices for ALTERNATE_REVERSE.
 * Backward replaces e by Q-e. Thus exact start/end, repeat boundaries and the
 * terminal value (including even/odd N) are fully determined.
 *
 * For each signed scalar decode endpoints a,b and compute
 * a+round_signed((b-a)*e/Q), nearest with halves away from zero in checked
 * int64_t, then encode modulo 2^32. Unsigned scalars use the same signed-delta
 * equation in int64_t and must remain within their structural range. Packed
 * colors interpolate R,G,B,A independently and repack 0xRRGGBBAA. BACKDROP
 * noise_seed is discrete: from seed for e<Q and to seed only at e==Q.
 * Interpolated endpoints already passed limits, so any arithmetic overflow is
 * MOO_COMP_LIMIT with no state mutation.
 */

typedef struct {
    uint16_t max_corner_radius;
    uint16_t max_shadow_blur_radius;
    uint16_t max_shadow_spread_radius;
    uint16_t max_backdrop_blur_radius;
    uint16_t max_saturation_q8_8;
    uint16_t reserved;
    int32_t max_abs_shadow_offset;
    MooCompQ16 max_abs_affine_coefficient;
    MooCompQ16 max_abs_affine_translation;
    MooCompQ16 max_abs_affine_origin;
    uint32_t max_animations_per_surface;
    uint32_t max_animations_per_client;
    uint32_t reserved_alignment;
    uint64_t min_animation_duration_ns;
    uint64_t max_animation_duration_ns;
    uint64_t max_animation_timeline_ns;
    uint64_t max_effect_work_units_per_frame;
    uint64_t max_effect_scratch_bytes;
} MooCompEffectLimits;
/*
 * Normative portable pixel and geometry semantics
 * ------------------------------------------------
 * - Images are RGBA8888, top-left origin, row-major, straight (unassociated)
 *   alpha. Geometry uses half-open rectangles [x,x+w) x [y,y+h). Surface and
 *   output pixels are sampled at centers (x+0.5,y+0.5).
 * - Affine coefficients are signed Q16.16; identity is
 *   {65536,0,0,65536,0,0,0,0}. Products use checked int64_t intermediates.
 *   Q16.16 multiply/divide rounds nearest with exact halves away from zero.
 *   Inverse affine sampling selects the nearest source pixel; an exact half
 *   selects the greater coordinate. Outside-source samples are transparent.
 *   AABBs include every covered sample and clip only after checked arithmetic.
 * - Corner normalization is CSS-style proportional scaling. Form candidate
 *   positive fractions width/(top_left+top_right),
 *   width/(bottom_left+bottom_right), height/(top_left+bottom_left), and
 *   height/(top_right+bottom_right), omitting zero denominators. Select the
 *   minimum of 1/1 and all candidates by checked uint64 cross multiplication.
 *   Every normalized radius is floor(radius*numerator/denominator), using that
 *   single selected fraction. A center exactly on the resulting ellipse is in.
 *
 * Premultiplied blur contract:
 * - Before blur, convert each straight channel C and alpha A to uint32 P=C*A
 *   (range 0..65025); alpha remains uint32 A (0..255). Radius r is a separable
 *   box of width d=2*r+1, horizontal then vertical. Each pass independently
 *   computes round(sum/d) by (sum+floor(d/2))/d using checked uint64 sums, for
 *   P and A. Convert back with A==0 => C=0, otherwise
 *   C=min(255,round(P/A)) using (P+floor(A/2))/A. This conversion happens only
 *   after both passes. Thus transparent colored pixels cannot create fringes.
 * - Backdrop samples beyond the output clamp to the nearest output-edge pixel.
 *   Source/shadow samples beyond their finite mask are transparent black.
 *   Shadow blur filters only its coverage alpha by the same two passes, then
 *   applies the straight shadow color; it never averages hidden shadow RGB.
 *
 * Integer color and alpha:
 * - Operations are integer sRGB; there is no implicit linear conversion.
 *   Saturation s is unsigned Q8.8 and must be <= max_saturation_q8_8. Let
 *   L=round((54*R+183*G+19*B)/256). Each output channel is
 *   clamp_0_255(L + round_signed((C-L)*s/256)); signed rounding is nearest
 *   with exact halves away from zero and uses int64_t intermediates.
 * - Tint is round((C*(255-mix)+tintC*mix)/255), half upward. Noise uses
 *   defined uint32 wrap: h=seed ^ uint32(x)*0x9E3779B1u ^
 *   uint32(y)*0x85EBCA77u; then h^=h>>16; h*=0x7FEB352Du;
 *   h^=h>>15; h*=0x846CA68Bu; h^=h>>16. Let n=(h>>24)-128 and
 *   delta=round_signed(n*noise/255). Add delta to each straight RGB channel
 *   and clamp 0..255; alpha is unchanged. It never depends on time, address,
 *   creation order, or frame number.
 * - Straight-alpha source-over uses exact nonnegative integers:
 *   A=Sa*255+Da*(255-Sa), outA=round(A/255), and
 *   U=Sc*Sa*255+Dc*Da*(255-Sa), outC=(A==0?0:round(U/A)).
 *   Nonnegative rounding is nearest with exact halves upward.
 *
 * Normative pass order for each surface from low to high Z is: (1) shadow,
 * (2) blur of the freshly composed lower-Z backdrop prefix, (3) saturation,
 * explicit tint, then deterministic noise, and (4) transformed rounded-clipped
 * content source-over. A surface never samples itself or a previous final frame.
 *
 * Validation and fallback table (all failures are pre-mutation):
 * - required_mask must be a subset of enabled_mask. Unknown mask bits, nonzero
 *   reserved fields, invalid enum values, and malformed limits are INVALID.
 * - Missing capability for a required bit is UNSUPPORTED for every policy.
 *   Any required parameter beyond its quantitative limit is LIMIT.
 * - REQUIRE: any missing optional capability is also UNSUPPORTED and any
 *   optional parameter beyond a limit is LIMIT; requested==effective.
 * - ALLOW_DISABLE: each unavailable or over-limit optional effect bit is
 *   removed from effective and placed in degraded_mask. No parameter clamps.
 * - ALLOW_APPROXIMATE: unavailable/over-limit optional BACKDROP_BLUR is removed
 *   while explicit tint remains; SATURATION becomes exactly 256; NOISE is
 *   removed. Each changed bit enters degraded_mask. An unavailable/over-limit
 *   optional CORNER_CLIP, SHADOW, TINT, AFFINE_2D or ANIMATION fails with
 *   UNSUPPORTED or LIMIT respectively; these effects are never approximated.
 * - Work-budget or scratch-budget excess, animation count/duration/timeline
 *   excess, and arithmetic overflow are whole-operation LIMIT for every policy
 *   because they cannot be safely attributed or approximated.
 * - degraded_mask contains exactly enabled bits whose effective enablement or
 *   parameters differ from requested; otherwise it is zero. Effective neutral
 *   values are zeroed, except saturation neutral is 256 and affine neutral is
 *   exact identity. Required bits never appear in a successful degraded_mask.
 *
 * Limits are validated once at init: reserved and reserved_alignment are zero; signed max_abs fields
 * are in [0,INT32_MAX] (coefficient >=65536); max_saturation_q8_8>=256;
 * duration min>=1 and min<=max<=timeline; counts/work/scratch are nonzero.
 * Runtime magnitude comparisons widen to int64_t and compare x<-limit or
 * x>limit; they never evaluate abs(INT32_MIN). Callers may only lower defaults.
 */
#endif
