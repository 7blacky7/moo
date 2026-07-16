#include "../moo_ui_host_parity.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static unsigned checks;
static unsigned failures;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL: %s\n", (message)); \
    } \
} while (0)

_Static_assert(MOO_UI_HOST_PARITY_VERSION == UINT32_C(1),
    "parity ABI version");
_Static_assert(MOO_UI_HOST_PARITY_DOMAIN_COUNT == UINT32_C(8),
    "parity domain count");
_Static_assert(MOO_UI_HOST_PARITY_ALL == UINT64_C(0xff),
    "parity domain mask");
_Static_assert(
    sizeof(((MooUiHostParityReport *)0)->measurements) /
        sizeof(MooUiHostParityMeasurement) ==
        MOO_UI_HOST_PARITY_DOMAIN_COUNT,
    "fixed report measurement capacity");
_Static_assert(
    offsetof(MooUiHostParityMeasurement, native_error) >
        offsetof(MooUiHostParityMeasurement, value_c),
    "measurement fields remain ordered");
_Static_assert(
    offsetof(MooUiHostParityReport, measurements) >
        offsetof(MooUiHostParityReport, measurement_count),
    "report header precedes measurements");

int
main(void)
{
    const uint64_t domains[MOO_UI_HOST_PARITY_DOMAIN_COUNT] = {
        MOO_UI_HOST_PARITY_TYPOGRAPHY,
        MOO_UI_HOST_PARITY_INPUT_IME,
        MOO_UI_HOST_PARITY_ACCESSIBILITY,
        MOO_UI_HOST_PARITY_DPI_MONITORS,
        MOO_UI_HOST_PARITY_CLIPBOARD_DRAG_DROP,
        MOO_UI_HOST_PARITY_ANIMATION_ENERGY,
        MOO_UI_HOST_PARITY_CRASH_ISOLATION,
        MOO_UI_HOST_PARITY_DEVTOOLS
    };
    uint64_t union_mask = UINT64_C(0);
    uint32_t i;

    for (i = 0u; i < MOO_UI_HOST_PARITY_DOMAIN_COUNT; ++i) {
        CHECK(domains[i] == (UINT64_C(1) << i),
            "domain bits are unique and ordered");
        union_mask |= domains[i];
    }
    CHECK(union_mask == MOO_UI_HOST_PARITY_ALL,
        "domain union matches ALL");

    CHECK(MOO_UI_HOST_PARITY_RESULT_OK == 0,
        "result OK value");
    CHECK(MOO_UI_HOST_PARITY_RESULT_LIMIT == 5,
        "result LIMIT value");
    CHECK(MOO_UI_HOST_PARITY_NATIVE_UNINITIALIZED == 0,
        "native initial value");
    CHECK(MOO_UI_HOST_PARITY_NATIVE_LOST == 4,
        "native LOST value");
    CHECK(MOO_UI_HOST_PARITY_EVIDENCE_UNMEASURED == 0,
        "evidence unmeasured value");
    CHECK(MOO_UI_HOST_PARITY_EVIDENCE_NOT_APPLICABLE == 4,
        "evidence N/A value");

    if (failures != 0u) {
        fprintf(stderr, "P016 O6 PARITY ABI FAIL: %u/%u\n",
            failures, checks);
        return 1;
    }
    printf("P016 O6 PARITY ABI GREEN: %u\n", checks);
    return 0;
}
