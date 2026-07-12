#include "moo_compositor_effects_math.h"

#define MOO_COMP_EFFECT_Q16_SCALE INT64_C(65536)
#define MOO_COMP_EFFECT_Q32_SCALE INT64_C(4294967296)

static uint64_t moo_comp_effect_abs_i64(int64_t value) {
    if (value >= 0) return (uint64_t)value;
    return (uint64_t)(-(value + 1)) + UINT64_C(1);
}

static int moo_comp_effect_add_i64(
    int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) ||
        (b < 0 && a < INT64_MIN - b))
        return 0;
    *out = a + b;
    return 1;
}

static int moo_comp_effect_sub_i64(
    int64_t a, int64_t b, int64_t *out) {
    if ((b < 0 && a > INT64_MAX + b) ||
        (b > 0 && a < INT64_MIN + b))
        return 0;
    *out = a - b;
    return 1;
}

static int moo_comp_effect_mul_i64(
    int64_t a, int64_t b, int64_t *out) {
    uint64_t ua = moo_comp_effect_abs_i64(a);
    uint64_t ub = moo_comp_effect_abs_i64(b);
    uint64_t product;
    int negative = (a < 0) != (b < 0);
    if (ua != 0u && ub > (uint64_t)INT64_MAX / ua) return 0;
    product = ua * ub;
    *out = negative ? -(int64_t)product : (int64_t)product;
    return 1;
}

static int moo_comp_effect_div_round_i64(
    int64_t numerator, int64_t denominator, int64_t *out) {
    uint64_t un;
    uint64_t ud;
    uint64_t q;
    uint64_t r;
    int negative;
    if (denominator == 0) return 0;
    un = moo_comp_effect_abs_i64(numerator);
    ud = moo_comp_effect_abs_i64(denominator);
    q = un / ud;
    r = un % ud;
    if (r > ud - r || r == ud - r) {
        if (q == (uint64_t)INT64_MAX) return 0;
        ++q;
    }
    if (q > (uint64_t)INT64_MAX) return 0;
    negative = (numerator < 0) != (denominator < 0);
    *out = negative ? -(int64_t)q : (int64_t)q;
    return 1;
}

static int moo_comp_effect_i64_to_i32(int64_t value, int32_t *out) {
    if (value < INT32_MIN || value > INT32_MAX) return 0;
    *out = (int32_t)value;
    return 1;
}

static int moo_comp_effect_rect_edges(
    MooCompRect rect, int64_t *x0, int64_t *y0,
    int64_t *x1, int64_t *y1) {
    if (rect.width < 0 || rect.height < 0) return 0;
    *x0 = rect.x;
    *y0 = rect.y;
    return moo_comp_effect_add_i64(
               rect.x, rect.width, x1) &&
           moo_comp_effect_add_i64(rect.y, rect.height, y1);
}

static int moo_comp_effect_rect_from_edges(
    int64_t x0, int64_t y0, int64_t x1, int64_t y1,
    MooCompRect *out) {
    int64_t width;
    int64_t height;
    MooCompRect value;
    if (!out || x1 < x0 || y1 < y0 ||
        !moo_comp_effect_sub_i64(x1, x0, &width) ||
        !moo_comp_effect_sub_i64(y1, y0, &height) ||
        !moo_comp_effect_i64_to_i32(x0, &value.x) ||
        !moo_comp_effect_i64_to_i32(y0, &value.y) ||
        !moo_comp_effect_i64_to_i32(width, &value.width) ||
        !moo_comp_effect_i64_to_i32(height, &value.height))
        return 0;
    out->x = value.x; out->y = value.y;
    out->width = value.width; out->height = value.height;
    return 1;
}

