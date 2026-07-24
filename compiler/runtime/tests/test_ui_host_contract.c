/*
 * test_ui_host_contract.c — Vertragsgate fuer MooUiHostOps (NATIVE-UI-4).
 * Toolkitfrei: prueft Registry-Semantik und Vollstaendigkeits-Vertrag
 * mit einer Fake-Ops-Tabelle. Laeuft unter ASan+UBSan (mise-Task
 * test-ui-host-contract).
 */

#include "moo_ui_host.h"
#include <stdio.h>
#include <string.h>

static MooValue mv_null(void) {
    MooValue v;
    memset(&v, 0, sizeof v);
    return v;
}
static MooValue f0(void) { return mv_null(); }
static MooValue f1(MooValue a) { (void)a; return mv_null(); }
static MooValue f2(MooValue a, MooValue b) { (void)a; (void)b; return mv_null(); }
static MooValue f5(MooValue a, MooValue b, MooValue c, MooValue d, MooValue e) {
    (void)a; (void)b; (void)c; (void)d; (void)e; return mv_null();
}
static MooValue f6(MooValue a, MooValue b, MooValue c, MooValue d, MooValue e,
                   MooValue f) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return mv_null();
}
static uint32_t fake_serial(MooValue fenster) { (void)fenster; return 42u; }

static int fail(const char* grund) {
    printf("UI-HOST-CONTRACT-FAIL %s\n", grund);
    return 1;
}

int main(void) {
    MooUiHostOps ops;
    memset(&ops, 0, sizeof ops);

    if (moo_ui_host_vollstaendig(&ops)) return fail("leer-als-vollstaendig");
    if (moo_ui_host_registriere(&ops)) return fail("leer-registriert");
    if (moo_ui_host_aktiv() != NULL) return fail("aktiv-ohne-registrierung");

    ops.name = "fake";
    ops.capabilities = MOO_UI_HOST_CAP_POLL_DISPATCH;
    ops.fenster = f5;
    ops.fenster_schliessen = f1;
    ops.zeige = f1;
    ops.zeichne_frame = f6;
    ops.laufen = f0;
    ops.pump = f0;
    ops.beenden = f0;
    ops.drag_start = f1;
    ops.resize_start = f2;
    ops.minimiere = f1;
    ops.maximiere_umschalten = f1;
    ops.transparenz = f2;
    ops.cursor_setze = f2;

    /* Ein fehlender Pointer -> unvollstaendig (Stichprobe: Serial-Getter,
     * das juengste Vertragsfeld — genau das darf ein Backend nicht
     * vergessen koennen). */
    if (moo_ui_host_vollstaendig(&ops)) return fail("serial-luecke-unerkannt");
    ops.letztes_input_serial = fake_serial;
    if (!moo_ui_host_vollstaendig(&ops)) return fail("voll-als-unvollstaendig");

    if (!moo_ui_host_registriere(&ops)) return fail("registrierung");
    if (moo_ui_host_aktiv() != &ops) return fail("aktiv-falsch");

    /* Zweite Registrierung muss abgelehnt werden (ein Loop-Owner). */
    MooUiHostOps zweite = ops;
    zweite.name = "zweite";
    if (moo_ui_host_registriere(&zweite)) return fail("doppelregistrierung");
    if (moo_ui_host_aktiv() != &ops) return fail("aktiv-nach-doppel");

    if (moo_ui_host_aktiv()->letztes_input_serial(mv_null()) != 42u)
        return fail("serial-dispatch");

    printf("UI-HOST-CONTRACT-OK backend=%s caps=%u\n",
           moo_ui_host_aktiv()->name, moo_ui_host_aktiv()->capabilities);
    return 0;
}
