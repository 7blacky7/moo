#include "../moo_ui_host_parity.h"

#include <stdint.h>
#include <stdio.h>

#if !defined(_WIN32)
int main(void) {
    puts("P016 O6 WIN32 PARITY SKIP: 77");
    return 77;
}
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <imm.h>

static unsigned checks;
static unsigned failures;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL: %s\n", (message)); \
    } \
} while (0)

int main(void) {
    MooUiHostParityState state;
    MooUiHostParityRequest request = moo_ui_host_parity_request_default();
    MooUiHostParityReport report;
    MooUiHostParityResult result;
    uint64_t expected_passed = MOO_UI_HOST_PARITY_DPI_MONITORS |
        MOO_UI_HOST_PARITY_ACCESSIBILITY | MOO_UI_HOST_PARITY_TYPOGRAPHY;
    DWORD clipboard_sequence_before = GetClipboardSequenceNumber();
    HKL keyboard_layout_before = GetKeyboardLayout(0);
    HKL keyboard_layout_after;
    WCHAR keyboard_layout_name_before[KL_NAMELENGTH] = {0};
    WCHAR keyboard_layout_name_after[KL_NAMELENGTH] = {0};
    BOOL keyboard_layout_name_before_ok =
        GetKeyboardLayoutNameW(keyboard_layout_name_before);
    BOOL keyboard_layout_name_after_ok;
    BOOL keyboard_layout_is_ime_before = ImmIsIME(keyboard_layout_before);
    BOOL keyboard_layout_is_ime_after;
    result = moo_ui_host_parity_init_win32(&state);
    CHECK(result == MOO_UI_HOST_PARITY_RESULT_OK, "native init");
    request.allow_partial = 1u;
    request.required_domains = expected_passed;
    ZeroMemory(&report, sizeof report);
    result = moo_ui_host_parity_probe(&state, &request, &report);
    CHECK(result == MOO_UI_HOST_PARITY_RESULT_OK, "partial native probe");
    if (result != MOO_UI_HOST_PARITY_RESULT_OK) {
        DWORD clipboard_sequence_after = GetClipboardSequenceNumber();
        fprintf(stderr,
            "PROBE_DIAGNOSTIC result=%u native_state=%u native_error=%d "
            "clipboard_sequence=%lu/%lu\n",
            (unsigned)result, (unsigned)state.native_state,
            (int)state.last_native_error,
            (unsigned long)clipboard_sequence_before,
            (unsigned long)clipboard_sequence_after);
        CHECK(clipboard_sequence_after == clipboard_sequence_before,
            "failed parity probe leaves interactive clipboard sequence unchanged");
        moo_ui_host_parity_shutdown(&state);
        return 1;
    }
    if (report.measurements[1].evidence ==
        (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS)
        expected_passed |= MOO_UI_HOST_PARITY_INPUT_IME;
    CHECK(report.version == MOO_UI_HOST_PARITY_VERSION, "report version");
    CHECK(report.native_state == (uint32_t)MOO_UI_HOST_PARITY_NATIVE_READY,
        "native state ready");
    CHECK(report.measurement_count == MOO_UI_HOST_PARITY_DOMAIN_COUNT,
        "all domain slots reported");
    CHECK(report.generation == UINT64_C(1), "initial generation");
    CHECK(report.passed_domains == expected_passed,
        "only native typography, accessibility and DPI domains pass");
    CHECK(report.measured_domains == expected_passed,
        "only native typography, accessibility and DPI are measured");
    CHECK(report.failed_domains == UINT64_C(0), "no failed domains");
    CHECK(report.not_applicable_domains == UINT64_C(0),
        "no domains hidden as not applicable");
    CHECK(report.unsupported_domains ==
        (MOO_UI_HOST_PARITY_ALL & ~expected_passed),
        "every unmeasured domain is explicitly unsupported");
    CHECK((report.measured_domains | report.unsupported_domains) ==
        MOO_UI_HOST_PARITY_ALL, "domain disposition complete");
    CHECK((report.unsupported_domains & expected_passed) == UINT64_C(0),
        "measured native domains are not unsupported");
    CHECK(report.measurements[0].domain == 0u, "typography domain index");
    CHECK(report.measurements[0].evidence ==
        (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS,
        "native typography evidence pass");
    CHECK(report.measurements[0].sample_count == 9u,
        "three strings at three sizes are measured");
    CHECK(report.measurements[0].value_a <=
        (uint64_t)MOO_UI_HOST_PARITY_TYPOGRAPHY_MAX_ERROR_MILLI_PX,
        "GDI-vs-DirectWrite baseline error within 0.5 pixel");
    CHECK(report.measurements[0].value_b <=
        (uint64_t)MOO_UI_HOST_PARITY_TYPOGRAPHY_MAX_ERROR_MILLI_PX,
        "GDI-vs-DirectWrite advance error within 0.5 pixel");
    CHECK(report.measurements[0].value_c == UINT64_C(0),
        "no sample codepoint missing on either explicit Segoe UI face");
    CHECK(report.measurements[0].native_error == 0,
        "typography native error clear");
    CHECK(report.measurements[3].evidence ==
        (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS, "DPI evidence pass");
    CHECK(report.measurements[3].domain == 3u, "DPI domain index");
    CHECK(report.measurements[3].sample_count > 0u, "monitor samples");
    CHECK(report.measurements[3].sample_count <=
        (uint32_t)MOO_UI_HOST_PARITY_MAX_MONITORS, "monitor sample limit");
    CHECK(report.measurements[3].native_error == 0, "DPI native error clear");
    CHECK(report.measurements[3].value_a ==
        (uint64_t)report.measurements[3].sample_count, "monitor count exact");
    CHECK(report.measurements[3].value_b >=
        (uint64_t)MOO_UI_HOST_PARITY_MIN_DPI, "minimum DPI");
    CHECK(report.measurements[3].value_c >=
        report.measurements[3].value_b, "ordered DPI range");
    CHECK(report.measurements[3].value_c <=
        (uint64_t)MOO_UI_HOST_PARITY_MAX_DPI, "maximum DPI");
    CHECK(report.missing_required_domains == UINT64_C(0),
        "required native typography, accessibility and DPI satisfied");
    CHECK(report.measurements[1].domain == 1u, "input/IME domain index");
    CHECK(report.measurements[1].evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS ||
        report.measurements[1].evidence ==
            (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED,
        "input/IME is exact native pass or explicit unsupported");
    if (report.measurements[1].evidence ==
        (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS) {
        CHECK(report.measurements[1].sample_count == 4u,
            "input/IME has four native sub-gate samples");
        CHECK(report.measurements[1].value_a == UINT64_C(2),
            "one keyboard and one pointer sequence observed");
        CHECK(report.measurements[1].value_b == UINT64_C(1),
            "one active IMM composition update observed");
        CHECK(report.measurements[1].value_c == UINT64_C(1),
            "one active IMM composition result observed");
        CHECK(report.measurements[1].native_error == 0,
            "input/IME native error clear on pass");
    } else {
        CHECK(report.measurements[1].sample_count == 0u,
            "unsupported input/IME has no samples");
        CHECK(report.measurements[1].value_a == UINT64_C(0) &&
            report.measurements[1].value_b == UINT64_C(0) &&
            report.measurements[1].value_c == UINT64_C(0),
            "unsupported input/IME has zero metrics");
        CHECK((report.measured_domains & MOO_UI_HOST_PARITY_INPUT_IME) == 0u &&
            (report.passed_domains & MOO_UI_HOST_PARITY_INPUT_IME) == 0u &&
            (report.unsupported_domains & MOO_UI_HOST_PARITY_INPUT_IME) != 0u,
            "unsupported input/IME is absent from measured and passed masks");
    }
    CHECK(report.measurements[2].domain == 2u, "accessibility domain index");
    CHECK(report.measurements[2].evidence ==
        (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_PASS,
        "native accessibility evidence pass");
    CHECK(report.measurements[2].sample_count == 3u,
        "native accessibility has exactly three independent sub-gates");
    CHECK(report.measurements[2].value_a == UINT64_C(1),
        "one MSAA role traversed");
    CHECK(report.measurements[2].value_b == UINT64_C(1),
        "one nonempty MSAA name/property read");
    CHECK(report.measurements[2].value_c == UINT64_C(1),
        "one MSAA default action invoked");
    CHECK(report.measurements[2].native_error == 0,
        "accessibility native error clear");
    CHECK(report.measurements[4].domain == 4u, "clipboard domain index");
    CHECK(report.measurements[4].evidence ==
        (uint32_t)MOO_UI_HOST_PARITY_EVIDENCE_UNSUPPORTED,
        "clipboard/drag-drop stays unsupported until both native gates pass");
    CHECK(report.measurements[4].sample_count == 0u,
        "unsupported clipboard has no samples");
    CHECK(report.measurements[4].value_a == UINT64_C(0) &&
        report.measurements[4].value_b == UINT64_C(0) &&
        report.measurements[4].value_c == UINT64_C(0),
        "unsupported clipboard has no fabricated measurements");
    CHECK(GetClipboardSequenceNumber() == clipboard_sequence_before,
        "parity probe leaves interactive clipboard sequence unchanged");
    moo_ui_host_parity_shutdown(&state);
    keyboard_layout_after = GetKeyboardLayout(0);
    keyboard_layout_name_after_ok =
        GetKeyboardLayoutNameW(keyboard_layout_name_after);
    keyboard_layout_is_ime_after = ImmIsIME(keyboard_layout_after);
    CHECK(keyboard_layout_before != NULL && keyboard_layout_after != NULL,
        "active keyboard layout handles available");
    CHECK(keyboard_layout_name_before_ok != FALSE &&
        keyboard_layout_name_after_ok != FALSE,
        "active keyboard layout names available");
    CHECK(keyboard_layout_before == keyboard_layout_after,
        "parity probe leaves active keyboard layout handle unchanged");
    CHECK(lstrcmpW(keyboard_layout_name_before,
        keyboard_layout_name_after) == 0,
        "parity probe leaves active keyboard layout name unchanged");
    CHECK(keyboard_layout_is_ime_before == keyboard_layout_is_ime_after,
        "parity probe leaves active ImmIsIME state unchanged");

    if (failures != 0u) {
        fprintf(stderr, "P016 O6 WIN32 PARITY FAIL: %u/%u\n",
            failures, checks);
        return 1;
    }
    printf("P016 O6 WIN32 PARITY GREEN: typo_samples=%u "
        "baseline_mpx=%llu advance_mpx=%llu missing_glyphs=%llu "
        "ime_ev=%u ime_samples=%u ime=%llu/%llu/%llu "
        "a11y=%llu/%llu/%llu monitors=%llu dpi=%llu..%llu "
        "hkl=%llx klid=%ls is_ime=%u inventory_unchanged=1 checks=%u\n",
        report.measurements[0].sample_count,
        (unsigned long long)report.measurements[0].value_a,
        (unsigned long long)report.measurements[0].value_b,
        (unsigned long long)report.measurements[0].value_c,
        report.measurements[1].evidence,
        report.measurements[1].sample_count,
        (unsigned long long)report.measurements[1].value_a,
        (unsigned long long)report.measurements[1].value_b,
        (unsigned long long)report.measurements[1].value_c,
        (unsigned long long)report.measurements[2].value_a,
        (unsigned long long)report.measurements[2].value_b,
        (unsigned long long)report.measurements[2].value_c,
        (unsigned long long)report.measurements[3].value_a,
        (unsigned long long)report.measurements[3].value_b,
        (unsigned long long)report.measurements[3].value_c,
        (unsigned long long)(uintptr_t)keyboard_layout_after,
        keyboard_layout_name_after,
        (unsigned)(keyboard_layout_is_ime_after != FALSE),
        checks);
    return 0;
}
#endif