MooCompResult moo_comp_effect_q16_mul(
    MooCompQ16 a, MooCompQ16 b, MooCompQ16 *out) {
    int64_t product;
    int64_t rounded;
    int32_t value;
    if (!out) return MOO_COMP_INVALID;
    product = (int64_t)a * (int64_t)b;
    if (!moo_comp_effect_div_round_i64(
            product, MOO_COMP_EFFECT_Q16_SCALE, &rounded) ||
        !moo_comp_effect_i64_to_i32(rounded, &value))
        return MOO_COMP_LIMIT;
    *out = value;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_effect_q16_div(
    MooCompQ16 numerator, MooCompQ16 denominator, MooCompQ16 *out) {
    int64_t scaled;
    int64_t rounded;
    int32_t value;
    if (!out || denominator == 0) return MOO_COMP_INVALID;
    scaled = (int64_t)numerator * MOO_COMP_EFFECT_Q16_SCALE;
    if (!moo_comp_effect_div_round_i64(scaled, denominator, &rounded) ||
        !moo_comp_effect_i64_to_i32(rounded, &value))
        return MOO_COMP_LIMIT;
    *out = value;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_effect_affine_transform_point(
    const MooCompAffine2D *a, MooCompEffectPointQ16 point,
    MooCompEffectPointQ16 *out) {
    int64_t rx;
    int64_t ry;
    int64_t p0;
    int64_t p1;
    int64_t sum;
    int64_t rounded;
    int64_t translated;
    MooCompEffectPointQ16 value;
    if (!a || !out) return MOO_COMP_INVALID;
    if (!moo_comp_effect_sub_i64(point.x, a->origin_x, &rx) ||
        !moo_comp_effect_sub_i64(point.y, a->origin_y, &ry) ||
        !moo_comp_effect_mul_i64(a->m11, rx, &p0) ||
        !moo_comp_effect_mul_i64(a->m12, ry, &p1) ||
        !moo_comp_effect_add_i64(p0, p1, &sum) ||
        !moo_comp_effect_div_round_i64(
            sum, MOO_COMP_EFFECT_Q16_SCALE, &rounded) ||
        !moo_comp_effect_add_i64(rounded, a->origin_x, &translated) ||
        !moo_comp_effect_add_i64(translated, a->tx, &translated) ||
        !moo_comp_effect_i64_to_i32(translated, &value.x))
        return MOO_COMP_LIMIT;
    if (!moo_comp_effect_mul_i64(a->m21, rx, &p0) ||
        !moo_comp_effect_mul_i64(a->m22, ry, &p1) ||
        !moo_comp_effect_add_i64(p0, p1, &sum) ||
        !moo_comp_effect_div_round_i64(
            sum, MOO_COMP_EFFECT_Q16_SCALE, &rounded) ||
        !moo_comp_effect_add_i64(rounded, a->origin_y, &translated) ||
        !moo_comp_effect_add_i64(translated, a->ty, &translated) ||
        !moo_comp_effect_i64_to_i32(translated, &value.y))
        return MOO_COMP_LIMIT;
    out->x = value.x; out->y = value.y;
    return MOO_COMP_OK;
}

static int moo_comp_effect_inverse_coefficient(
    int64_t numerator, int64_t determinant, int32_t *out) {
    int64_t scaled;
    int64_t value;
    if (!moo_comp_effect_mul_i64(
            numerator, MOO_COMP_EFFECT_Q32_SCALE, &scaled) ||
        !moo_comp_effect_div_round_i64(scaled, determinant, &value))
        return 0;
    return moo_comp_effect_i64_to_i32(value, out);
}

MooCompResult moo_comp_effect_affine_inverse(
    const MooCompAffine2D *a, MooCompAffine2D *out) {
    int64_t p0;
    int64_t p1;
    int64_t determinant;
    MooCompAffine2D value;
    MooCompQ16 q0;
    MooCompQ16 q1;
    int64_t sum;
    if (!a || !out) return MOO_COMP_INVALID;
    if (!moo_comp_effect_mul_i64(a->m11, a->m22, &p0) ||
        !moo_comp_effect_mul_i64(a->m12, a->m21, &p1) ||
        !moo_comp_effect_sub_i64(p0, p1, &determinant))
        return MOO_COMP_LIMIT;
    if (determinant == 0) return MOO_COMP_INVALID;
    if (!moo_comp_effect_inverse_coefficient(
            a->m22, determinant, &value.m11) ||
        !moo_comp_effect_inverse_coefficient(
            -((int64_t)a->m12), determinant, &value.m12) ||
        !moo_comp_effect_inverse_coefficient(
            -((int64_t)a->m21), determinant, &value.m21) ||
        !moo_comp_effect_inverse_coefficient(
            a->m11, determinant, &value.m22))
        return MOO_COMP_LIMIT;
    value.origin_x = a->origin_x;
    value.origin_y = a->origin_y;
    if (moo_comp_effect_q16_mul(value.m11, a->tx, &q0) != MOO_COMP_OK ||
        moo_comp_effect_q16_mul(value.m12, a->ty, &q1) != MOO_COMP_OK ||
        !moo_comp_effect_add_i64(q0, q1, &sum) ||
        sum == INT64_MIN ||
        !moo_comp_effect_i64_to_i32(-sum, &value.tx))
        return MOO_COMP_LIMIT;
    if (moo_comp_effect_q16_mul(value.m21, a->tx, &q0) != MOO_COMP_OK ||
        moo_comp_effect_q16_mul(value.m22, a->ty, &q1) != MOO_COMP_OK ||
        !moo_comp_effect_add_i64(q0, q1, &sum) ||
        sum == INT64_MIN ||
        !moo_comp_effect_i64_to_i32(-sum, &value.ty))
        return MOO_COMP_LIMIT;
    out->m11=value.m11; out->m12=value.m12;
    out->m21=value.m21; out->m22=value.m22;
    out->tx=value.tx; out->ty=value.ty;
    out->origin_x=value.origin_x; out->origin_y=value.origin_y;
    return MOO_COMP_OK;
}

static int32_t moo_comp_effect_q16_floor(int32_t value) {
    int64_t v = value;
    if (v >= 0) return (int32_t)(v / MOO_COMP_EFFECT_Q16_SCALE);
    return (int32_t)(-((-v + MOO_COMP_EFFECT_Q16_SCALE - 1) /
                       MOO_COMP_EFFECT_Q16_SCALE));
}

static int32_t moo_comp_effect_q16_ceil(int32_t value) {
    int64_t v = value;
    if (v >= 0)
        return (int32_t)((v + MOO_COMP_EFFECT_Q16_SCALE - 1) /
                         MOO_COMP_EFFECT_Q16_SCALE);
    return (int32_t)(-((-v) / MOO_COMP_EFFECT_Q16_SCALE));
}

static int moo_comp_effect_pixel_to_q16(int64_t pixel, int32_t *out) {
    int64_t scaled;
    if (!moo_comp_effect_mul_i64(
            pixel, MOO_COMP_EFFECT_Q16_SCALE, &scaled))
        return 0;
    return moo_comp_effect_i64_to_i32(scaled, out);
}

MooCompResult moo_comp_effect_transform_rect_aabb(
    const MooCompAffine2D *a, MooCompRect rect, MooCompRect *out) {
    int64_t x0,y0,x1,y1;
    MooCompEffectPointQ16 p[4];
    MooCompEffectPointQ16 q;
    int32_t min_x,max_x,min_y,max_y;
    uint32_t i;
    MooCompRect value;
    if (!a || !out || !moo_comp_effect_rect_edges(
            rect,&x0,&y0,&x1,&y1))
        return MOO_COMP_INVALID;
    if (!moo_comp_effect_pixel_to_q16(x0,&p[0].x) ||
        !moo_comp_effect_pixel_to_q16(y0,&p[0].y) ||
        !moo_comp_effect_pixel_to_q16(x1,&p[1].x) ||
        !moo_comp_effect_pixel_to_q16(y0,&p[1].y) ||
        !moo_comp_effect_pixel_to_q16(x0,&p[2].x) ||
        !moo_comp_effect_pixel_to_q16(y1,&p[2].y) ||
        !moo_comp_effect_pixel_to_q16(x1,&p[3].x) ||
        !moo_comp_effect_pixel_to_q16(y1,&p[3].y))
        return MOO_COMP_LIMIT;
    if (moo_comp_effect_affine_transform_point(a,p[0],&q)!=MOO_COMP_OK)
        return MOO_COMP_LIMIT;
    min_x=max_x=q.x; min_y=max_y=q.y;
    for(i=1u;i<4u;++i){
        if(moo_comp_effect_affine_transform_point(a,p[i],&q)!=MOO_COMP_OK)
            return MOO_COMP_LIMIT;
        if (q.x < min_x) min_x = q.x;
        if (q.x > max_x) max_x = q.x;
        if (q.y < min_y) min_y = q.y;
        if (q.y > max_y) max_y = q.y;
    }
    if (!moo_comp_effect_rect_from_edges(
            moo_comp_effect_q16_floor(min_x),
            moo_comp_effect_q16_floor(min_y),
            moo_comp_effect_q16_ceil(max_x),
            moo_comp_effect_q16_ceil(max_y), &value))
        return MOO_COMP_LIMIT;
    out->x=value.x; out->y=value.y;
    out->width=value.width; out->height=value.height;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_effect_rect_union(
    MooCompRect a, MooCompRect b, MooCompRect *out) {
    int64_t ax0,ay0,ax1,ay1,bx0,by0,bx1,by1;
    MooCompRect value;
    if (!out || !moo_comp_effect_rect_edges(a,&ax0,&ay0,&ax1,&ay1) ||
        !moo_comp_effect_rect_edges(b,&bx0,&by0,&bx1,&by1))
        return MOO_COMP_INVALID;
    if (a.width == 0 || a.height == 0) { value=b; }
    else if (b.width == 0 || b.height == 0) { value=a; }
    else if (!moo_comp_effect_rect_from_edges(
        ax0<bx0?ax0:bx0, ay0<by0?ay0:by0,
        ax1>bx1?ax1:bx1, ay1>by1?ay1:by1, &value))
        return MOO_COMP_LIMIT;
    out->x=value.x; out->y=value.y;
    out->width=value.width; out->height=value.height;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_effect_rect_intersect(
    MooCompRect a, MooCompRect b, MooCompRect *out) {
    int64_t ax0,ay0,ax1,ay1,bx0,by0,bx1,by1;
    int64_t x0,y0,x1,y1;
    MooCompRect value;
    if (!out || !moo_comp_effect_rect_edges(a,&ax0,&ay0,&ax1,&ay1) ||
        !moo_comp_effect_rect_edges(b,&bx0,&by0,&bx1,&by1))
        return MOO_COMP_INVALID;
    x0=ax0>bx0?ax0:bx0; y0=ay0>by0?ay0:by0;
    x1=ax1<bx1?ax1:bx1; y1=ay1<by1?ay1:by1;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    if(!moo_comp_effect_rect_from_edges(x0,y0,x1,y1,&value))
        return MOO_COMP_LIMIT;
    out->x=value.x; out->y=value.y;
    out->width=value.width; out->height=value.height;
    return MOO_COMP_OK;
}

MooCompResult moo_comp_effect_rect_expand(
    MooCompRect rect, int32_t radius, MooCompRect *out) {
    int64_t x0,y0,x1,y1;
    MooCompRect value;
    if (!out || radius < 0 ||
        !moo_comp_effect_rect_edges(rect,&x0,&y0,&x1,&y1))
        return MOO_COMP_INVALID;
    if (!moo_comp_effect_sub_i64(x0,radius,&x0) ||
        !moo_comp_effect_sub_i64(y0,radius,&y0) ||
        !moo_comp_effect_add_i64(x1,radius,&x1) ||
        !moo_comp_effect_add_i64(y1,radius,&y1) ||
        !moo_comp_effect_rect_from_edges(x0,y0,x1,y1,&value))
        return MOO_COMP_LIMIT;
    out->x=value.x; out->y=value.y;
    out->width=value.width; out->height=value.height;
    return MOO_COMP_OK;
}

static void moo_comp_effect_fraction_min(
    uint64_t n, uint64_t d, uint64_t *best_n, uint64_t *best_d) {
    if (d != 0u && n * (*best_d) < (*best_n) * d) {
        *best_n=n; *best_d=d;
    }
}

MooCompResult moo_comp_effect_corners_normalize(
    int32_t width, int32_t height,
    const MooCompCorners *r, MooCompCorners *out) {
    uint64_t n=1u,d=1u;
    MooCompCorners v;
    if (!r || !out || width < 0 || height < 0)
        return MOO_COMP_INVALID;
    moo_comp_effect_fraction_min((uint64_t)width,
        (uint64_t)r->top_left+r->top_right,&n,&d);
    moo_comp_effect_fraction_min((uint64_t)width,
        (uint64_t)r->bottom_left+r->bottom_right,&n,&d);
    moo_comp_effect_fraction_min((uint64_t)height,
        (uint64_t)r->top_left+r->bottom_left,&n,&d);
    moo_comp_effect_fraction_min((uint64_t)height,
        (uint64_t)r->top_right+r->bottom_right,&n,&d);
    v.top_left=(uint16_t)(((uint64_t)r->top_left*n)/d);
    v.top_right=(uint16_t)(((uint64_t)r->top_right*n)/d);
    v.bottom_right=(uint16_t)(((uint64_t)r->bottom_right*n)/d);
    v.bottom_left=(uint16_t)(((uint64_t)r->bottom_left*n)/d);
    out->top_left=v.top_left; out->top_right=v.top_right;
    out->bottom_right=v.bottom_right; out->bottom_left=v.bottom_left;
    return MOO_COMP_OK;
}

static int moo_comp_effect_corner_inside(
    int32_t px,int32_t py,int32_t cx,int32_t cy,uint16_t radius) {
    int64_t sx=(int64_t)px*2+1;
    int64_t sy=(int64_t)py*2+1;
    int64_t dx=sx-(int64_t)cx*2;
    int64_t dy=sy-(int64_t)cy*2;
    uint64_t ux=moo_comp_effect_abs_i64(dx);
    uint64_t uy=moo_comp_effect_abs_i64(dy);
    uint64_t rr=(uint64_t)radius*2u;
    return ux*ux+uy*uy<=rr*rr;
}

MooCompResult moo_comp_effect_rounded_coverage_a8(
    int32_t width,int32_t height,const MooCompCorners *r,
    int32_t x,int32_t y,uint8_t *out) {
    uint8_t value=255u;
    if(!r||!out||width<0||height<0)return MOO_COMP_INVALID;
    if(x<0||y<0||x>=width||y>=height)value=0u;
    else if(x<(int32_t)r->top_left&&y<(int32_t)r->top_left)
        value=moo_comp_effect_corner_inside(
            x,y,r->top_left,r->top_left,r->top_left)?255u:0u;
    else if(x>=width-(int32_t)r->top_right&&
            y<(int32_t)r->top_right)
        value=moo_comp_effect_corner_inside(
            x,y,width-r->top_right,r->top_right,r->top_right)?255u:0u;
    else if(x>=width-(int32_t)r->bottom_right&&
            y>=height-(int32_t)r->bottom_right)
        value=moo_comp_effect_corner_inside(
            x,y,width-r->bottom_right,height-r->bottom_right,
            r->bottom_right)?255u:0u;
    else if(x<(int32_t)r->bottom_left&&
            y>=height-(int32_t)r->bottom_left)
        value=moo_comp_effect_corner_inside(
            x,y,r->bottom_left,height-r->bottom_left,
            r->bottom_left)?255u:0u;
    *out=value; return MOO_COMP_OK;
}

static void moo_comp_effect_rect_copy(MooCompRect *dst,MooCompRect src){
    dst->x=src.x;dst->y=src.y;dst->width=src.width;dst->height=src.height;
}

MooCompResult moo_comp_effect_compute_bounds(
    MooCompRect local,const MooCompAffine2D *a,uint64_t mask,
    const MooCompShadow *shadow,const MooCompBackdrop *backdrop,
    MooCompEffectBounds *out) {
    MooCompRect content,visual,sample,shadow_rect;
    int64_t footprint;
    MooCompResult r;
    if(!a||!shadow||!backdrop||!out||
       (mask&~MOO_COMP_EFFECTS_V2)!=0u)return MOO_COMP_INVALID;
    r=moo_comp_effect_transform_rect_aabb(a,local,&content);
    if(r!=MOO_COMP_OK)return r;
    moo_comp_effect_rect_copy(&visual,content);
    if((mask&MOO_COMP_EFFECT_SHADOW)!=0u){
        footprint=(int64_t)shadow->blur_radius+shadow->spread_radius;
        if(footprint>INT32_MAX)return MOO_COMP_LIMIT;
        shadow_rect.x=content.x;shadow_rect.y=content.y;
        shadow_rect.width=content.width;shadow_rect.height=content.height;
        r=moo_comp_effect_rect_expand(
            shadow_rect,(int32_t)footprint,&shadow_rect);
        if(r!=MOO_COMP_OK)return r;
        if(!moo_comp_effect_add_i64(
              shadow_rect.x,shadow->offset_x,&footprint)||
           !moo_comp_effect_i64_to_i32(footprint,&shadow_rect.x)||
           !moo_comp_effect_add_i64(
              shadow_rect.y,shadow->offset_y,&footprint)||
           !moo_comp_effect_i64_to_i32(footprint,&shadow_rect.y))
            return MOO_COMP_LIMIT;
        r=moo_comp_effect_rect_union(content,shadow_rect,&visual);
        if(r!=MOO_COMP_OK)return r;
    }
    moo_comp_effect_rect_copy(&sample,content);
    if((mask&MOO_COMP_EFFECT_BACKDROP_BLUR)!=0u){
        r=moo_comp_effect_rect_expand(
            content,(int32_t)backdrop->blur_radius,&sample);
        if(r!=MOO_COMP_OK)return r;
    }
    moo_comp_effect_rect_copy(&out->content_bounds,content);
    moo_comp_effect_rect_copy(&out->visual_bounds,visual);
    moo_comp_effect_rect_copy(&out->backdrop_sample_bounds,sample);
    return MOO_COMP_OK;
}
