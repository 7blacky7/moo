#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include "../moo_ui_host_parity.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL: %s\n", (message)); \
    } \
} while (0)

static const char *const domain_names[MOO_UI_HOST_PARITY_DOMAIN_COUNT] = {
    "typography",
    "input_ime",
    "accessibility",
    "dpi_monitors",
    "clipboard_drag_drop",
    "animation_energy",
    "crash_isolation",
    "devtools"
};

static int measurement_is_exact_unsupported(
    const MooUiHostParityMeasurement *measurement, uint32_t domain) {
    return measurement->version == MOO_UI_HOST_PARITY_VERSION &&
        measurement->domain == domain &&
        measurement->evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED &&
        measurement->sample_count == 0u &&
        measurement->value_a == UINT64_C(0) &&
        measurement->value_b == UINT64_C(0) &&
        measurement->value_c == UINT64_C(0) &&
        measurement->native_error == 0 &&
        measurement->reserved == 0u;
}

int main(void) {
#if !defined(__APPLE__)
    fputs("P016 O6 MACOS APPKIT HARNESS PLATFORM_UNAVAILABLE\n", stderr);
    return 77;
#else
    @autoreleasepool {
        MooUiHostParityState state;
        MooUiHostParityRequest request;
        MooUiHostParityReport report;
        MooUiHostParityReport strict_report;
        MooUiHostParityResult result;
        NSApplication *application;
        NSArray<NSScreen *> *screens;
        NSString *os_version;
        uint32_t index;

        CHECK([NSThread isMainThread], "AppKit gate runs on the main thread");
        application = [NSApplication sharedApplication];
        CHECK(application != nil, "NSApplication initializes");
        screens = [NSScreen screens];
        CHECK(screens != nil, "NSScreen inventory is available");
        os_version = [[NSProcessInfo processInfo] operatingSystemVersionString];
        CHECK(os_version != nil && [os_version length] != 0u,
            "macOS version is available");

        memset(&state, 0xa5, sizeof(state));
        result = moo_ui_host_parity_init_cocoa(&state);
        CHECK(result == MOO_UI_HOST_PARITY_RESULT_OK,
            "Cocoa parity adapter initializes");
        CHECK(state.native_state ==
                (uint32_t)MOO_UI_HOST_PARITY_NATIVE_READY &&
              moo_ui_host_parity_generation(&state) == UINT64_C(1),
            "Cocoa parity lifecycle is ready at generation one");

        request = moo_ui_host_parity_request_default();
        request.allow_partial = 1u;
        memset(&report, 0xa5, sizeof(report));
        result = moo_ui_host_parity_probe(&state, &request, &report);
        CHECK(result == MOO_UI_HOST_PARITY_RESULT_OK,
            "partial probe reports honest availability");
        CHECK(report.version == MOO_UI_HOST_PARITY_VERSION &&
              report.native_state ==
                  (uint32_t)MOO_UI_HOST_PARITY_NATIVE_READY &&
              report.measurement_count == MOO_UI_HOST_PARITY_DOMAIN_COUNT,
            "report ABI and native state");
        CHECK(report.measured_domains == UINT64_C(0) &&
              report.passed_domains == UINT64_C(0) &&
              report.failed_domains == UINT64_C(0) &&
              report.unsupported_domains == MOO_UI_HOST_PARITY_ALL &&
              report.not_applicable_domains == UINT64_C(0) &&
              report.missing_required_domains == MOO_UI_HOST_PARITY_ALL &&
              report.native_error == 0,
            "all unimplemented native domains remain exact unsupported");

        for (index = 0u; index < MOO_UI_HOST_PARITY_DOMAIN_COUNT; ++index) {
            const MooUiHostParityMeasurement *measurement =
                &report.measurements[index];
            CHECK(measurement_is_exact_unsupported(measurement, index),
                "unavailable domain has zero samples, values, and native error");
            printf(
                "DOMAIN name=%s index=%" PRIu32
                " evidence=UNSUPPORTED samples=0 values=0/0/0 native_error=0\n",
                domain_names[index], index);
        }

        request.allow_partial = 0u;
        memset(&strict_report, 0xa5, sizeof(strict_report));
        result = moo_ui_host_parity_probe(&state, &request, &strict_report);
        CHECK(result == MOO_UI_HOST_PARITY_RESULT_UNAVAILABLE,
            "strict all-domain request fails closed");
        CHECK(strict_report.passed_domains == UINT64_C(0) &&
              strict_report.unsupported_domains == MOO_UI_HOST_PARITY_ALL &&
              strict_report.missing_required_domains ==
                  MOO_UI_HOST_PARITY_ALL,
            "strict report preserves exact unsupported evidence");

        printf("HOST os=%s screens=%lu appkit_initialized=1\n",
            [os_version UTF8String], (unsigned long)[screens count]);

        moo_ui_host_parity_shutdown(&state);
        CHECK(moo_ui_host_parity_generation(&state) == UINT64_C(0) &&
              state.native_state ==
                  (uint32_t)MOO_UI_HOST_PARITY_NATIVE_UNINITIALIZED,
            "Cocoa parity shutdown clears lifecycle state");

        if (failures != 0u) {
            fprintf(stderr,
                "P016 O6 MACOS APPKIT GATE FAIL: failures=%u checks=%u\n",
                failures, checks);
            return 1;
        }
        printf("P016 O6 MACOS APPKIT GATE EXECUTED: "
               "PARITY_STATUS=UNSUPPORTED domains=0xff checks=%u\n", checks);
        return 0;
    }
#endif
}
