#include "moo_runtime.h"
#include <time.h>

static bool random_seeded = false;

MooValue moo_abs(MooValue v) {
    return moo_number(fabs(moo_as_number(v)));
}

MooValue moo_sqrt(MooValue v) {
    return moo_number(sqrt(moo_as_number(v)));
}

MooValue moo_round(MooValue v) {
    return moo_number(round(moo_as_number(v)));
}

MooValue moo_floor(MooValue v) {
    return moo_number(floor(moo_as_number(v)));
}

MooValue moo_ceil(MooValue v) {
    return moo_number(ceil(moo_as_number(v)));
}

MooValue moo_min(MooValue a, MooValue b) {
    double na = moo_as_number(a), nb = moo_as_number(b);
    return moo_number(na < nb ? na : nb);
}

MooValue moo_max(MooValue a, MooValue b) {
    double na = moo_as_number(a), nb = moo_as_number(b);
    return moo_number(na > nb ? na : nb);
}

MooValue moo_random(void) {
    if (!random_seeded) {
        srand((unsigned)time(NULL));
        random_seeded = true;
    }
    return moo_number((double)rand() / RAND_MAX);
}

MooValue moo_input(MooValue prompt) {
    if (prompt.tag == MOO_STRING) {
        printf("%s", prompt.data.string->chars);
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
