#include "moo_runtime.h"
#include <time.h>

static bool random_seeded = false;

MooValue moo_abs(MooValue v) { return moo_number(fabs(moo_as_number(v))); }
MooValue moo_sqrt(MooValue v) { return moo_number(sqrt(moo_as_number(v))); }
MooValue moo_round(MooValue v) { return moo_number(round(moo_as_number(v))); }
MooValue moo_floor(MooValue v) { return moo_number(floor(moo_as_number(v))); }
MooValue moo_ceil(MooValue v) { return moo_number(ceil(moo_as_number(v))); }

MooValue moo_min(MooValue a, MooValue b) {
    double na = moo_as_number(a), nb = moo_as_number(b);
    return moo_number(na < nb ? na : nb);
}

MooValue moo_max(MooValue a, MooValue b) {
    double na = moo_as_number(a), nb = moo_as_number(b);
    return moo_number(na > nb ? na : nb);
}

MooValue moo_random(void) {
    if (!random_seeded) { srand((unsigned)time(NULL)); random_seeded = true; }
    return moo_number((double)rand() / RAND_MAX);
}

MooValue moo_index_get(MooValue container, MooValue index) {
    switch (container.tag) {
        case MOO_LIST:   return moo_list_get(container, index);
        case MOO_DICT:   return moo_dict_get(container, index);
        case MOO_STRING: return moo_string_index(container, index);
        default:         return moo_none();
    }
}

void moo_index_set(MooValue container, MooValue index, MooValue value) {
    switch (container.tag) {
        case MOO_LIST: moo_list_set(container, index, value); break;
        case MOO_DICT: moo_dict_set(container, index, value); break;
        default: break;
    }
}

MooValue moo_length(MooValue v) {
    switch (v.tag) {
        case MOO_STRING: return moo_number((double)MV_STR(v)->length);
        case MOO_LIST:   return moo_number((double)MV_LIST(v)->length);
        case MOO_DICT:   return moo_number((double)MV_DICT(v)->count);
        default:         return moo_number(0);
    }
}

MooValue moo_range(MooValue start, MooValue end) {
    int32_t s = (int32_t)moo_as_number(start);
    int32_t e = (int32_t)moo_as_number(end);
    int32_t len = e > s ? e - s : 0;
    MooValue list = moo_list_new(len);
    for (int32_t i = s; i < e; i++)
        moo_list_append(list, moo_number((double)i));
    return list;
}

MooValue moo_input(MooValue prompt) {
    if (prompt.tag == MOO_STRING) {
        printf("%s", MV_STR(prompt)->chars);
        fflush(stdout);
    }
    char buf[1024];
    if (fgets(buf, sizeof(buf), stdin)) {
        int len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
        return moo_string_new(buf);
    }
    return moo_none();
}
