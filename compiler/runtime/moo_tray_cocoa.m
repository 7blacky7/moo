/*
 * moo_tray_cocoa.m — macOS-Backend fuer moo_tray.h via NSStatusItem.
 * ==================================================================
 *
 * BLIND geschriebenes Pendant zu moo_tray_linux.c. Jede Funktion ist mit
 * "// Entspricht moo_tray_linux.c:<zeile>" annotiert.
 *
 * Architektur:
 *   Tray      — NSStatusItem* (via [NSStatusBar.systemStatusBar statusItemWithLength:])
 *   Submenu   — NSMenu*
 *   Menu-Item — NSMenuItem*
 *   Check-Item — NSMenuItem mit state=ON/OFF
 *
 * Callback-Ownership:
 *   Jeder NSMenuItem bekommt einen MooCallbackTarget als target (Action =
 *   @selector(fire:)). Der Target wird via objc_setAssociatedObject mit
 *   OBJC_ASSOCIATION_RETAIN an das Item gehaengt, damit er lebt solange
 *   das Item im Menue haengt. Beim Entfernen/Ersetzen des Menues werden
 *   alle Items freigegeben → Targets dealloc-en → moo_release auf cb.
 *
 * Event-Loop: geteilt mit moo_ui_cocoa.m ([NSApp run]).
 *
 * Main-Thread-Pflicht: Alle Tray-Ops muessen auf dem Haupt-Thread laufen.
 *
 * Plan-Referenz: Memory `plan-002-moo-ui-cross-platform`.
 */

#import <Cocoa/Cocoa.h>
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <objc/runtime.h>

#include "moo_runtime.h"
#include "moo_tray.h"
#include <stdlib.h>

/* ------------------------------------------------------------------ *
 * Lokaler MooCallbackTarget — gleiche Semantik wie in moo_ui_cocoa.m,
 * hier dupliziert weil die Unit separat compiliert wird. Alternative
 * waere ein gemeinsamer Header; fuer Phase 4 reicht Duplikat.
 * (Entspricht moo_tray_linux.c:47–72 — cb_box_new/destroy + Trampoline)
 * ------------------------------------------------------------------ */

@interface MooTrayCallbackTarget : NSObject {
    @public MooValue cb;
}
- (instancetype)initWithCallback:(MooValue)callback;
- (void)fire:(id)sender;
@end

@implementation MooTrayCallbackTarget
- (instancetype)initWithCallback:(MooValue)callback {
    if ((self = [super init])) {
        cb = callback;
        moo_retain(callback);
    }
    return self;
}
- (void)fire:(id)sender {
    (void)sender;
    if (cb.tag == MOO_FUNC) {
        MooValue rv = moo_func_call_0(cb);
        moo_release(rv);
    }
}
- (void)dealloc {
    moo_release(cb);
}
@end

/* ------------------------------------------------------------------ *
 * Globale State
 * ------------------------------------------------------------------ */

static int g_ns_init = 0;

/* NSStatusItem* kann schwach referenziert werden vom NSStatusBar, wir
 * halten ALLE aktiven Status-Items stark in g_trays, damit der raw-Pointer
 * in MooValue gueltig bleibt. */
static NSMutableArray<NSStatusItem *> *g_trays = nil;

/* Timer-Register analog moo_ui_cocoa.m, aber eigener Namespace fuer
 * moo_tray_timer_add/remove. Wir unterscheiden uns NICHT von der UI-Timer-
 * Implementierung semantisch, aber auf Linux sind beide an denselben
 * g_timeout_add gebunden. Hier auch: beide teilen NSRunLoop. */
static NSMutableDictionary<NSNumber *, NSTimer *> *g_tray_timers = nil;
static unsigned long g_tray_next_timer_id = 1;

static const void *kMooTrayCBTargetKey = &kMooTrayCBTargetKey;
static const void *kMooTrayMenuKey     = &kMooTrayMenuKey;

/* ------------------------------------------------------------------ *
 * Init
 * (Entspricht moo_tray_linux.c:35–40 — ensure_gtk)
 * ------------------------------------------------------------------ */

static inline void ensure_ns(void) {
    if (g_ns_init) return;
    (void)[NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    if (!g_trays)       g_trays       = [NSMutableArray array];
    if (!g_tray_timers) g_tray_timers = [NSMutableDictionary dictionary];
    g_ns_init = 1;
}

/* ------------------------------------------------------------------ *
 * Helper — MooValue ↔ id
 * (Entspricht moo_tray_linux.c:78–97 — ptr_to_moo / tray_main_menu / cb_box_new)
 * ------------------------------------------------------------------ */

static MooValue wrap_objc(id obj) {
    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, (__bridge void *)obj);
    return v;
}

static id unwrap_objc(MooValue v) {
    if (v.tag != MOO_NUMBER) return nil;
    void *p = moo_val_as_ptr(v);
    if (!p) return nil;
    return (__bridge id)p;
}

static NSMenu *tray_main_menu(MooValue tray) {
    id t = unwrap_objc(tray);
    if (!t || ![t isKindOfClass:[NSStatusItem class]]) return nil;
    return [(NSStatusItem *)t menu];
}

/* Anhaengen eines Label-Items an ein NSMenu.
 * (Entspricht moo_tray_linux.c:100–110 — menu_append_label_item) */
static NSMenuItem *menu_append_label_item(NSMenu *menu, NSString *label,
                                           MooValue callback) {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:label
                                                  action:nil
                                           keyEquivalent:@""];
    if (callback.tag == MOO_FUNC) {
        MooTrayCallbackTarget *tgt = [[MooTrayCallbackTarget alloc]
                                        initWithCallback:callback];
        [item setTarget:tgt];
        [item setAction:@selector(fire:)];
        objc_setAssociatedObject(item, kMooTrayCBTargetKey, tgt,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    [menu addItem:item];
    return item;
}

/* Separator-Append. (Entspricht moo_tray_linux.c:113–118) */
static NSMenuItem *menu_append_separator(NSMenu *menu) {
    NSMenuItem *sep = [NSMenuItem separatorItem];
    [menu addItem:sep];
    return sep;
}

/* Check-Item-Append. (Entspricht moo_tray_linux.c:121–132) */
static NSMenuItem *menu_append_check_item(NSMenu *menu, NSString *label,
                                           BOOL initial, MooValue callback) {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:label
                                                  action:nil
                                           keyEquivalent:@""];
    [item setState:(initial ? NSControlStateValueOn : NSControlStateValueOff)];
    if (callback.tag == MOO_FUNC) {
        MooTrayCallbackTarget *tgt = [[MooTrayCallbackTarget alloc]
                                        initWithCallback:callback];
        [item setTarget:tgt];
        [item setAction:@selector(fire:)];
        objc_setAssociatedObject(item, kMooTrayCBTargetKey, tgt,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    [menu addItem:item];
    return item;
}

/* ================================================================== *
 * Tray-Icon
 * (Entspricht moo_tray_linux.c:138–181)
 * ================================================================== */

/* Entspricht moo_tray_linux.c:138–155 */
MooValue moo_tray_create(MooValue titel, MooValue icon_name) {
    @autoreleasepool {
        ensure_ns();
        NSString *t = (titel.tag == MOO_STRING)
            ? [NSString stringWithUTF8String:MV_STR(titel)->chars]
            : @"Tray";
        NSString *iconName = (icon_name.tag == MOO_STRING)
            ? [NSString stringWithUTF8String:MV_STR(icon_name)->chars]
            : nil;

        NSStatusItem *item = [[NSStatusBar systemStatusBar]
                               statusItemWithLength:NSVariableStatusItemLength];

        /* Button.title als Fallback (kein Icon-Name verfuegbar) analog GTK's
         * app_indicator-Titel. macOS hat `image` und `title` am Button. */
        if ([item respondsToSelector:@selector(button)] && [item button]) {
            [[item button] setTitle:t];
            if (iconName) {
                /* Versuche Datei-Pfad; fallback template-image. */
                NSImage *img = [[NSImage alloc] initWithContentsOfFile:iconName];
                if (!img) img = [NSImage imageNamed:iconName];
                if (img) {
                    [img setTemplate:YES];
                    [[item button] setImage:img];
                }
            }
        }

        NSMenu *menu = [[NSMenu alloc] initWithTitle:t];
        [item setMenu:menu];
        [g_trays addObject:item];

        return wrap_objc(item);
    }
}

/* Entspricht moo_tray_linux.c:157–163 */
MooValue moo_tray_titel_setze(MooValue tray, MooValue titel) {
    @autoreleasepool {
        id t = unwrap_objc(tray);
        if (!t || ![t isKindOfClass:[NSStatusItem class]]) return moo_bool(0);
        NSString *s = (titel.tag == MOO_STRING)
            ? [NSString stringWithUTF8String:MV_STR(titel)->chars] : @"";
        NSStatusItem *si = (NSStatusItem *)t;
        if ([si respondsToSelector:@selector(button)] && [si button]) {
            [[si button] setTitle:s];
        }
        return moo_bool(1);
    }
}

/* Entspricht moo_tray_linux.c:165–172 */
MooValue moo_tray_icon_setze(MooValue tray, MooValue icon_name) {
    @autoreleasepool {
        id t = unwrap_objc(tray);
        if (!t || ![t isKindOfClass:[NSStatusItem class]]) return moo_bool(0);
        NSString *n = (icon_name.tag == MOO_STRING)
            ? [NSString stringWithUTF8String:MV_STR(icon_name)->chars] : nil;
        if (!n) return moo_bool(0);
        NSImage *img = [[NSImage alloc] initWithContentsOfFile:n];
        if (!img) img = [NSImage imageNamed:n];
        if (!img) return moo_bool(0);
        [img setTemplate:YES];
        NSStatusItem *si = (NSStatusItem *)t;
        if ([si respondsToSelector:@selector(button)] && [si button]) {
            [[si button] setImage:img];
        }
        return moo_bool(1);
    }
}

/* Entspricht moo_tray_linux.c:174–181 */
MooValue moo_tray_aktiv(MooValue tray, MooValue aktiv) {
    @autoreleasepool {
        id t = unwrap_objc(tray);
        if (!t || ![t isKindOfClass:[NSStatusItem class]]) return moo_bool(0);
        BOOL an = (aktiv.tag == MOO_BOOL) ? (BOOL)MV_BOOL(aktiv) : YES;
        NSStatusItem *si = (NSStatusItem *)t;
        /* NSStatusItem kennt kein ACTIVE/PASSIVE wie AppIndicator; wir
         * simulieren es ueber sichtbar/unsichtbar. */
        if ([si respondsToSelector:@selector(setVisible:)]) {
            [si setVisible:an];
        } else if ([si respondsToSelector:@selector(button)] && [si button]) {
            [[si button] setHidden:!an];
        }
        return moo_bool(1);
    }
}

/* ================================================================== *
 * Menu (flach) — Top-Level
 * (Entspricht moo_tray_linux.c:187–230)
 * ================================================================== */

/* Entspricht moo_tray_linux.c:187–193 */
MooValue moo_tray_menu_add(MooValue tray, MooValue label, MooValue callback) {
    @autoreleasepool {
        NSMenu *menu = tray_main_menu(tray);
        if (!menu) return moo_bool(0);
        NSString *l = (label.tag == MOO_STRING)
            ? [NSString stringWithUTF8String:MV_STR(label)->chars] : @"(?)";
        NSMenuItem *item = menu_append_label_item(menu, l, callback);
        return wrap_objc(item);
    }
}

/* Entspricht moo_tray_linux.c:195–200 */
MooValue moo_tray_separator_add(MooValue tray) {
    @autoreleasepool {
        NSMenu *menu = tray_main_menu(tray);
        if (!menu) return moo_bool(0);
        NSMenuItem *sep = menu_append_separator(menu);
        return wrap_objc(sep);
    }
}

/* Entspricht moo_tray_linux.c:202–230 — menu_clear: neues Menue anlegen,
 * altes zerstoert sich automatisch sobald keine Referenz mehr drauf zeigt.
 * Da wir associated-objects mit RETAIN auf den Items haben, werden die
 * Callback-Targets released, sobald die Items zerstoert werden. */
MooValue moo_tray_menu_clear(MooValue tray) {
    @autoreleasepool {
        id t = unwrap_objc(tray);
        if (!t || ![t isKindOfClass:[NSStatusItem class]]) return moo_bool(0);
        NSStatusItem *si = (NSStatusItem *)t;
        NSMenu *old = [si menu];
        NSMenu *neu = [[NSMenu alloc] initWithTitle:[old title] ?: @""];
        [si setMenu:neu];
        /* old wird freigegeben sobald kein Besitzer mehr drauf zeigt; ARC
         * cleaned up automatisch. Items + CB-Targets deallocen cascading. */
        (void)old;
        return moo_bool(1);
    }
}

/* ================================================================== *
 * Submenues
 * (Entspricht moo_tray_linux.c:236–268)
 * ================================================================== */

/* Entspricht moo_tray_linux.c:236–253 */
MooValue moo_tray_submenu_add(MooValue tray, MooValue label) {
    @autoreleasepool {
        NSMenu *menu = tray_main_menu(tray);
        if (!menu) return moo_bool(0);
        NSString *l = (label.tag == MOO_STRING)
            ? [NSString stringWithUTF8String:MV_STR(label)->chars] : @"(?)";
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:l
                                                      action:nil
                                               keyEquivalent:@""];
        NSMenu *sub = [[NSMenu alloc] initWithTitle:l];
        [item setSubmenu:sub];
        [menu addItem:item];
        return wrap_objc(sub);
    }
}

/* Entspricht moo_tray_linux.c:255–261 */
MooValue moo_tray_menu_add_to(MooValue submenu, MooValue label, MooValue callback) {
    @autoreleasepool {
        id m = unwrap_objc(submenu);
        if (!m || ![m isKindOfClass:[NSMenu class]]) return moo_bool(0);
        NSString *l = (label.tag == MOO_STRING)
            ? [NSString stringWithUTF8String:MV_STR(label)->chars] : @"(?)";
        NSMenuItem *item = menu_append_label_item((NSMenu *)m, l, callback);
        return wrap_objc(item);
    }
}

/* Entspricht moo_tray_linux.c:263–268 */
MooValue moo_tray_separator_add_to(MooValue submenu) {
    @autoreleasepool {
        id m = unwrap_objc(submenu);
        if (!m || ![m isKindOfClass:[NSMenu class]]) return moo_bool(0);
        NSMenuItem *sep = menu_append_separator((NSMenu *)m);
        return wrap_objc(sep);
    }
}

/* ================================================================== *
 * Check-Items
 * (Entspricht moo_tray_linux.c:274–310)
 * ================================================================== */

/* Entspricht moo_tray_linux.c:274–282 */
MooValue moo_tray_check_add(MooValue tray, MooValue label, MooValue initial,
                            MooValue callback) {
    @autoreleasepool {
        NSMenu *menu = tray_main_menu(tray);
        if (!menu) return moo_bool(0);
        NSString *l = (label.tag == MOO_STRING)
            ? [NSString stringWithUTF8String:MV_STR(label)->chars] : @"(?)";
        BOOL init = (initial.tag == MOO_BOOL) ? (BOOL)MV_BOOL(initial) : NO;
        NSMenuItem *item = menu_append_check_item(menu, l, init, callback);
        return wrap_objc(item);
    }
}

/* Entspricht moo_tray_linux.c:284–292 */
MooValue moo_tray_check_add_to(MooValue submenu, MooValue label,
                               MooValue initial, MooValue callback) {
    @autoreleasepool {
        id m = unwrap_objc(submenu);
        if (!m || ![m isKindOfClass:[NSMenu class]]) return moo_bool(0);
        NSString *l = (label.tag == MOO_STRING)
            ? [NSString stringWithUTF8String:MV_STR(label)->chars] : @"(?)";
        BOOL init = (initial.tag == MOO_BOOL) ? (BOOL)MV_BOOL(initial) : NO;
        NSMenuItem *item = menu_append_check_item((NSMenu *)m, l, init, callback);
        return wrap_objc(item);
    }
}

/* Entspricht moo_tray_linux.c:294–299 */
MooValue moo_tray_check_wert(MooValue check_item) {
    @autoreleasepool {
        id i = unwrap_objc(check_item);
        if (!i || ![i isKindOfClass:[NSMenuItem class]]) return moo_bool(0);
        return moo_bool([(NSMenuItem *)i state] == NSControlStateValueOn);
    }
}

/* Entspricht moo_tray_linux.c:301–310 */
MooValue moo_tray_check_set(MooValue check_item, MooValue wert) {
    @autoreleasepool {
        id i = unwrap_objc(check_item);
        if (!i || ![i isKindOfClass:[NSMenuItem class]]) return moo_bool(0);
        BOOL an = (wert.tag == MOO_BOOL) ? (BOOL)MV_BOOL(wert) : NO;
        NSMenuItem *it = (NSMenuItem *)i;
        NSControlStateValue oldState = [it state];
        NSControlStateValue newState = an ? NSControlStateValueOn : NSControlStateValueOff;
        [it setState:newState];
        /* Linux feuert "toggled" nur bei Aenderung → wir spiegeln das,
         * indem wir bei echter Aenderung die action manuell ausloesen. */
        if (oldState != newState && [it target] && [it action]) {
            [NSApp sendAction:[it action] to:[it target] from:it];
        }
        return moo_bool(1);
    }
}

/* ================================================================== *
 * Item-Manipulation
 * (Entspricht moo_tray_linux.c:316–329)
 * ================================================================== */

/* Entspricht moo_tray_linux.c:316–322 */
MooValue moo_tray_item_aktiv(MooValue item, MooValue aktiv) {
    @autoreleasepool {
        id i = unwrap_objc(item);
        if (!i || ![i isKindOfClass:[NSMenuItem class]]) return moo_bool(0);
        BOOL en = (aktiv.tag == MOO_BOOL) ? (BOOL)MV_BOOL(aktiv) : YES;
        [(NSMenuItem *)i setEnabled:en];
        return moo_bool(1);
    }
}

/* Entspricht moo_tray_linux.c:324–329 */
MooValue moo_tray_item_label_setze(MooValue item, MooValue label) {
    @autoreleasepool {
        id i = unwrap_objc(item);
        if (!i || ![i isKindOfClass:[NSMenuItem class]]) return moo_bool(0);
        NSString *l = (label.tag == MOO_STRING)
            ? [NSString stringWithUTF8String:MV_STR(label)->chars] : @"";
        [(NSMenuItem *)i setTitle:l];
        return moo_bool(1);
    }
}

/* ================================================================== *
 * Timer
 * (Entspricht moo_tray_linux.c:336–371)
 * ================================================================== */

@interface MooTrayTimerTarget : MooTrayCallbackTarget
@end
@implementation MooTrayTimerTarget
- (void)tick:(NSTimer *)timer {
    (void)timer;
    [self fire:nil];
}
@end

/* Entspricht moo_tray_linux.c:351–361 */
MooValue moo_tray_timer_add(MooValue interval_ms, MooValue callback) {
    @autoreleasepool {
        ensure_ns();
        int ms = (interval_ms.tag == MOO_NUMBER) ? (int)MV_NUM(interval_ms) : 1000;
        MooTrayTimerTarget *tgt = [[MooTrayTimerTarget alloc]
                                    initWithCallback:callback];
        NSTimer *t = [NSTimer scheduledTimerWithTimeInterval:(ms / 1000.0)
                                                      target:tgt
                                                    selector:@selector(tick:)
                                                    userInfo:nil
                                                     repeats:YES];
        unsigned long id_ = g_tray_next_timer_id++;
        g_tray_timers[@(id_)] = t;
        return moo_number((double)id_);
    }
}

/* Entspricht moo_tray_linux.c:363–371 */
MooValue moo_tray_timer_remove(MooValue timer_id) {
    @autoreleasepool {
        if (timer_id.tag != MOO_NUMBER) return moo_bool(0);
        unsigned long id_ = (unsigned long)MV_NUM(timer_id);
        if (id_ == 0) return moo_bool(0);
        NSTimer *t = g_tray_timers[@(id_)];
        if (!t) return moo_bool(0);
        [t invalidate];
        [g_tray_timers removeObjectForKey:@(id_)];
        return moo_bool(1);
    }
}

/* ================================================================== *
 * Event-Loop (Legacy-Alias, siehe moo_tray.h)
 * (Entspricht moo_tray_linux.c:377–381)
 * ================================================================== */

MooValue moo_tray_run(void) {
    @autoreleasepool {
        ensure_ns();
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
    }
    return moo_none();
}
