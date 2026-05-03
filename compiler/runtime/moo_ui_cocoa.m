/*
 * moo_ui_cocoa.m — macOS-Backend fuer moo_ui.h via AppKit/Cocoa.
 * ==============================================================
 *
 * BLIND geschriebenes Pendant zu moo_ui_gtk.c. Jede Funktion ist mit einem
 * Kommentar "// Entspricht moo_ui_gtk.c:<zeile>" markiert. Test erfolgt
 * erst auf einem macOS-Runner (GitHub Actions macos-latest).
 *
 * ARC: build.rs setzt -fobjc-arc → alle NSObject-Instanzen werden automatisch
 * refcounted. C-seitige MooValue-Callbacks werden explizit via moo_retain/
 * moo_release verwaltet; die ObjC-Seite (MooCallbackTarget) released die
 * MooValue in -dealloc.
 *
 * Koordinaten: GTK nutzt top-left, Cocoa per Default bottom-left. Wir
 * benutzen einen flipped NSView (isFlipped=YES) als contentView und fuer
 * alle Child-Container — damit sind x/y/w/h identisch zu GTK.
 *
 * Handle-Layout:
 *   MooValue { tag=MOO_NUMBER, data=raw pointer to NSObject }
 *   Der Pointer bleibt gueltig solange der Parent-Superview oder ein
 *   globales Register (g_windows) eine starke Referenz haelt.
 *
 * Main-Thread-Pflicht: Alle UI-API-Aufrufe MUESSEN auf dem Haupt-Thread
 * laufen. moo_ui_laufen blockiert den Haupt-Thread in [NSApp run].
 *
 * Plan-Referenz: Memory `plan-002-moo-ui-cross-platform`.
 */

#import <Cocoa/Cocoa.h>
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <objc/runtime.h>

#include "moo_runtime.h"
#include "moo_ui.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ------------------------------------------------------------------ *
 * Forward Declarations: ObjC-Hilfsklassen
 * ------------------------------------------------------------------ */

@class MooCallbackTarget;
@class MooFlippedView;
@class MooWindowDelegate;
@class MooTableDataSource;

/* Entspricht moo_ui_gtk.c:34 — globaler Init-Guard + Fenster-Zaehler. */
static int    g_cocoa_init   = 0;
static int    g_open_windows = 0;
static int    g_ui_debug_on  = 0;

/* Haelt ALLE aktiven NSWindow* stark, damit sie nicht von ARC freigegeben
 * werden sobald C-Code nur noch einen raw pointer haelt. Bei windowWillClose
 * wird das Fenster wieder entfernt. */
static NSMutableArray<NSWindow *> *g_windows = nil;

/* Globales Timer-Register fuer moo_ui_timer_entfernen. Key = NSNumber(id),
 * Value = NSTimer*. Der NSRunLoop haelt den Timer ebenfalls stark, sodass
 * wir uns auf diese Verwaltung verlassen koennen. */
static NSMutableDictionary<NSNumber *, NSTimer *> *g_timers = nil;
static unsigned long g_next_timer_id = 1;

/* Associated-Object-Keys. Wir muessen Pointer-Identitaeten haben, die
 * global lebendig sind — `static const char` reicht. */
static const void *kMooFixedKey          = &kMooFixedKey;
static const void *kMooVBoxKey           = &kMooVBoxKey;
static const void *kMooMenuBarKey        = &kMooMenuBarKey;
static const void *kMooCBTargetKey       = &kMooCBTargetKey;
static const void *kMooCBTargetCloseKey  = &kMooCBTargetCloseKey;
static const void *kMooCBTargetChangeKey = &kMooCBTargetChangeKey;
static const void *kMooCBTargetDrawKey   = &kMooCBTargetDrawKey;
static const void *kMooCBTargetMouseKey  = &kMooCBTargetMouseKey;
static const void *kMooCBTargetMotionKey = &kMooCBTargetMotionKey;
static const void *kMooTableDSKey        = &kMooTableDSKey;
static const void *kMooTVKey             = &kMooTVKey;
static const void *kMooScrollViewKey     = &kMooScrollViewKey;
static const void *kMooNColsKey          = &kMooNColsKey;
static const void *kMooRadioGroupKey     = &kMooRadioGroupKey;
static const void *kMooWindowDelegateKey = &kMooWindowDelegateKey;
static const void *kMooShortcutsKey      = &kMooShortcutsKey;
static const void *kMooCanvasTrackingKey = &kMooCanvasTrackingKey;
static const void *kMooUIIdKey           = &kMooUIIdKey;  /* Plan-004 P1: widget-ID */

/* ------------------------------------------------------------------ *
 * MooCallbackTarget — Target-Action-Bruecke fuer MooValue-Callbacks
 * (Entspricht moo_ui_gtk.c:55–75 — cb_box_destroy / cb_box_new / Trampoline)
 * ------------------------------------------------------------------ */

@interface MooCallbackTarget : NSObject {
    @public MooValue cb;
}
- (instancetype)initWithCallback:(MooValue)callback;
- (void)fire:(id)sender;                /* 0-args moo-Call */
- (BOOL)fireCloseAllow;                 /* callback-Return als BOOL */
- (void)fireWithHandle:(id)sender;      /* 1-arg moo-Call mit Widget-Handle */
- (void)fireWithHandle:(id)sender zeichner:(void *)zptr; /* 2-arg: leinwand, zeichner */
@end

@implementation MooCallbackTarget
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
- (BOOL)fireCloseAllow {
    if (cb.tag != MOO_FUNC) return YES;
    MooValue rv = moo_func_call_0(cb);
    BOOL allow = moo_is_truthy(rv) ? YES : NO;
    moo_release(rv);
    return allow;
}
- (void)fireWithHandle:(id)sender {
    if (cb.tag != MOO_FUNC) return;
    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, (__bridge void *)sender);
    MooValue rv = moo_func_call_1(cb, v);
    moo_release(rv);
}
- (void)fireWithHandle:(id)sender zeichner:(void *)zptr {
    if (cb.tag != MOO_FUNC) return;
    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, (__bridge void *)sender);
    MooValue z;
    z.tag = MOO_NUMBER;
    moo_val_set_ptr(&z, zptr);
    MooValue rv = moo_func_call_2(cb, v, z);
    moo_release(rv);
}
- (void)dealloc {
    /* moo-Referenz abgeben, damit der Callback ggf. freigegeben wird. */
    moo_release(cb);
}
@end

/* ------------------------------------------------------------------ *
 * MooFlippedView — NSView mit top-left-Origin (wie GtkFixed)
 * (Entspricht moo_ui_gtk.c:96–102 / 117–121 — resolve_container, place_child)
 * ------------------------------------------------------------------ */

@interface MooFlippedView : NSView
@end

@implementation MooFlippedView
- (BOOL)isFlipped { return YES; }
@end

/* ------------------------------------------------------------------ *
 * MooWindowDelegate — NSWindowDelegate fuer close/destroy Handling
 * (Entspricht moo_ui_gtk.c:160–168 — on_window_destroy
 *  + moo_ui_gtk.c:260–279 — on_delete_event_trampoline / on_close)
 * ------------------------------------------------------------------ */

@interface MooWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, strong, nullable) MooCallbackTarget *closeTarget;
@end

@implementation MooWindowDelegate
/* windowShouldClose: wird vor dem Schliessen gefragt. Liefert callback wahr
 * → YES (schliessen), sonst NO (abbrechen). Analog zu GTK "delete-event". */
- (BOOL)windowShouldClose:(NSWindow *)sender {
    (void)sender;
    if (self.closeTarget) {
        return [self.closeTarget fireCloseAllow];
    }
    return YES;
}
/* windowWillClose: echtes destroy-Pendant. Fenster aus g_windows entfernen,
 * Zaehler runter, ggf. Event-Loop stoppen. */
- (void)windowWillClose:(NSNotification *)notification {
    NSWindow *win = notification.object;
    if (g_open_windows > 0) g_open_windows--;
    if (win) [g_windows removeObject:win];
    if (g_open_windows == 0 && [NSApp isRunning]) {
        [NSApp stop:nil];
        /* [NSApp stop:] greift erst nach dem naechsten Event — ein Dummy-
         * Event posten, damit das Return aus [NSApp run] sofort passiert. */
        NSEvent *dummy = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                            location:NSZeroPoint
                                       modifierFlags:0
                                           timestamp:0
                                        windowNumber:0
                                             context:nil
                                             subtype:0
                                               data1:0
                                               data2:0];
        [NSApp postEvent:dummy atStart:YES];
    }
}
@end

/* ------------------------------------------------------------------ *
 * MooTableDataSource — NSTableViewDataSource fuer NSTableView
 * (Entspricht moo_ui_gtk.c:573–707 — Liste-Funktionen via GtkListStore)
 * ------------------------------------------------------------------ */

@interface MooTableDataSource : NSObject <NSTableViewDataSource, NSTableViewDelegate>
@property (nonatomic, strong) NSMutableArray<NSArray<NSString *> *> *rows;
@property (nonatomic, assign) NSInteger ncols;
@property (nonatomic, strong, nullable) MooCallbackTarget *selectTarget;
@end

@implementation MooTableDataSource
- (instancetype)init {
    if ((self = [super init])) {
        _rows = [NSMutableArray array];
        _ncols = 1;
    }
    return self;
}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    (void)tableView;
    return (NSInteger)self.rows.count;
}
- (id)tableView:(NSTableView *)tableView
objectValueForTableColumn:(NSTableColumn *)tableColumn
            row:(NSInteger)row {
    (void)tableView;
    if (row < 0 || row >= (NSInteger)self.rows.count) return @"";
    NSArray<NSString *> *r = self.rows[row];
    NSInteger col = [tableView.tableColumns indexOfObject:tableColumn];
    if (col == NSNotFound || col < 0 || col >= (NSInteger)r.count) return @"";
    return r[col];
}
- (void)tableViewSelectionDidChange:(NSNotification *)notification {
    (void)notification;
    if (self.selectTarget) [self.selectTarget fire:nil];
}
@end

/* ------------------------------------------------------------------ *
 * Cocoa-Init + Debug-Log (Entspricht moo_ui_gtk.c:34–49)
 * ------------------------------------------------------------------ */

static inline void ensure_cocoa(void) {
    if (g_cocoa_init) return;
    /* NSApplication muss auf dem Haupt-Thread initialisiert werden. */
    (void)[NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    if (!g_windows) g_windows = [NSMutableArray array];
    if (!g_timers)  g_timers  = [NSMutableDictionary dictionary];
    g_cocoa_init = 1;
}

static void ui_log(const char *fmt, const char *arg) {
    if (!g_ui_debug_on) return;
    if (arg) fprintf(stderr, "[moo_ui] %s: %s\n", fmt, arg);
    else     fprintf(stderr, "[moo_ui] %s\n", fmt);
}

/* ------------------------------------------------------------------ *
 * Helpers: MooValue ↔ Cocoa, Parent-Container-Aufloesung
 * (Entspricht moo_ui_gtk.c:80–121)
 * ------------------------------------------------------------------ */

/* wrap_widget (Entspricht moo_ui_gtk.c:80–85). Wir packen einen raw void*
 * ein — der __bridge-Cast macht NICHTS am Retain-Count, der Superview/
 * die globale Liste haelt die Referenz. */
static inline MooValue wrap_objc(id obj) {
    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, (__bridge void *)obj);
    return v;
}

/* unwrap_widget (Entspricht moo_ui_gtk.c:87–90). */
static inline id unwrap_objc(MooValue v) {
    if (v.tag != MOO_NUMBER) return nil;
    void *p = moo_val_as_ptr(v);
    if (!p) return nil;
    return (__bridge id)p;
}

/* resolve_container (Entspricht moo_ui_gtk.c:96–102): liefert den
 * MooFlippedView, in den Kinder gesetzt werden. Fenster → associated
 * "moo-fixed"; Tab-Content ist direkt ein MooFlippedView; Rahmen (NSBox)
 * hat seinen contentView als MooFlippedView (content-dance bei NSBox). */
static MooFlippedView *resolve_container(MooValue parent) {
    id p = unwrap_objc(parent);
    if (!p) return nil;
    id fx = objc_getAssociatedObject(p, kMooFixedKey);
    if (fx && [fx isKindOfClass:[MooFlippedView class]]) return (MooFlippedView *)fx;
    if ([p isKindOfClass:[MooFlippedView class]]) return (MooFlippedView *)p;
    /* NSBox: contentView abrufen */
    if ([p isKindOfClass:[NSBox class]]) {
        id cv = [(NSBox *)p contentView];
        if ([cv isKindOfClass:[MooFlippedView class]]) return (MooFlippedView *)cv;
    }
    return nil;
}

/* str_or (Entspricht moo_ui_gtk.c:104–106). */
static inline const char *str_or(MooValue v, const char *fallback) {
    return (v.tag == MOO_STRING) ? MV_STR(v)->chars : fallback;
}

/* num_or (Entspricht moo_ui_gtk.c:108–110). */
static inline int num_or(MooValue v, int fallback) {
    return (v.tag == MOO_NUMBER) ? (int)MV_NUM(v) : fallback;
}

/* bool_or (Entspricht moo_ui_gtk.c:112–115). */
static inline BOOL bool_or(MooValue v, BOOL fallback) {
    if (v.tag == MOO_BOOL) return (BOOL)MV_BOOL(v);
    return fallback;
}

/* place_child (Entspricht moo_ui_gtk.c:117–121). Setzt frame + add. */
static void place_child(MooFlippedView *fx, NSView *child,
                        int x, int y, int b, int h) {
    if (!fx || !child) return;
    /* Wenn w/h nicht gesetzt (GTK uebergibt -1), intrinsicContentSize nehmen. */
    if (b <= 0 || h <= 0) {
        NSSize s = child.intrinsicContentSize;
        if (b <= 0) b = (s.width  > 0) ? (int)s.width  : 100;
        if (h <= 0) h = (s.height > 0) ? (int)s.height : 24;
    }
    [child setFrame:NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)b, (CGFloat)h)];
    [fx addSubview:child];
}

/* NSString aus MooValue (UTF8). */
static inline NSString *nsstr_or(MooValue v, NSString *fallback) {
    if (v.tag == MOO_STRING) {
        return [NSString stringWithUTF8String:MV_STR(v)->chars];
    }
    return fallback;
}

/* ------------------------------------------------------------------ *
 * Initialisierung & Event-Loop
 * (Entspricht moo_ui_gtk.c:127–154)
 * ------------------------------------------------------------------ */

/* Entspricht moo_ui_gtk.c:127–130 */
MooValue moo_ui_init(void) {
    @autoreleasepool {
        ensure_cocoa();
    }
    return moo_bool(1);
}

/* Entspricht moo_ui_gtk.c:132–138 */
MooValue moo_ui_laufen(void) {
    @autoreleasepool {
        ensure_cocoa();
        ui_log("ui_laufen: enter main loop", NULL);
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
        ui_log("ui_laufen: exit main loop", NULL);
    }
    return moo_none();
}

/* Entspricht moo_ui_gtk.c:140–143 */
MooValue moo_ui_beenden(void) {
    @autoreleasepool {
        if ([NSApp isRunning]) {
            [NSApp stop:nil];
            NSEvent *dummy = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                                location:NSZeroPoint
                                           modifierFlags:0
                                               timestamp:0
                                            windowNumber:0
                                                 context:nil
                                                 subtype:0
                                                   data1:0
                                                   data2:0];
            [NSApp postEvent:dummy atStart:YES];
        }
    }
    return moo_none();
}

/* Entspricht moo_ui_gtk.c:145–149 */
MooValue moo_ui_pump(void) {
    @autoreleasepool {
        ensure_cocoa();
        NSEvent *ev;
        while ((ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:[NSDate distantPast]
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES]) != nil) {
            [NSApp sendEvent:ev];
        }
    }
    return moo_none();
}

/* Entspricht moo_ui_gtk.c:151–154 */
MooValue moo_ui_debug(MooValue an) {
    g_ui_debug_on = bool_or(an, NO) ? 1 : 0;
    return moo_bool(g_ui_debug_on);
}

/* ------------------------------------------------------------------ *
 * Fenster & Top-Level (Entspricht moo_ui_gtk.c:156–279)
 * ------------------------------------------------------------------ */

/* Entspricht moo_ui_gtk.c:170–210 */
MooValue moo_ui_fenster(MooValue titel, MooValue breite, MooValue hoehe,
                        MooValue flags, MooValue parent) {
    @autoreleasepool {
        ensure_cocoa();
        NSString *t = nsstr_or(titel, @"moo");
        int w = num_or(breite, 640);
        int h = num_or(hoehe, 480);
        unsigned f = (unsigned)num_or(flags, 0);

        NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
        if (f & MOO_UI_FLAG_RESIZABLE) style |= NSWindowStyleMaskResizable;
        if (f & MOO_UI_FLAG_NO_DECOR)  style  = NSWindowStyleMaskBorderless;

        NSRect rect = NSMakeRect(100, 100, (CGFloat)w, (CGFloat)h);
        NSWindow *win = [[NSWindow alloc] initWithContentRect:rect
                                                    styleMask:style
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
        [win setTitle:t];
        [win setReleasedWhenClosed:NO];  /* Wir verwalten selbst via g_windows */

        /* Fullscreen/maximized/always-top-Flags (Entspricht gtk-branch :185–189) */
        if (f & MOO_UI_FLAG_MAXIMIZED) {
            [win zoom:nil];
        }
        if (f & MOO_UI_FLAG_FULLSCREEN) {
            [win toggleFullScreen:nil];
        }
        if (f & MOO_UI_FLAG_ALWAYS_TOP) {
            [win setLevel:NSFloatingWindowLevel];
        }

        /* Transient-for / modal (Entspricht gtk-branch :191–194 + :187) */
        id pw = unwrap_objc(parent);
        if (pw && [pw isKindOfClass:[NSWindow class]]) {
            [(NSWindow *)pw addChildWindow:win ordered:NSWindowAbove];
        }
        if (f & MOO_UI_FLAG_MODAL) {
            /* Echte Modal-Behandlung lauft ueber [NSApp runModalForWindow:] —
             * hier markieren wir das Fenster; moo_ui_zeige startet die Modal-
             * Schleife. Fuer Phase 4 reicht Ebene erhoehen. */
            [win setLevel:NSModalPanelWindowLevel];
        }

        /* vbox + fixed wie GTK: wir bauen eine vertikale Kette aus zwei
         * MooFlippedViews. MenuBar wird auf macOS global gesetzt
         * ([NSApp setMainMenu:]), nicht pro Fenster — aber wir halten die
         * vbox-Struktur fuer API-Konsistenz. (Entspricht gtk-branch :196–203) */
        NSRect bounds = [[win contentView] bounds];
        MooFlippedView *vbox = [[MooFlippedView alloc]
                                 initWithFrame:bounds];
        [vbox setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        [win setContentView:vbox];

        MooFlippedView *fx = [[MooFlippedView alloc]
                              initWithFrame:[vbox bounds]];
        [fx setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        [vbox addSubview:fx];

        objc_setAssociatedObject(win, kMooVBoxKey,  vbox, OBJC_ASSOCIATION_ASSIGN);
        objc_setAssociatedObject(win, kMooFixedKey, fx,   OBJC_ASSOCIATION_ASSIGN);

        /* Window-Delegate fuer close-Handling (Entspricht gtk-branch :206). */
        MooWindowDelegate *del = [[MooWindowDelegate alloc] init];
        [win setDelegate:del];
        objc_setAssociatedObject(win, kMooWindowDelegateKey, del,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        g_open_windows++;
        [g_windows addObject:win];

        ui_log("ui_fenster", [t UTF8String]);
        return wrap_objc(win);
    }
}

/* Entspricht moo_ui_gtk.c:212–217 */
MooValue moo_ui_fenster_titel_setze(MooValue fenster, MooValue titel) {
    @autoreleasepool {
        id w = unwrap_objc(fenster);
        if (!w || ![w isKindOfClass:[NSWindow class]]) return moo_bool(0);
        [(NSWindow *)w setTitle:nsstr_or(titel, @"")];
        return moo_bool(1);
    }
}

/* Entspricht moo_ui_gtk.c:219–224 */
MooValue moo_ui_fenster_icon_setze(MooValue fenster, MooValue pfad) {
    @autoreleasepool {
        id w = unwrap_objc(fenster);
        if (!w || ![w isKindOfClass:[NSWindow class]]) return moo_bool(0);
        NSString *p = nsstr_or(pfad, @"");
        if ([p length] == 0) return moo_bool(0);
        NSImage *img = [[NSImage alloc] initWithContentsOfFile:p];
        if (img) {
            /* macOS hat keinen echten Per-Fenster-Icon — wir setzen es als
             * App-Icon, was fuer Single-Window-Apps die uebliche Praxis ist. */
            [NSApp setApplicationIconImage:img];
        }
        return moo_bool(img != nil);
    }
}

/* Entspricht moo_ui_gtk.c:226–231 */
MooValue moo_ui_fenster_groesse_setze(MooValue fenster, MooValue b, MooValue h) {
    @autoreleasepool {
        id w = unwrap_objc(fenster);
        if (!w || ![w isKindOfClass:[NSWindow class]]) return moo_bool(0);
        NSRect f = [(NSWindow *)w frame];
        f.size.width  = num_or(b, 100);
        f.size.height = num_or(h, 100);
        [(NSWindow *)w setFrame:f display:YES];
        return moo_bool(1);
    }
}

/* Entspricht moo_ui_gtk.c:233–238 */
MooValue moo_ui_fenster_position_setze(MooValue fenster, MooValue x, MooValue y) {
    @autoreleasepool {
        id w = unwrap_objc(fenster);
        if (!w || ![w isKindOfClass:[NSWindow class]]) return moo_bool(0);
        NSWindow *win = (NSWindow *)w;
        /* GTK-Semantik: x/y im top-left Screen-Koord-System. Cocoa hat
         * bottom-left → wir konvertieren ueber die main-Screen-Hoehe. */
        CGFloat sh = [[NSScreen mainScreen] frame].size.height;
        NSRect f = [win frame];
        f.origin.x = (CGFloat)num_or(x, 0);
        f.origin.y = sh - (CGFloat)num_or(y, 0) - f.size.height;
        [win setFrame:f display:YES];
        return moo_bool(1);
    }
}

/* Entspricht moo_ui_gtk.c:240–245 */
MooValue moo_ui_fenster_schliessen(MooValue fenster) {
    @autoreleasepool {
        id w = unwrap_objc(fenster);
        if (!w) return moo_bool(0);
        if ([w isKindOfClass:[NSWindow class]]) {
            [(NSWindow *)w close];
            return moo_bool(1);
        }
        return moo_bool(0);
    }
}

/* Entspricht moo_ui_gtk.c:247–252 */
MooValue moo_ui_zeige(MooValue fenster) {
    @autoreleasepool {
        id w = unwrap_objc(fenster);
        if (!w) return moo_bool(0);
        if ([w isKindOfClass:[NSWindow class]]) {
            [(NSWindow *)w makeKeyAndOrderFront:nil];
            return moo_bool(1);
        }
        if ([w isKindOfClass:[NSView class]]) {
            [(NSView *)w setHidden:NO];
            return moo_bool(1);
        }
        return moo_bool(0);
    }
}

/* Entspricht moo_ui_gtk.c:254–256 — Alias. */
MooValue moo_ui_zeige_nebenbei(MooValue fenster) {
    return moo_ui_zeige(fenster);
}

/* Entspricht moo_ui_gtk.c:271–279 */
MooValue moo_ui_fenster_on_close(MooValue fenster, MooValue callback) {
    @autoreleasepool {
        id w = unwrap_objc(fenster);
        if (!w || ![w isKindOfClass:[NSWindow class]]) return moo_bool(0);
        MooWindowDelegate *del = objc_getAssociatedObject(w, kMooWindowDelegateKey);
        if (!del) return moo_bool(0);
        MooCallbackTarget *tgt = [[MooCallbackTarget alloc] initWithCallback:callback];
        del.closeTarget = tgt;  /* strong retain via property */
        return moo_bool(1);
    }
}

/* ------------------------------------------------------------------ *
 * Label + Knopf (Entspricht moo_ui_gtk.c:285–324)
 * ------------------------------------------------------------------ */

/* Entspricht moo_ui_gtk.c:285–294 */
MooValue moo_ui_label(MooValue parent, MooValue text,
                      MooValue x, MooValue y, MooValue b, MooValue h) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);
        NSTextField *lbl = [[NSTextField alloc] initWithFrame:NSZeroRect];
        [lbl setStringValue:nsstr_or(text, @"")];
        [lbl setEditable:NO];
        [lbl setSelectable:NO];
        [lbl setBezeled:NO];
        [lbl setDrawsBackground:NO];
        [lbl setAlignment:NSTextAlignmentLeft];
        place_child(fx, lbl, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
        return wrap_objc(lbl);
    }
}

/* Entspricht moo_ui_gtk.c:296–301 */
MooValue moo_ui_label_setze(MooValue label, MooValue text) {
    @autoreleasepool {
        id l = unwrap_objc(label);
        if (!l || ![l isKindOfClass:[NSTextField class]]) return moo_bool(0);
        [(NSTextField *)l setStringValue:nsstr_or(text, @"")];
        return moo_bool(1);
    }
}

/* Entspricht moo_ui_gtk.c:303–308 */
MooValue moo_ui_label_text(MooValue label) {
    @autoreleasepool {
        id l = unwrap_objc(label);
        if (!l || ![l isKindOfClass:[NSTextField class]]) return moo_string_new("");
        NSString *s = [(NSTextField *)l stringValue];
        return moo_string_new([s UTF8String]);
    }
}

/* Entspricht moo_ui_gtk.c:310–324 */
MooValue moo_ui_knopf(MooValue parent, MooValue text,
                      MooValue x, MooValue y, MooValue b, MooValue h,
                      MooValue callback) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);
        NSButton *btn = [[NSButton alloc] initWithFrame:NSZeroRect];
        [btn setTitle:nsstr_or(text, @"OK")];
        [btn setButtonType:NSButtonTypeMomentaryPushIn];
        [btn setBezelStyle:NSBezelStyleRounded];
        place_child(fx, btn, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));

        if (callback.tag == MOO_FUNC) {
            MooCallbackTarget *tgt = [[MooCallbackTarget alloc]
                                       initWithCallback:callback];
            [btn setTarget:tgt];
            [btn setAction:@selector(fire:)];
            /* Target wird von NSButton nur schwach gehalten — wir retainen es
             * via associated object, damit es lebt solange der Button lebt. */
            objc_setAssociatedObject(btn, kMooCBTargetKey, tgt,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        return wrap_objc(btn);
    }
}

/* ------------------------------------------------------------------ *
 * Checkbox, Radio, Eingabe, Textbereich
 * (Entspricht moo_ui_gtk.c:326–509)
 * ------------------------------------------------------------------ */

/* Entspricht moo_ui_gtk.c:336–352 */
MooValue moo_ui_checkbox(MooValue parent, MooValue text,
                         MooValue x, MooValue y, MooValue b, MooValue h,
                         MooValue initial, MooValue callback) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);
        NSButton *cb = [[NSButton alloc] initWithFrame:NSZeroRect];
        [cb setButtonType:NSButtonTypeSwitch];
        [cb setTitle:nsstr_or(text, @"")];
        [cb setState:(bool_or(initial, NO) ? NSControlStateValueOn
                                           : NSControlStateValueOff)];
        place_child(fx, cb, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));

        if (callback.tag == MOO_FUNC) {
            MooCallbackTarget *tgt = [[MooCallbackTarget alloc]
                                       initWithCallback:callback];
            [cb setTarget:tgt];
            [cb setAction:@selector(fire:)];
            objc_setAssociatedObject(cb, kMooCBTargetKey, tgt,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        return wrap_objc(cb);
    }
}

/* Entspricht moo_ui_gtk.c:354–358 */
MooValue moo_ui_checkbox_wert(MooValue checkbox) {
    @autoreleasepool {
        id c = unwrap_objc(checkbox);
        if (!c || ![c isKindOfClass:[NSButton class]]) return moo_bool(0);
        return moo_bool([(NSButton *)c state] == NSControlStateValueOn);
    }
}

/* Entspricht moo_ui_gtk.c:360–365 */
MooValue moo_ui_checkbox_setze(MooValue checkbox, MooValue wert) {
    @autoreleasepool {
        id c = unwrap_objc(checkbox);
        if (!c || ![c isKindOfClass:[NSButton class]]) return moo_bool(0);
        [(NSButton *)c setState:(bool_or(wert, NO) ? NSControlStateValueOn
                                                   : NSControlStateValueOff)];
        return moo_bool(1);
    }
}

/* Radio-Gruppen: Cocoa-Radio-Buttons gruppieren sich automatisch wenn sie
 * dieselbe action haben UND denselben Superview — aber da wir pro Gruppe
 * unterschiedliche Callbacks wollen, emulieren wir die Gruppierung manuell.
 * Der erste Radio einer Gruppe wird auf Window-Ebene unter
 * "moo-radio-grp:<name>" gespeichert; Folge-Radios verbinden sich mit
 * demselben Action-Handler, der vor dem Callback alle Gruppenmitglieder
 * auf OFF schaltet ausser sich selbst.
 * (Entspricht moo_ui_gtk.c:370–399) */

@interface MooRadioGroup : NSObject
@property (nonatomic, strong) NSMutableArray<NSButton *> *buttons;
@end
@implementation MooRadioGroup
- (instancetype)init {
    if ((self = [super init])) _buttons = [NSMutableArray array];
    return self;
}
- (void)select:(NSButton *)btn {
    for (NSButton *b in self.buttons) {
        [b setState:(b == btn ? NSControlStateValueOn : NSControlStateValueOff)];
    }
}
@end

@interface MooRadioTarget : MooCallbackTarget
@property (nonatomic, weak) MooRadioGroup *group;
@end
@implementation MooRadioTarget
- (void)fire:(id)sender {
    if ([sender isKindOfClass:[NSButton class]]) {
        [self.group select:(NSButton *)sender];
    }
    [super fire:sender];
}
@end

/* Entspricht moo_ui_gtk.c:370–399 */
MooValue moo_ui_radio(MooValue parent, MooValue gruppe, MooValue text,
                      MooValue x, MooValue y, MooValue b, MooValue h,
                      MooValue callback) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);
        NSWindow *top = [fx window];
        if (!top) return moo_bool(0);

        const char *grp = str_or(gruppe, "default");
        NSString *key = [NSString stringWithFormat:@"moo-radio-grp:%s", grp];

        /* Group-Register per Fenster via associated dictionary. */
        NSMutableDictionary *groups = objc_getAssociatedObject(top, kMooRadioGroupKey);
        if (!groups) {
            groups = [NSMutableDictionary dictionary];
            objc_setAssociatedObject(top, kMooRadioGroupKey, groups,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        MooRadioGroup *g = groups[key];
        if (!g) {
            g = [[MooRadioGroup alloc] init];
            groups[key] = g;
        }

        NSButton *rb = [[NSButton alloc] initWithFrame:NSZeroRect];
        [rb setButtonType:NSButtonTypeRadio];
        [rb setTitle:nsstr_or(text, @"")];
        [g.buttons addObject:rb];

        place_child(fx, rb, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));

        MooRadioTarget *tgt = [[MooRadioTarget alloc] initWithCallback:callback];
        tgt.group = g;
        [rb setTarget:tgt];
        [rb setAction:@selector(fire:)];
        objc_setAssociatedObject(rb, kMooCBTargetKey, tgt,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return wrap_objc(rb);
    }
}

/* Entspricht moo_ui_gtk.c:401–405 */
MooValue moo_ui_radio_wert(MooValue radio) {
    @autoreleasepool {
        id r = unwrap_objc(radio);
        if (!r || ![r isKindOfClass:[NSButton class]]) return moo_bool(0);
        return moo_bool([(NSButton *)r state] == NSControlStateValueOn);
    }
}

/* Eingabe (NSTextField). `passwort` → NSSecureTextField subklasse. */
/* Entspricht moo_ui_gtk.c:413–426 */
MooValue moo_ui_eingabe(MooValue parent,
                        MooValue x, MooValue y, MooValue b, MooValue h,
                        MooValue platzhalter, MooValue passwort) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);
        NSTextField *en = bool_or(passwort, NO)
            ? [[NSSecureTextField alloc] initWithFrame:NSZeroRect]
            : [[NSTextField alloc]       initWithFrame:NSZeroRect];
        [en setEditable:YES];
        [en setBezeled:YES];
        [en setDrawsBackground:YES];
        if (platzhalter.tag == MOO_STRING) {
            [[en cell] setPlaceholderString:[NSString stringWithUTF8String:MV_STR(platzhalter)->chars]];
        }
        place_child(fx, en, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
        return wrap_objc(en);
    }
}

/* Entspricht moo_ui_gtk.c:428–432 */
MooValue moo_ui_eingabe_text(MooValue eingabe) {
    @autoreleasepool {
        id e = unwrap_objc(eingabe);
        if (!e || ![e isKindOfClass:[NSTextField class]]) return moo_string_new("");
        return moo_string_new([[(NSTextField *)e stringValue] UTF8String]);
    }
}

/* Entspricht moo_ui_gtk.c:434–439 */
MooValue moo_ui_eingabe_setze(MooValue eingabe, MooValue text) {
    @autoreleasepool {
        id e = unwrap_objc(eingabe);
        if (!e || ![e isKindOfClass:[NSTextField class]]) return moo_bool(0);
        [(NSTextField *)e setStringValue:nsstr_or(text, @"")];
        return moo_bool(1);
    }
}

/* NSControlTextDidChangeNotification → Callback-Bruecke */
@interface MooEntryChangeTarget : MooCallbackTarget
@end
@implementation MooEntryChangeTarget
- (void)controlTextDidChange:(NSNotification *)note {
    (void)note;
    [self fire:nil];
}
@end

/* Entspricht moo_ui_gtk.c:441–449 */
MooValue moo_ui_eingabe_on_change(MooValue eingabe, MooValue callback) {
    @autoreleasepool {
        id e = unwrap_objc(eingabe);
        if (!e || ![e isKindOfClass:[NSTextField class]]) return moo_bool(0);
        MooEntryChangeTarget *tgt = [[MooEntryChangeTarget alloc]
                                       initWithCallback:callback];
        [(NSTextField *)e setDelegate:(id<NSTextFieldDelegate>)tgt];
        objc_setAssociatedObject(e, kMooCBTargetChangeKey, tgt,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return moo_bool(1);
    }
}

/* Textbereich: NSScrollView + NSTextView (analog GTK GtkScrolledWindow + GtkTextView).
 * Handle = NSScrollView. Associated "moo-tv" = NSTextView.
 * (Entspricht moo_ui_gtk.c:455–476) */

static NSTextView *tb_get_view(MooValue tb) {
    id sw = unwrap_objc(tb);
    if (!sw) return nil;
    id tv = objc_getAssociatedObject(sw, kMooTVKey);
    return ([tv isKindOfClass:[NSTextView class]]) ? (NSTextView *)tv : nil;
}

/* Entspricht moo_ui_gtk.c:455–469 */
MooValue moo_ui_textbereich(MooValue parent,
                            MooValue x, MooValue y, MooValue b, MooValue h) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);
        NSScrollView *sw = [[NSScrollView alloc] initWithFrame:NSZeroRect];
        [sw setHasVerticalScroller:YES];
        [sw setHasHorizontalScroller:YES];
        [sw setAutohidesScrollers:YES];
        [sw setBorderType:NSBezelBorder];

        NSTextView *tv = [[NSTextView alloc] initWithFrame:[sw.contentView bounds]];
        [tv setEditable:YES];
        [tv setRichText:NO];
        [tv setMinSize:NSMakeSize(0, 0)];
        [tv setMaxSize:NSMakeSize(FLT_MAX, FLT_MAX)];
        [tv setVerticallyResizable:YES];
        [tv setHorizontallyResizable:YES];
        [tv setAutoresizingMask:NSViewWidthSizable];
        [[tv textContainer] setContainerSize:NSMakeSize(FLT_MAX, FLT_MAX)];
        [[tv textContainer] setWidthTracksTextView:YES];
        [sw setDocumentView:tv];

        objc_setAssociatedObject(sw, kMooTVKey, tv, OBJC_ASSOCIATION_ASSIGN);
        place_child(fx, sw, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
        return wrap_objc(sw);
    }
}

/* Entspricht moo_ui_gtk.c:478–489 */
MooValue moo_ui_textbereich_text(MooValue tb) {
    @autoreleasepool {
        NSTextView *v = tb_get_view(tb);
        if (!v) return moo_string_new("");
        return moo_string_new([[v string] UTF8String]);
    }
}

/* Entspricht moo_ui_gtk.c:491–498 */
MooValue moo_ui_textbereich_setze(MooValue tb, MooValue text) {
    @autoreleasepool {
        NSTextView *v = tb_get_view(tb);
        if (!v) return moo_bool(0);
        [v setString:nsstr_or(text, @"")];
        return moo_bool(1);
    }
}

/* Entspricht moo_ui_gtk.c:500–509 */
MooValue moo_ui_textbereich_anhaengen(MooValue tb, MooValue text) {
    @autoreleasepool {
        NSTextView *v = tb_get_view(tb);
        if (!v) return moo_bool(0);
        NSString *s = nsstr_or(text, @"");
        NSTextStorage *st = [v textStorage];
        [st beginEditing];
        [st replaceCharactersInRange:NSMakeRange([st length], 0) withString:s];
        [st endEditing];
        return moo_bool(1);
    }
}

/* ------------------------------------------------------------------ *
 * Dropdown, Liste, Slider, Fortschritt, Bild, Leinwand
 * (Entspricht moo_ui_gtk.c:511–824)
 * ------------------------------------------------------------------ */

/* Entspricht moo_ui_gtk.c:521–546 */
MooValue moo_ui_dropdown(MooValue parent, MooValue optionen,
                         MooValue x, MooValue y, MooValue b, MooValue h,
                         MooValue callback) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);
        NSPopUpButton *dd = [[NSPopUpButton alloc] initWithFrame:NSZeroRect
                                                       pullsDown:NO];
        if (optionen.tag == MOO_LIST) {
            int32_t n = moo_list_iter_len(optionen);
            for (int32_t i = 0; i < n; i++) {
                MooValue item = moo_list_iter_get(optionen, i);
                if (item.tag == MOO_STRING) {
                    [dd addItemWithTitle:[NSString stringWithUTF8String:MV_STR(item)->chars]];
                }
            }
        }
        place_child(fx, dd, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
        if (callback.tag == MOO_FUNC) {
            MooCallbackTarget *tgt = [[MooCallbackTarget alloc]
                                       initWithCallback:callback];
            [dd setTarget:tgt];
            [dd setAction:@selector(fire:)];
            objc_setAssociatedObject(dd, kMooCBTargetKey, tgt,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        return wrap_objc(dd);
    }
}

/* Entspricht moo_ui_gtk.c:548–552 */
MooValue moo_ui_dropdown_auswahl(MooValue dd) {
    @autoreleasepool {
        id c = unwrap_objc(dd);
        if (!c || ![c isKindOfClass:[NSPopUpButton class]]) return moo_number(-1);
        return moo_number((double)[(NSPopUpButton *)c indexOfSelectedItem]);
    }
}

/* Entspricht moo_ui_gtk.c:554–559 */
MooValue moo_ui_dropdown_auswahl_setze(MooValue dd, MooValue index) {
    @autoreleasepool {
        id c = unwrap_objc(dd);
        if (!c || ![c isKindOfClass:[NSPopUpButton class]]) return moo_bool(0);
        NSInteger i = num_or(index, -1);
        NSPopUpButton *p = (NSPopUpButton *)c;
        if (i >= 0 && i < [p numberOfItems]) {
            [p selectItemAtIndex:i];
        }
        return moo_bool(1);
    }
}

/* Entspricht moo_ui_gtk.c:561–568 */
MooValue moo_ui_dropdown_text(MooValue dd) {
    @autoreleasepool {
        id c = unwrap_objc(dd);
        if (!c || ![c isKindOfClass:[NSPopUpButton class]]) return moo_string_new("");
        NSString *t = [(NSPopUpButton *)c titleOfSelectedItem];
        return moo_string_new(t ? [t UTF8String] : "");
    }
}

/* Liste: NSScrollView umhuellt NSTableView + MooTableDataSource.
 * Handle = NSScrollView. Associated: moo-tv=NSTableView, moo-ds=DataSource.
 * (Entspricht moo_ui_gtk.c:573–615) */
MooValue moo_ui_liste(MooValue parent, MooValue spalten,
                      MooValue x, MooValue y, MooValue b, MooValue h) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);

        int32_t ncols = 1;
        if (spalten.tag == MOO_LIST) ncols = moo_list_iter_len(spalten);
        if (ncols < 1) ncols = 1;

        NSTableView *tv = [[NSTableView alloc] initWithFrame:NSZeroRect];
        [tv setAllowsColumnReordering:NO];
        [tv setAllowsColumnResizing:YES];
        [tv setUsesAlternatingRowBackgroundColors:YES];

        for (int32_t i = 0; i < ncols; i++) {
            NSString *title = @"";
            if (spalten.tag == MOO_LIST) {
                MooValue cv = moo_list_iter_get(spalten, i);
                if (cv.tag == MOO_STRING) {
                    title = [NSString stringWithUTF8String:MV_STR(cv)->chars];
                }
            }
            NSTableColumn *col = [[NSTableColumn alloc]
                                  initWithIdentifier:[NSString stringWithFormat:@"c%d", i]];
            [[col headerCell] setStringValue:title];
            [col setWidth:100];
            [col setResizingMask:NSTableColumnUserResizingMask];
            [tv addTableColumn:col];
        }

        MooTableDataSource *ds = [[MooTableDataSource alloc] init];
        ds.ncols = ncols;
        [tv setDataSource:ds];
        [tv setDelegate:ds];

        NSScrollView *sw = [[NSScrollView alloc] initWithFrame:NSZeroRect];
        [sw setHasVerticalScroller:YES];
        [sw setHasHorizontalScroller:YES];
        [sw setAutohidesScrollers:YES];
        [sw setBorderType:NSBezelBorder];
        [sw setDocumentView:tv];

        objc_setAssociatedObject(sw, kMooTVKey, tv, OBJC_ASSOCIATION_ASSIGN);
        objc_setAssociatedObject(sw, kMooTableDSKey, ds,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(sw, kMooNColsKey, @(ncols),
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        place_child(fx, sw, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
        return wrap_objc(sw);
    }
}

static MooTableDataSource *liste_ds(MooValue liste) {
    id sw = unwrap_objc(liste);
    if (!sw) return nil;
    id ds = objc_getAssociatedObject(sw, kMooTableDSKey);
    return ([ds isKindOfClass:[MooTableDataSource class]]) ? (MooTableDataSource *)ds : nil;
}

static NSTableView *liste_tv(MooValue liste) {
    id sw = unwrap_objc(liste);
    if (!sw) return nil;
    id tv = objc_getAssociatedObject(sw, kMooTVKey);
    return ([tv isKindOfClass:[NSTableView class]]) ? (NSTableView *)tv : nil;
}

/* Entspricht moo_ui_gtk.c:628–644 */
MooValue moo_ui_liste_zeile_hinzu(MooValue liste, MooValue zeile) {
    @autoreleasepool {
        MooTableDataSource *ds = liste_ds(liste);
        NSTableView *tv = liste_tv(liste);
        if (!ds || !tv) return moo_bool(0);
        int32_t n = (zeile.tag == MOO_LIST) ? moo_list_iter_len(zeile) : 0;
        NSMutableArray<NSString *> *row = [NSMutableArray arrayWithCapacity:ds.ncols];
        for (NSInteger i = 0; i < ds.ncols; i++) {
            NSString *s = @"";
            if (i < n) {
                MooValue c = moo_list_iter_get(zeile, (int32_t)i);
                if (c.tag == MOO_STRING) s = [NSString stringWithUTF8String:MV_STR(c)->chars];
            }
            [row addObject:s];
        }
        [ds.rows addObject:row];
        [tv reloadData];
        return moo_bool(1);
    }
}

/* Entspricht moo_ui_gtk.c:646–660 */
MooValue moo_ui_liste_auswahl(MooValue liste) {
    @autoreleasepool {
        NSTableView *tv = liste_tv(liste);
        if (!tv) return moo_number(-1);
        NSInteger row = [tv selectedRow];
        return moo_number((double)row);
    }
}

/* Entspricht moo_ui_gtk.c:662–681 */
MooValue moo_ui_liste_zeile(MooValue liste, MooValue index) {
    @autoreleasepool {
        MooTableDataSource *ds = liste_ds(liste);
        if (!ds) return moo_list_new(0);
        int idx = num_or(index, -1);
        if (idx < 0 || idx >= (int)ds.rows.count) return moo_list_new(0);
        NSArray<NSString *> *r = ds.rows[idx];
        MooValue out = moo_list_new((int32_t)ds.ncols);
        for (NSInteger i = 0; i < ds.ncols; i++) {
            NSString *s = (i < (NSInteger)r.count) ? r[i] : @"";
            moo_list_append(out, moo_string_new([s UTF8String]));
        }
        return out;
    }
}

/* Entspricht moo_ui_gtk.c:683–689 */
MooValue moo_ui_liste_leeren(MooValue liste) {
    @autoreleasepool {
        MooTableDataSource *ds = liste_ds(liste);
        NSTableView *tv = liste_tv(liste);
        if (!ds || !tv) return moo_bool(0);
        [ds.rows removeAllObjects];
        [tv reloadData];
        return moo_bool(1);
    }
}

/* Entspricht moo_ui_gtk.c:697–708 */
MooValue moo_ui_liste_on_auswahl(MooValue liste, MooValue callback) {
    @autoreleasepool {
        MooTableDataSource *ds = liste_ds(liste);
        if (!ds) return moo_bool(0);
        MooCallbackTarget *tgt = [[MooCallbackTarget alloc] initWithCallback:callback];
        ds.selectTarget = tgt;
        return moo_bool(1);
    }
}

/* Slider — NSSlider. (Entspricht moo_ui_gtk.c:716–749) */
MooValue moo_ui_slider(MooValue parent, MooValue min, MooValue max, MooValue start,
                       MooValue x, MooValue y, MooValue b, MooValue h,
                       MooValue callback) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);
        double mn = (min.tag   == MOO_NUMBER) ? MV_NUM(min)   : 0.0;
        double mx = (max.tag   == MOO_NUMBER) ? MV_NUM(max)   : 100.0;
        double st = (start.tag == MOO_NUMBER) ? MV_NUM(start) : mn;
        NSSlider *sl = [[NSSlider alloc] initWithFrame:NSZeroRect];
        [sl setMinValue:mn];
        [sl setMaxValue:mx];
        [sl setDoubleValue:st];
        /* setSliderType:NSSliderTypeLinear ist Default; horizontal per frame. */
        place_child(fx, sl, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
        if (callback.tag == MOO_FUNC) {
            MooCallbackTarget *tgt = [[MooCallbackTarget alloc]
                                       initWithCallback:callback];
            [sl setTarget:tgt];
            [sl setAction:@selector(fire:)];
            [sl setContinuous:YES];
            objc_setAssociatedObject(sl, kMooCBTargetKey, tgt,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        return wrap_objc(sl);
    }
}

/* Entspricht moo_ui_gtk.c:737–741 */
MooValue moo_ui_slider_wert(MooValue slider) {
    @autoreleasepool {
        id s = unwrap_objc(slider);
        if (!s || ![s isKindOfClass:[NSSlider class]]) return moo_number(0.0);
        return moo_number([(NSSlider *)s doubleValue]);
    }
}

/* Entspricht moo_ui_gtk.c:743–749 */
MooValue moo_ui_slider_setze(MooValue slider, MooValue wert) {
    @autoreleasepool {
        id s = unwrap_objc(slider);
        if (!s || ![s isKindOfClass:[NSSlider class]]) return moo_bool(0);
        [(NSSlider *)s setDoubleValue:(wert.tag == MOO_NUMBER) ? MV_NUM(wert) : 0.0];
        return moo_bool(1);
    }
}

/* Fortschritt — NSProgressIndicator. (Entspricht moo_ui_gtk.c:751–759) */
MooValue moo_ui_fortschritt(MooValue parent,
                            MooValue x, MooValue y, MooValue b, MooValue h) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);
        NSProgressIndicator *pb = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
        [pb setStyle:NSProgressIndicatorStyleBar];
        [pb setIndeterminate:NO];
        [pb setMinValue:0.0];
        [pb setMaxValue:1.0];
        [pb setDoubleValue:0.0];
        place_child(fx, pb, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
        return wrap_objc(pb);
    }
}

/* Entspricht moo_ui_gtk.c:761–769 */
MooValue moo_ui_fortschritt_setze(MooValue bar, MooValue wert) {
    @autoreleasepool {
        id p = unwrap_objc(bar);
        if (!p || ![p isKindOfClass:[NSProgressIndicator class]]) return moo_bool(0);
        double v = (wert.tag == MOO_NUMBER) ? MV_NUM(wert) : 0.0;
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;
        [(NSProgressIndicator *)p setDoubleValue:v];
        return moo_bool(1);
    }
}

/* Bild — NSImageView. (Entspricht moo_ui_gtk.c:771–787) */
MooValue moo_ui_bild(MooValue parent, MooValue pfad,
                     MooValue x, MooValue y, MooValue b, MooValue h) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);
        const char *p = str_or(pfad, "");
        NSImageView *iv = [[NSImageView alloc] initWithFrame:NSZeroRect];
        if (*p) {
            NSImage *img = [[NSImage alloc] initWithContentsOfFile:
                              [NSString stringWithUTF8String:p]];
            if (img) [iv setImage:img];
        }
        [iv setImageScaling:NSImageScaleProportionallyUpOrDown];
        place_child(fx, iv, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
        return wrap_objc(iv);
    }
}

/* Entspricht moo_ui_gtk.c:782–787 */
MooValue moo_ui_bild_setze(MooValue bild, MooValue pfad) {
    @autoreleasepool {
        id i = unwrap_objc(bild);
        if (!i || ![i isKindOfClass:[NSImageView class]]) return moo_bool(0);
        NSString *s = nsstr_or(pfad, @"");
        NSImage *img = [[NSImage alloc] initWithContentsOfFile:s];
        [(NSImageView *)i setImage:img];
        return moo_bool(img != nil);
    }
}

/* ------------------------------------------------------------------ *
 * Leinwand / Zeichner (Phase 5)
 *
 * Der zeichner wrapped CGContextRef + aktuelle Farbe + valid-Flag.
 * Wird stack-allokiert in drawRect:, an den moo-Callback als 2. Arg
 * uebergeben (tag=MOO_NUMBER, data=&zeichner), danach `valid=0`.
 * ------------------------------------------------------------------ */

typedef struct {
    CGContextRef ctx;
    CGFloat r, g, b, a;    /* 0..1 */
    int    valid;
    CGFloat view_w, view_h;
} MooZeichnerCG;

static inline MooZeichnerCG *unwrap_zeichner_cg(MooValue v) {
    if (v.tag != MOO_NUMBER) return NULL;
    MooZeichnerCG *z = (MooZeichnerCG *)moo_val_as_ptr(v);
    if (!z || !z->valid || !z->ctx) return NULL;
    return z;
}

/* Leinwand: NSView-Subklasse mit drawRect → moo-callback (lw, z).
 *
 * Welle 2 (Plan 003 P5, Phase 1): Maus-Klicks + Bewegung via
 * mouseDown/rightMouseDown/otherMouseDown und mouseMoved. Ein
 * NSTrackingArea wird lazy angelegt wenn on_bewegung registriert wird
 * und bei jedem updateTrackingAreas neu geformt (Groessen-Changes).
 * acceptsFirstResponder liefert YES, damit NSView mouseMoved-Events
 * ueberhaupt bekommt (Standard ist NO fuer non-focused views).
 *
 * Taste-Mapping Cocoa → moo:
 *   mouseDown:       → taste=1 (links)
 *   rightMouseDown:  → taste=3 (rechts)
 *   otherMouseDown:  → taste=2 (mitte — NSEvent.buttonNumber==2)
 *
 * Koordinaten: flipped=YES (von MooFlippedView geerbt) → top-left-origin
 * stimmt mit GTK/Windows ueberein. [event locationInWindow] →
 * [self convertPoint:fromView:nil] liefert view-lokal in flipped-Koord.
 */
@interface MooCanvasView : MooFlippedView
@property (nonatomic, strong) MooCallbackTarget *drawTarget;
@property (nonatomic, strong, nullable) MooCallbackTarget *mouseTarget;
@property (nonatomic, strong, nullable) MooCallbackTarget *motionTarget;
@property (nonatomic, strong, nullable) NSTrackingArea *trackingArea;
@end
@implementation MooCanvasView
- (BOOL)acceptsFirstResponder { return YES; }

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];
    if (self.drawTarget) {
        CGContextRef cg = [[NSGraphicsContext currentContext] CGContext];
        MooZeichnerCG z;
        z.ctx = cg;
        z.r = 0; z.g = 0; z.b = 0; z.a = 1.0;
        z.valid = 1;
        z.view_w = self.bounds.size.width;
        z.view_h = self.bounds.size.height;
        [self.drawTarget fireWithHandle:self zeichner:&z];
        z.valid = 0;
        z.ctx = NULL;
    }
}

/* Ruft callback(view, x, y, taste) via moo_func_call — 4 Argumente, daher
 * direkt ueber moo_func_call_n statt ueber MooCallbackTarget.
 * (MooCallbackTarget hat nur 0/1/2-Arg-Fire-Methoden.) */
- (void)fireMouse:(NSEvent *)event taste:(int)taste {
    if (!self.mouseTarget) return;
    MooValue cb = self.mouseTarget->cb;
    if (cb.tag != MOO_FUNC) return;
    NSPoint lp = [self convertPoint:[event locationInWindow] fromView:nil];
    MooValue vSelf;   vSelf.tag = MOO_NUMBER; moo_val_set_ptr(&vSelf, (__bridge void *)self);
    MooValue vx = moo_number((double)(int)lp.x);
    MooValue vy = moo_number((double)(int)lp.y);
    MooValue vt = moo_number((double)taste);
    MooValue rv = moo_func_call_4(cb, vSelf, vx, vy, vt);
    moo_release(rv);
}

- (void)mouseDown:(NSEvent *)event      { [self fireMouse:event taste:1]; [super mouseDown:event]; }
- (void)rightMouseDown:(NSEvent *)event { [self fireMouse:event taste:3]; [super rightMouseDown:event]; }
- (void)otherMouseDown:(NSEvent *)event {
    int taste = 2;  /* Mitte ist typischerweise buttonNumber==2 */
    if (event.buttonNumber != 2) taste = (int)event.buttonNumber + 1;
    [self fireMouse:event taste:taste];
    [super otherMouseDown:event];
}

- (void)mouseMoved:(NSEvent *)event {
    if (!self.motionTarget) { [super mouseMoved:event]; return; }
    MooValue cb = self.motionTarget->cb;
    if (cb.tag != MOO_FUNC) { [super mouseMoved:event]; return; }
    NSPoint lp = [self convertPoint:[event locationInWindow] fromView:nil];
    MooValue vSelf; vSelf.tag = MOO_NUMBER; moo_val_set_ptr(&vSelf, (__bridge void *)self);
    MooValue vx = moo_number((double)(int)lp.x);
    MooValue vy = moo_number((double)(int)lp.y);
    MooValue rv = moo_func_call_3(cb, vSelf, vx, vy);
    moo_release(rv);
}

/* Dragged-Events ebenfalls als Bewegung zaehlen (Konsistenz mit GTK). */
- (void)mouseDragged:(NSEvent *)event      { [self mouseMoved:event]; }
- (void)rightMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)otherMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (self.trackingArea) {
        [self removeTrackingArea:self.trackingArea];
        self.trackingArea = nil;
    }
    if (!self.motionTarget) return;
    NSTrackingAreaOptions opts = NSTrackingMouseMoved
                               | NSTrackingActiveInKeyWindow
                               | NSTrackingInVisibleRect;
    self.trackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds
                                                     options:opts
                                                       owner:self
                                                    userInfo:nil];
    [self addTrackingArea:self.trackingArea];
}
@end

/* ------------------------------------------------------------------ *
 * Zeichner-Primitive (CoreGraphics) — nur im drawRect-Kontext gueltig.
 * ------------------------------------------------------------------ */

static inline void cg_apply_color(MooZeichnerCG *z) {
    CGContextSetRGBFillColor(z->ctx, z->r, z->g, z->b, z->a);
    CGContextSetRGBStrokeColor(z->ctx, z->r, z->g, z->b, z->a);
}

MooValue moo_ui_zeichne_farbe(MooValue zeichner,
                              MooValue r, MooValue g, MooValue b, MooValue a) {
    MooZeichnerCG *z = unwrap_zeichner_cg(zeichner);
    if (!z) return moo_bool(0);
    int ri = num_or(r, 0), gi = num_or(g, 0), bi = num_or(b, 0), ai = num_or(a, 255);
    if (ri < 0) ri = 0; if (ri > 255) ri = 255;
    if (gi < 0) gi = 0; if (gi > 255) gi = 255;
    if (bi < 0) bi = 0; if (bi > 255) bi = 255;
    if (ai < 0) ai = 0; if (ai > 255) ai = 255;
    z->r = ri / 255.0; z->g = gi / 255.0; z->b = bi / 255.0; z->a = ai / 255.0;
    return moo_bool(1);
}

MooValue moo_ui_zeichne_linie(MooValue zeichner,
                              MooValue x1, MooValue y1,
                              MooValue x2, MooValue y2,
                              MooValue breite) {
    MooZeichnerCG *z = unwrap_zeichner_cg(zeichner);
    if (!z) return moo_bool(0);
    CGFloat bw = (breite.tag == MOO_NUMBER) ? MV_NUM(breite) : 1.0;
    if (bw < 0.1) bw = 0.1;
    cg_apply_color(z);
    CGContextSetLineWidth(z->ctx, bw);
    CGContextBeginPath(z->ctx);
    CGContextMoveToPoint(z->ctx, num_or(x1, 0) + 0.5, num_or(y1, 0) + 0.5);
    CGContextAddLineToPoint(z->ctx, num_or(x2, 0) + 0.5, num_or(y2, 0) + 0.5);
    CGContextStrokePath(z->ctx);
    return moo_bool(1);
}

MooValue moo_ui_zeichne_rechteck(MooValue zeichner,
                                 MooValue x, MooValue y,
                                 MooValue b, MooValue h,
                                 MooValue gefuellt) {
    MooZeichnerCG *z = unwrap_zeichner_cg(zeichner);
    if (!z) return moo_bool(0);
    CGRect r = CGRectMake(num_or(x, 0), num_or(y, 0),
                          num_or(b, 0), num_or(h, 0));
    cg_apply_color(z);
    if (bool_or(gefuellt, YES)) {
        CGContextFillRect(z->ctx, r);
    } else {
        CGContextSetLineWidth(z->ctx, 1.0);
        CGContextStrokeRect(z->ctx, r);
    }
    return moo_bool(1);
}

MooValue moo_ui_zeichne_kreis(MooValue zeichner,
                              MooValue cx, MooValue cy,
                              MooValue radius, MooValue gefuellt) {
    MooZeichnerCG *z = unwrap_zeichner_cg(zeichner);
    if (!z) return moo_bool(0);
    CGFloat rad = (radius.tag == MOO_NUMBER) ? MV_NUM(radius) : 1.0;
    if (rad < 0) rad = 0;
    CGRect r = CGRectMake(num_or(cx, 0) - rad, num_or(cy, 0) - rad,
                          rad * 2.0, rad * 2.0);
    cg_apply_color(z);
    if (bool_or(gefuellt, YES)) {
        CGContextFillEllipseInRect(z->ctx, r);
    } else {
        CGContextSetLineWidth(z->ctx, 1.0);
        CGContextStrokeEllipseInRect(z->ctx, r);
    }
    return moo_bool(1);
}

MooValue moo_ui_zeichne_text(MooValue zeichner,
                             MooValue x, MooValue y,
                             MooValue text, MooValue schriftgroesse) {
    @autoreleasepool {
        MooZeichnerCG *z = unwrap_zeichner_cg(zeichner);
        if (!z) return moo_bool(0);
        NSString *s = nsstr_or(text, @"");
        CGFloat sz = (schriftgroesse.tag == MOO_NUMBER) ? MV_NUM(schriftgroesse) : 12.0;
        if (sz < 1.0) sz = 1.0;
        NSColor *col = [NSColor colorWithCalibratedRed:z->r green:z->g blue:z->b alpha:z->a];
        NSDictionary *attrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:sz],
            NSForegroundColorAttributeName: col,
        };
        /* Da der umgebende View flipped ist (isFlipped=YES), funktioniert
         * top-left-Origin fuer NSString drawAtPoint direkt. */
        [s drawAtPoint:NSMakePoint(num_or(x, 0), num_or(y, 0)) withAttributes:attrs];
        return moo_bool(1);
    }
}

MooValue moo_ui_zeichne_bild(MooValue zeichner,
                             MooValue x, MooValue y,
                             MooValue b, MooValue h,
                             MooValue pfad) {
    @autoreleasepool {
        MooZeichnerCG *z = unwrap_zeichner_cg(zeichner);
        if (!z) return moo_bool(0);
        NSString *p = nsstr_or(pfad, @"");
        if (p.length == 0) return moo_bool(0);
        NSImage *img = [[NSImage alloc] initWithContentsOfFile:p];
        if (!img) return moo_bool(0);
        NSSize is = img.size;
        int tw = num_or(b, (int)is.width);
        int th = num_or(h, (int)is.height);
        NSRect dst = NSMakeRect(num_or(x, 0), num_or(y, 0), tw, th);
        [img drawInRect:dst
               fromRect:NSZeroRect
              operation:NSCompositingOperationSourceOver
               fraction:1.0
         respectFlipped:YES
                  hints:nil];
        return moo_bool(1);
    }
}

/* Entspricht moo_ui_gtk.c:802–817 */
MooValue moo_ui_leinwand(MooValue parent,
                         MooValue x, MooValue y, MooValue b, MooValue h,
                         MooValue on_zeichne) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);
        MooCanvasView *da = [[MooCanvasView alloc] initWithFrame:NSZeroRect];
        place_child(fx, da, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
        if (on_zeichne.tag == MOO_FUNC) {
            MooCallbackTarget *tgt = [[MooCallbackTarget alloc]
                                       initWithCallback:on_zeichne];
            da.drawTarget = tgt;
            objc_setAssociatedObject(da, kMooCBTargetDrawKey, tgt,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        return wrap_objc(da);
    }
}

/* Entspricht moo_ui_gtk.c:819–824 */
MooValue moo_ui_leinwand_anfordern(MooValue leinwand) {
    @autoreleasepool {
        id w = unwrap_objc(leinwand);
        if (!w || ![w isKindOfClass:[NSView class]]) return moo_bool(0);
        [(NSView *)w setNeedsDisplay:YES];
        return moo_bool(1);
    }
}

/* ------------------------------------------------------------------ *
 * Rahmen, Trenner, Tabs, Scroll
 * (Entspricht moo_ui_gtk.c:830–908)
 * ------------------------------------------------------------------ */

/* Rahmen = NSBox mit Titel. contentView = MooFlippedView. */
/* Entspricht moo_ui_gtk.c:830–841 */
MooValue moo_ui_rahmen(MooValue parent, MooValue titel,
                       MooValue x, MooValue y, MooValue b, MooValue h) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);
        NSBox *box = [[NSBox alloc] initWithFrame:NSZeroRect];
        [box setTitlePosition:(titel.tag == MOO_STRING) ? NSAtTop : NSNoTitle];
        if (titel.tag == MOO_STRING) {
            [box setTitle:[NSString stringWithUTF8String:MV_STR(titel)->chars]];
        }
        [box setBoxType:NSBoxPrimary];
        MooFlippedView *inner = [[MooFlippedView alloc] initWithFrame:NSZeroRect];
        [box setContentView:inner];
        objc_setAssociatedObject(box, kMooFixedKey, inner, OBJC_ASSOCIATION_ASSIGN);
        place_child(fx, box, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
        return wrap_objc(box);
    }
}

/* Trenner = NSBox im Separator-Stil. */
/* Entspricht moo_ui_gtk.c:843–855 */
MooValue moo_ui_trenner(MooValue parent,
                        MooValue x, MooValue y, MooValue b, MooValue h) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);
        int bb = num_or(b, 1), hh = num_or(h, 1);
        NSBox *s = [[NSBox alloc] initWithFrame:NSZeroRect];
        [s setBoxType:NSBoxSeparator];
        place_child(fx, s, num_or(x, 0), num_or(y, 0), bb, hh);
        return wrap_objc(s);
    }
}

/* Tabs = NSTabView. (Entspricht moo_ui_gtk.c:857–865) */
MooValue moo_ui_tabs(MooValue parent,
                     MooValue x, MooValue y, MooValue b, MooValue h) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);
        NSTabView *nb = [[NSTabView alloc] initWithFrame:NSZeroRect];
        place_child(fx, nb, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
        return wrap_objc(nb);
    }
}

/* Tab hinzu: liefert den page-View, damit er als parent fuer Kind-Widgets
 * dienen kann. (Entspricht moo_ui_gtk.c:869–880) */
MooValue moo_ui_tab_hinzu(MooValue tabs, MooValue titel) {
    @autoreleasepool {
        id nb = unwrap_objc(tabs);
        if (!nb || ![nb isKindOfClass:[NSTabView class]]) return moo_bool(0);
        NSTabViewItem *ti = [[NSTabViewItem alloc]
                             initWithIdentifier:[NSString stringWithFormat:@"t%lu",
                                                 (unsigned long)[(NSTabView *)nb numberOfTabViewItems]]];
        [ti setLabel:nsstr_or(titel, @"")];
        MooFlippedView *page = [[MooFlippedView alloc] initWithFrame:NSZeroRect];
        [ti setView:page];
        [(NSTabView *)nb addTabViewItem:ti];
        /* page is retained by the NSTabViewItem. */
        objc_setAssociatedObject(page, kMooFixedKey, page, OBJC_ASSOCIATION_ASSIGN);
        return wrap_objc(page);
    }
}

/* Entspricht moo_ui_gtk.c:882–886 */
MooValue moo_ui_tabs_auswahl(MooValue tabs) {
    @autoreleasepool {
        id nb = unwrap_objc(tabs);
        if (!nb || ![nb isKindOfClass:[NSTabView class]]) return moo_number(-1);
        NSTabView *tv = (NSTabView *)nb;
        NSTabViewItem *sel = [tv selectedTabViewItem];
        if (!sel) return moo_number(-1);
        return moo_number((double)[tv indexOfTabViewItem:sel]);
    }
}

/* Entspricht moo_ui_gtk.c:888–893 */
MooValue moo_ui_tabs_auswahl_setze(MooValue tabs, MooValue index) {
    @autoreleasepool {
        id nb = unwrap_objc(tabs);
        if (!nb || ![nb isKindOfClass:[NSTabView class]]) return moo_bool(0);
        NSInteger i = num_or(index, 0);
        NSTabView *tv = (NSTabView *)nb;
        if (i >= 0 && i < [tv numberOfTabViewItems]) {
            [tv selectTabViewItemAtIndex:i];
        }
        return moo_bool(1);
    }
}

/* Scroll — NSScrollView + MooFlippedView als documentView. */
/* Entspricht moo_ui_gtk.c:895–908 */
MooValue moo_ui_scroll(MooValue parent,
                       MooValue x, MooValue y, MooValue b, MooValue h) {
    @autoreleasepool {
        MooFlippedView *fx = resolve_container(parent);
        if (!fx) return moo_bool(0);
        NSScrollView *sw = [[NSScrollView alloc] initWithFrame:NSZeroRect];
        [sw setHasVerticalScroller:YES];
        [sw setHasHorizontalScroller:YES];
        [sw setAutohidesScrollers:YES];
        [sw setBorderType:NSBezelBorder];
        MooFlippedView *inner = [[MooFlippedView alloc]
                                  initWithFrame:NSMakeRect(0, 0, 2000, 2000)];
        [sw setDocumentView:inner];
        objc_setAssociatedObject(sw, kMooFixedKey, inner, OBJC_ASSOCIATION_ASSIGN);
        place_child(fx, sw, num_or(x, 0), num_or(y, 0), num_or(b, -1), num_or(h, -1));
        return wrap_objc(sw);
    }
}

/* ------------------------------------------------------------------ *
 * Allgemeine Widget-Ops + Timer
 * (Entspricht moo_ui_gtk.c:914–1029)
 * ------------------------------------------------------------------ */

/* Entspricht moo_ui_gtk.c:914–920 */
MooValue moo_ui_sichtbar(MooValue widget, MooValue sichtbar) {
    @autoreleasepool {
        id w = unwrap_objc(widget);
        if (!w) return moo_bool(0);
        BOOL show = bool_or(sichtbar, YES);
        if ([w isKindOfClass:[NSWindow class]]) {
            if (show) [(NSWindow *)w makeKeyAndOrderFront:nil];
            else      [(NSWindow *)w orderOut:nil];
        } else if ([w isKindOfClass:[NSView class]]) {
            [(NSView *)w setHidden:!show];
        }
        return moo_bool(1);
    }
}

/* Entspricht moo_ui_gtk.c:922–927 */
MooValue moo_ui_aktiv(MooValue widget, MooValue aktiv) {
    @autoreleasepool {
        id w = unwrap_objc(widget);
        if (!w) return moo_bool(0);
        BOOL enabled = bool_or(aktiv, YES);
        if ([w isKindOfClass:[NSControl class]]) {
            [(NSControl *)w setEnabled:enabled];
        }
        return moo_bool(1);
    }
}

/* Entspricht moo_ui_gtk.c:929–942 */
MooValue moo_ui_position_setze(MooValue widget, MooValue x, MooValue y) {
    @autoreleasepool {
        id w = unwrap_objc(widget);
        if (!w) return moo_bool(0);
        if ([w isKindOfClass:[NSWindow class]]) {
            NSWindow *win = (NSWindow *)w;
            CGFloat sh = [[NSScreen mainScreen] frame].size.height;
            NSRect f = [win frame];
            f.origin.x = (CGFloat)num_or(x, 0);
            f.origin.y = sh - (CGFloat)num_or(y, 0) - f.size.height;
            [win setFrame:f display:YES];
            return moo_bool(1);
        }
        if ([w isKindOfClass:[NSView class]]) {
            NSView *v = (NSView *)w;
            NSRect f = [v frame];
            f.origin.x = (CGFloat)num_or(x, 0);
            f.origin.y = (CGFloat)num_or(y, 0);
            [v setFrame:f];
            return moo_bool(1);
        }
        return moo_bool(0);
    }
}

/* Entspricht moo_ui_gtk.c:944–953 */
MooValue moo_ui_groesse_setze(MooValue widget, MooValue b, MooValue h) {
    @autoreleasepool {
        id w = unwrap_objc(widget);
        if (!w) return moo_bool(0);
        if ([w isKindOfClass:[NSWindow class]]) {
            NSRect f = [(NSWindow *)w frame];
            f.size.width  = num_or(b, 100);
            f.size.height = num_or(h, 100);
            [(NSWindow *)w setFrame:f display:YES];
            return moo_bool(1);
        }
        if ([w isKindOfClass:[NSView class]]) {
            NSRect f = [(NSView *)w frame];
            if (num_or(b, -1) >= 0) f.size.width  = num_or(b, 100);
            if (num_or(h, -1) >= 0) f.size.height = num_or(h, 100);
            [(NSView *)w setFrame:f];
            return moo_bool(1);
        }
        return moo_bool(0);
    }
}

/* Farbe setzen: setzt textColor bei Text-Controls, backgroundColor bei
 * NSBox, setzt via layer-backing fuer generische NSView.
 * (Entspricht moo_ui_gtk.c:955–968) */
static NSColor *parse_hex_color(const char *hex) {
    if (!hex) return nil;
    if (*hex == '#') hex++;
    if (strlen(hex) < 6) return nil;
    unsigned int r = 0, g = 0, b = 0;
    if (sscanf(hex, "%02x%02x%02x", &r, &g, &b) != 3) return nil;
    return [NSColor colorWithCalibratedRed:(r / 255.0)
                                     green:(g / 255.0)
                                      blue:(b / 255.0)
                                     alpha:1.0];
}

MooValue moo_ui_farbe_setze(MooValue widget, MooValue hex) {
    @autoreleasepool {
        id w = unwrap_objc(widget);
        if (!w) return moo_bool(0);
        NSColor *c = parse_hex_color(str_or(hex, "#000000"));
        if (!c) return moo_bool(0);
        if ([w isKindOfClass:[NSTextField class]]) {
            [(NSTextField *)w setTextColor:c];
        } else if ([w isKindOfClass:[NSButton class]]) {
            /* NSButton hat kein direktes textColor → Attributed-Title. */
            NSButton *btn = (NSButton *)w;
            NSMutableAttributedString *as = [[NSMutableAttributedString alloc]
                                             initWithString:[btn title]];
            [as addAttribute:NSForegroundColorAttributeName
                       value:c
                       range:NSMakeRange(0, as.length)];
            [btn setAttributedTitle:as];
        } else if ([w isKindOfClass:[NSBox class]]) {
            [(NSBox *)w setFillColor:c];
        } else if ([w isKindOfClass:[NSView class]]) {
            NSView *v = (NSView *)w;
            [v setWantsLayer:YES];
            [[v layer] setBackgroundColor:[c CGColor]];
        }
        return moo_bool(1);
    }
}

/* Entspricht moo_ui_gtk.c:970–986 */
MooValue moo_ui_schrift_setze(MooValue widget, MooValue groesse, MooValue fett) {
    @autoreleasepool {
        id w = unwrap_objc(widget);
        if (!w) return moo_bool(0);
        int sz = num_or(groesse, 10);
        BOOL bold = bool_or(fett, NO);
        NSFont *font;
        if (bold) {
            font = [NSFont boldSystemFontOfSize:(CGFloat)sz];
        } else {
            font = [NSFont systemFontOfSize:(CGFloat)sz];
        }
        if ([w isKindOfClass:[NSControl class]]) {
            [(NSControl *)w setFont:font];
        } else if ([w isKindOfClass:[NSTextView class]]) {
            [(NSTextView *)w setFont:font];
        }
        return moo_bool(1);
    }
}

/* Entspricht moo_ui_gtk.c:988–993 */
MooValue moo_ui_tooltip_setze(MooValue widget, MooValue text) {
    @autoreleasepool {
        id w = unwrap_objc(widget);
        if (!w) return moo_bool(0);
        NSString *s = nsstr_or(text, nil);
        if ([w isKindOfClass:[NSView class]])      [(NSView *)w      setToolTip:s];
        else if ([w isKindOfClass:[NSCell class]]) {
            NSView *cv = [(NSCell *)w controlView];
            if (cv) [cv setToolTip:s];
        }
        return moo_bool(1);
    }
}

/* Entspricht moo_ui_gtk.c:995–1000 */
MooValue moo_ui_zerstoere(MooValue widget) {
    @autoreleasepool {
        id w = unwrap_objc(widget);
        if (!w) return moo_bool(0);
        if ([w isKindOfClass:[NSWindow class]]) {
            [(NSWindow *)w close];
            return moo_bool(1);
        }
        if ([w isKindOfClass:[NSView class]]) {
            [(NSView *)w removeFromSuperview];
            return moo_bool(1);
        }
        return moo_bool(0);
    }
}

/* Timer-Bruecke: NSTimer targetet MooCallbackTarget.fire:. */
@interface MooTimerTarget : MooCallbackTarget
@end
@implementation MooTimerTarget
- (void)tick:(NSTimer *)timer {
    (void)timer;
    [self fire:nil];
}
@end

/* Entspricht moo_ui_gtk.c:1016–1023 */
MooValue moo_ui_timer_hinzu(MooValue ms, MooValue callback) {
    @autoreleasepool {
        ensure_cocoa();
        int ival = num_or(ms, 1000);
        MooTimerTarget *tgt = [[MooTimerTarget alloc] initWithCallback:callback];
        NSTimer *t = [NSTimer scheduledTimerWithTimeInterval:(ival / 1000.0)
                                                      target:tgt
                                                    selector:@selector(tick:)
                                                    userInfo:nil
                                                     repeats:YES];
        /* NSTimer retained target → MooTimerTarget und damit die moo-cb
         * bleibt lebendig bis der Timer invalidiert wird. */
        unsigned long id_ = g_next_timer_id++;
        g_timers[@(id_)] = t;
        return moo_number((double)id_);
    }
}

/* Entspricht moo_ui_gtk.c:1025–1029 */
MooValue moo_ui_timer_entfernen(MooValue timer_id) {
    @autoreleasepool {
        unsigned long id_ = (unsigned long)num_or(timer_id, 0);
        if (id_ == 0) return moo_bool(0);
        NSTimer *t = g_timers[@(id_)];
        if (!t) return moo_bool(0);
        [t invalidate];
        [g_timers removeObjectForKey:@(id_)];
        return moo_bool(1);
    }
}

/* ------------------------------------------------------------------ *
 * Dialoge + Menueleiste
 * (Entspricht moo_ui_gtk.c:1031–1248)
 * ------------------------------------------------------------------ */

/* Entspricht moo_ui_gtk.c:1035–1041 */
static NSWindow *resolve_dialog_parent(MooValue parent) {
    id w = unwrap_objc(parent);
    if (!w) return nil;
    if ([w isKindOfClass:[NSWindow class]]) return (NSWindow *)w;
    if ([w isKindOfClass:[NSView class]])   return [(NSView *)w window];
    return nil;
}

/* Entspricht moo_ui_gtk.c:1043–1051 */
static void run_message_dialog(NSWindow *par, NSAlertStyle style,
                                const char *title, const char *text) {
    NSAlert *a = [[NSAlert alloc] init];
    [a setAlertStyle:style];
    if (title) [a setMessageText:[NSString stringWithUTF8String:title]];
    if (text)  [a setInformativeText:[NSString stringWithUTF8String:text]];
    [a addButtonWithTitle:@"OK"];
    if (par) {
        /* Sheet-Modal waere nicht-blocking; hier wollen wir blockend → runModal. */
        [a runModal];
    } else {
        [a runModal];
    }
}

/* Entspricht moo_ui_gtk.c:1053–1058 */
MooValue moo_ui_info(MooValue parent, MooValue titel, MooValue text) {
    @autoreleasepool {
        ensure_cocoa();
        run_message_dialog(resolve_dialog_parent(parent), NSAlertStyleInformational,
                           str_or(titel, "Info"), str_or(text, ""));
    }
    return moo_none();
}

/* Entspricht moo_ui_gtk.c:1060–1065 */
MooValue moo_ui_warnung(MooValue parent, MooValue titel, MooValue text) {
    @autoreleasepool {
        ensure_cocoa();
        run_message_dialog(resolve_dialog_parent(parent), NSAlertStyleWarning,
                           str_or(titel, "Warnung"), str_or(text, ""));
    }
    return moo_none();
}

/* Entspricht moo_ui_gtk.c:1067–1072 */
MooValue moo_ui_fehler(MooValue parent, MooValue titel, MooValue text) {
    @autoreleasepool {
        ensure_cocoa();
        run_message_dialog(resolve_dialog_parent(parent), NSAlertStyleCritical,
                           str_or(titel, "Fehler"), str_or(text, ""));
    }
    return moo_none();
}

/* Entspricht moo_ui_gtk.c:1074–1085 */
MooValue moo_ui_frage(MooValue parent, MooValue titel, MooValue text) {
    @autoreleasepool {
        ensure_cocoa();
        (void)resolve_dialog_parent(parent);
        NSAlert *a = [[NSAlert alloc] init];
        [a setAlertStyle:NSAlertStyleInformational];
        [a setMessageText:nsstr_or(titel, @"Frage")];
        [a setInformativeText:nsstr_or(text, @"")];
        [a addButtonWithTitle:@"Ja"];
        [a addButtonWithTitle:@"Nein"];
        NSModalResponse r = [a runModal];
        return moo_bool(r == NSAlertFirstButtonReturn);
    }
}

/* Entspricht moo_ui_gtk.c:1087–1116 */
MooValue moo_ui_eingabe_dialog(MooValue parent, MooValue titel,
                               MooValue prompt, MooValue vorgabe) {
    @autoreleasepool {
        ensure_cocoa();
        (void)resolve_dialog_parent(parent);
        NSAlert *a = [[NSAlert alloc] init];
        [a setAlertStyle:NSAlertStyleInformational];
        [a setMessageText:nsstr_or(titel, @"Eingabe")];
        [a setInformativeText:nsstr_or(prompt, @"")];
        [a addButtonWithTitle:@"OK"];
        [a addButtonWithTitle:@"Abbrechen"];

        NSTextField *input = [[NSTextField alloc]
                              initWithFrame:NSMakeRect(0, 0, 300, 24)];
        [input setStringValue:nsstr_or(vorgabe, @"")];
        [a setAccessoryView:input];
        /* Initial-Fokus auf Textfeld. */
        [[a window] setInitialFirstResponder:input];

        NSModalResponse r = [a runModal];
        if (r == NSAlertFirstButtonReturn) {
            return moo_string_new([[input stringValue] UTF8String]);
        }
        return moo_none();
    }
}

/* filter: MooList [["Name","*.ext;*.ext2"], ...] → AllowedFileTypes (nur
 * die Endungen werden extrahiert; NSOpen/SavePanel kennt keine ;Globs). */
static NSArray<NSString *> *filter_to_types(MooValue filter) {
    if (filter.tag != MOO_LIST) return nil;
    NSMutableArray<NSString *> *types = [NSMutableArray array];
    int32_t n = moo_list_iter_len(filter);
    for (int32_t i = 0; i < n; i++) {
        MooValue pair = moo_list_iter_get(filter, i);
        if (pair.tag != MOO_LIST) continue;
        if (moo_list_iter_len(pair) < 2) continue;
        MooValue pt = moo_list_iter_get(pair, 1);
        if (pt.tag != MOO_STRING) continue;
        const char *src = MV_STR(pt)->chars;
        char *copy = strdup(src);
        for (char *tok = strtok(copy, ";"); tok; tok = strtok(NULL, ";")) {
            /* "*.png" → "png" */
            char *dot = strrchr(tok, '.');
            if (dot && *(dot + 1)) {
                [types addObject:[NSString stringWithUTF8String:(dot + 1)]];
            }
        }
        free(copy);
    }
    return types.count > 0 ? types : nil;
}

/* Entspricht moo_ui_gtk.c:1144–1163 + 1165–1168 */
MooValue moo_ui_datei_oeffnen(MooValue parent, MooValue titel, MooValue filter) {
    @autoreleasepool {
        ensure_cocoa();
        (void)resolve_dialog_parent(parent);
        NSOpenPanel *op = [NSOpenPanel openPanel];
        [op setTitle:nsstr_or(titel, @"Oeffnen")];
        [op setCanChooseFiles:YES];
        [op setCanChooseDirectories:NO];
        [op setAllowsMultipleSelection:NO];
        NSArray<NSString *> *types = filter_to_types(filter);
        if (types) [op setAllowedFileTypes:types];
        if ([op runModal] == NSModalResponseOK) {
            NSURL *u = [[op URLs] firstObject];
            if (u) return moo_string_new([[u path] UTF8String]);
        }
        return moo_none();
    }
}

/* Entspricht moo_ui_gtk.c:1170–1173 */
MooValue moo_ui_datei_speichern(MooValue parent, MooValue titel, MooValue filter) {
    @autoreleasepool {
        ensure_cocoa();
        (void)resolve_dialog_parent(parent);
        NSSavePanel *sp = [NSSavePanel savePanel];
        [sp setTitle:nsstr_or(titel, @"Speichern")];
        NSArray<NSString *> *types = filter_to_types(filter);
        if (types) [sp setAllowedFileTypes:types];
        if ([sp runModal] == NSModalResponseOK) {
            NSURL *u = [sp URL];
            if (u) return moo_string_new([[u path] UTF8String]);
        }
        return moo_none();
    }
}

/* Entspricht moo_ui_gtk.c:1175–1178 */
MooValue moo_ui_ordner_waehlen(MooValue parent, MooValue titel) {
    @autoreleasepool {
        ensure_cocoa();
        (void)resolve_dialog_parent(parent);
        NSOpenPanel *op = [NSOpenPanel openPanel];
        [op setTitle:nsstr_or(titel, @"Ordner waehlen")];
        [op setCanChooseFiles:NO];
        [op setCanChooseDirectories:YES];
        [op setAllowsMultipleSelection:NO];
        if ([op runModal] == NSModalResponseOK) {
            NSURL *u = [[op URLs] firstObject];
            if (u) return moo_string_new([[u path] UTF8String]);
        }
        return moo_none();
    }
}

/* ------------------------------------------------------------------ *
 * Menueleiste
 * (Entspricht moo_ui_gtk.c:1180–1248)
 *
 * WICHTIG: macOS hat EINE globale Menueleiste pro App (nicht pro Fenster).
 * Wir ignorieren den fenster-Parameter fuer [NSApp setMainMenu:], behalten
 * aber die API-Signatur bei. Jedes Fenster bekommt zur Referenz einen
 * zeiger auf die MainMenu gespeichert.
 * ------------------------------------------------------------------ */

/* Entspricht moo_ui_gtk.c:1182–1193 */
MooValue moo_ui_menueleiste(MooValue fenster) {
    @autoreleasepool {
        id win = unwrap_objc(fenster);
        if (!win || ![win isKindOfClass:[NSWindow class]]) return moo_bool(0);
        ensure_cocoa();
        NSMenu *mb = [NSApp mainMenu];
        if (!mb) {
            mb = [[NSMenu alloc] initWithTitle:@""];
            [NSApp setMainMenu:mb];
            /* macOS erwartet typischerweise einen App-Menue-Eintrag als erstes
             * Item der Menueleiste. Wir legen einen leeren Anker an. */
            NSMenuItem *appItem = [[NSMenuItem alloc] initWithTitle:@""
                                                             action:nil
                                                      keyEquivalent:@""];
            NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@""];
            [appItem setSubmenu:appMenu];
            [mb addItem:appItem];
        }
        objc_setAssociatedObject(win, kMooMenuBarKey, mb,
                                 OBJC_ASSOCIATION_ASSIGN);
        return wrap_objc(mb);
    }
}

/* Entspricht moo_ui_gtk.c:1196–1205 */
MooValue moo_ui_menue(MooValue leiste, MooValue titel) {
    @autoreleasepool {
        id mb = unwrap_objc(leiste);
        if (!mb || ![mb isKindOfClass:[NSMenu class]]) return moo_bool(0);
        NSString *t = nsstr_or(titel, @"");
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:t
                                                      action:nil
                                               keyEquivalent:@""];
        NSMenu *sub = [[NSMenu alloc] initWithTitle:t];
        [item setSubmenu:sub];
        [(NSMenu *)mb addItem:item];
        return wrap_objc(sub);
    }
}

/* Entspricht moo_ui_gtk.c:1213–1226 */
MooValue moo_ui_menue_eintrag(MooValue menue, MooValue titel, MooValue callback) {
    @autoreleasepool {
        id m = unwrap_objc(menue);
        if (!m || ![m isKindOfClass:[NSMenu class]]) return moo_bool(0);
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:nsstr_or(titel, @"")
                                                      action:nil
                                               keyEquivalent:@""];
        if (callback.tag == MOO_FUNC) {
            MooCallbackTarget *tgt = [[MooCallbackTarget alloc]
                                       initWithCallback:callback];
            [item setTarget:tgt];
            [item setAction:@selector(fire:)];
            objc_setAssociatedObject(item, kMooCBTargetKey, tgt,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        [(NSMenu *)m addItem:item];
        return wrap_objc(item);
    }
}

/* Entspricht moo_ui_gtk.c:1228–1235 */
MooValue moo_ui_menue_trenner(MooValue menue) {
    @autoreleasepool {
        id m = unwrap_objc(menue);
        if (!m || ![m isKindOfClass:[NSMenu class]]) return moo_bool(0);
        NSMenuItem *sep = [NSMenuItem separatorItem];
        [(NSMenu *)m addItem:sep];
        return wrap_objc(sep);
    }
}

/* Entspricht moo_ui_gtk.c:1239–1248 */
MooValue moo_ui_menue_untermenue(MooValue menue, MooValue titel) {
    @autoreleasepool {
        id m = unwrap_objc(menue);
        if (!m || ![m isKindOfClass:[NSMenu class]]) return moo_bool(0);
        NSString *t = nsstr_or(titel, @"");
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:t
                                                      action:nil
                                               keyEquivalent:@""];
        NSMenu *sub = [[NSMenu alloc] initWithTitle:t];
        [item setSubmenu:sub];
        [(NSMenu *)m addItem:item];
        return wrap_objc(sub);
    }
}

/* ================================================================== *
 * Welle 2 / Plan 003 P2 — ListView-Erweiterungen
 *
 * Alle Funktionen arbeiten auf dem von moo_ui_liste gelieferten
 * Scroll-View-Handle. liste_ds/liste_tv liefern datenquelle bzw.
 * NSTableView. Konsistent zur GTK-Impl (moo_ui_gtk.c:1260+).
 * ================================================================== */

/* 0-basierter Spalten-Zugriff mit Bound-Check. */
static NSTableColumn *liste_col(NSTableView *tv, int index) {
    if (!tv) return nil;
    NSArray<NSTableColumn *> *cols = [tv tableColumns];
    if (index < 0 || index >= (int)cols.count) return nil;
    return cols[index];
}

/* P2: Spaltenbreite setzen (Pixel). */
MooValue moo_ui_liste_spalte_breite(MooValue liste, MooValue spalte_index,
                                    MooValue breite) {
    @autoreleasepool {
        NSTableView *tv = liste_tv(liste);
        NSTableColumn *col = liste_col(tv, num_or(spalte_index, -1));
        if (!col) return moo_bool(0);
        CGFloat bw = (breite.tag == MOO_NUMBER) ? MV_NUM(breite) : 0.0;
        if (bw < 1.0) bw = 1.0;
        [col setWidth:bw];
        return moo_bool(1);
    }
}

/* P2: Spalte sortierbar (Klick auf Header loest Sort aus).
 * Wir installieren einen NSSortDescriptor mit String-Comparator. */
MooValue moo_ui_liste_sortierbar(MooValue liste, MooValue spalte_index,
                                 MooValue aktiv) {
    @autoreleasepool {
        NSTableView *tv = liste_tv(liste);
        NSTableColumn *col = liste_col(tv, num_or(spalte_index, -1));
        if (!col) return moo_bool(0);
        BOOL an = bool_or(aktiv, YES);
        if (an) {
            NSString *key = [NSString stringWithFormat:@"c%ld",
                             (long)[[tv tableColumns] indexOfObject:col]];
            NSSortDescriptor *sd = [NSSortDescriptor
                sortDescriptorWithKey:key
                            ascending:YES
                             selector:@selector(localizedCaseInsensitiveCompare:)];
            [col setSortDescriptorPrototype:sd];
        } else {
            [col setSortDescriptorPrototype:nil];
        }
        return moo_bool(1);
    }
}

/* P2: Programmatische Sortierung (lexikografisch, keine Auswahl-Events).
 * Sortiert direkt die Backing-Rows und ruft reloadData. */
MooValue moo_ui_liste_sortiere(MooValue liste, MooValue spalte_index,
                               MooValue aufsteigend) {
    @autoreleasepool {
        MooTableDataSource *ds = liste_ds(liste);
        NSTableView *tv = liste_tv(liste);
        if (!ds || !tv) return moo_bool(0);
        int col = num_or(spalte_index, -1);
        if (col < 0 || col >= (int)ds.ncols) return moo_bool(0);
        BOOL asc = bool_or(aufsteigend, YES);
        [ds.rows sortUsingComparator:^NSComparisonResult(NSArray<NSString *> *a,
                                                         NSArray<NSString *> *b) {
            NSString *av = (col < (int)a.count) ? a[col] : @"";
            NSString *bv = (col < (int)b.count) ? b[col] : @"";
            NSComparisonResult r = [av localizedCaseInsensitiveCompare:bv];
            return asc ? r : (NSComparisonResult)-r;
        }];
        [tv reloadData];
        return moo_bool(1);
    }
}

/* P2: Ganze Zeile ersetzen — werte_liste muss so viele Eintraege wie
 * Spalten haben, sonst Abbruch ohne Teil-Aenderung (wie GTK-Semantik). */
MooValue moo_ui_liste_zeile_setze(MooValue liste, MooValue zeile_index,
                                  MooValue werte_liste) {
    @autoreleasepool {
        MooTableDataSource *ds = liste_ds(liste);
        NSTableView *tv = liste_tv(liste);
        if (!ds || !tv) return moo_bool(0);
        int idx = num_or(zeile_index, -1);
        if (idx < 0 || idx >= (int)ds.rows.count) return moo_bool(0);
        if (werte_liste.tag != MOO_LIST) return moo_bool(0);
        int32_t n = moo_list_iter_len(werte_liste);
        if (n != ds.ncols) return moo_bool(0);
        NSMutableArray<NSString *> *row =
            [NSMutableArray arrayWithCapacity:ds.ncols];
        for (int32_t i = 0; i < n; i++) {
            MooValue c = moo_list_iter_get(werte_liste, i);
            NSString *s = @"";
            if (c.tag == MOO_STRING) {
                s = [NSString stringWithUTF8String:MV_STR(c)->chars];
            } else if (c.tag == MOO_NUMBER) {
                s = [NSString stringWithFormat:@"%g", MV_NUM(c)];
            }
            [row addObject:s];
        }
        ds.rows[idx] = row;
        [tv reloadDataForRowIndexes:[NSIndexSet indexSetWithIndex:idx]
                      columnIndexes:[NSIndexSet indexSetWithIndexesInRange:
                                     NSMakeRange(0, ds.ncols)]];
        return moo_bool(1);
    }
}

/* P2: Einzelne Zelle setzen — wert wird per moo_to_string koerziert. */
MooValue moo_ui_liste_zelle_setze(MooValue liste, MooValue zeile_index,
                                  MooValue spalte_index, MooValue wert) {
    @autoreleasepool {
        MooTableDataSource *ds = liste_ds(liste);
        NSTableView *tv = liste_tv(liste);
        if (!ds || !tv) return moo_bool(0);
        int r = num_or(zeile_index, -1);
        int c = num_or(spalte_index, -1);
        if (r < 0 || r >= (int)ds.rows.count) return moo_bool(0);
        if (c < 0 || c >= (int)ds.ncols)      return moo_bool(0);
        NSMutableArray<NSString *> *row = [ds.rows[r] mutableCopy];
        /* Falls die bestehende Zeile weniger Zellen hat, auffuellen. */
        while ((int)row.count <= c) [row addObject:@""];
        NSString *s = @"";
        if (wert.tag == MOO_STRING) {
            s = [NSString stringWithUTF8String:MV_STR(wert)->chars];
        } else if (wert.tag == MOO_NUMBER) {
            s = [NSString stringWithFormat:@"%g", MV_NUM(wert)];
        } else if (wert.tag == MOO_BOOL) {
            s = MV_BOOL(wert) ? @"wahr" : @"falsch";
        }
        row[c] = s;
        ds.rows[r] = row;
        [tv reloadDataForRowIndexes:[NSIndexSet indexSetWithIndex:r]
                      columnIndexes:[NSIndexSet indexSetWithIndex:c]];
        return moo_bool(1);
    }
}

/* P2: Zeile entfernen. Auswahl wird auf -1 zurueckgesetzt, kein
 * on_auswahl-Callback wird gefeuert (entspricht Header-Doku). */
MooValue moo_ui_liste_entferne(MooValue liste, MooValue zeile_index) {
    @autoreleasepool {
        MooTableDataSource *ds = liste_ds(liste);
        NSTableView *tv = liste_tv(liste);
        if (!ds || !tv) return moo_bool(0);
        int idx = num_or(zeile_index, -1);
        if (idx < 0 || idx >= (int)ds.rows.count) return moo_bool(0);
        [ds.rows removeObjectAtIndex:idx];
        /* Delegate-Callback beim Deselect unterdruecken: Target kurz ausblenden. */
        MooCallbackTarget *saved = ds.selectTarget;
        ds.selectTarget = nil;
        [tv deselectAll:nil];
        [tv removeRowsAtIndexes:[NSIndexSet indexSetWithIndex:idx]
                 withAnimation:NSTableViewAnimationEffectNone];
        [tv reloadData];
        ds.selectTarget = saved;
        return moo_bool(1);
    }
}

/* ================================================================== *
 * Welle 2 / Plan 003 P5 — Leinwand-Maus-Events + Text-Metrik
 * ================================================================== */

/* Hilfs-Getter: wrappt die MooCanvasView aus dem leinwand-Handle. */
static MooCanvasView *canvas_view(MooValue leinwand) {
    id v = unwrap_objc(leinwand);
    if (!v || ![v isKindOfClass:[MooCanvasView class]]) return nil;
    return (MooCanvasView *)v;
}

MooValue moo_ui_leinwand_on_maus(MooValue leinwand, MooValue callback) {
    @autoreleasepool {
        MooCanvasView *cv = canvas_view(leinwand);
        if (!cv) return moo_bool(0);
        if (callback.tag != MOO_FUNC) {
            /* Release-Pfad: Binding entfernen. */
            cv.mouseTarget = nil;
            objc_setAssociatedObject(cv, kMooCBTargetMouseKey, nil,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            return moo_bool(1);
        }
        MooCallbackTarget *tgt = [[MooCallbackTarget alloc]
                                   initWithCallback:callback];
        /* Altes Target implizit released — property-assign + associated
         * object-Retain-Notify. */
        cv.mouseTarget = tgt;
        objc_setAssociatedObject(cv, kMooCBTargetMouseKey, tgt,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return moo_bool(1);
    }
}

MooValue moo_ui_leinwand_on_bewegung(MooValue leinwand, MooValue callback) {
    @autoreleasepool {
        MooCanvasView *cv = canvas_view(leinwand);
        if (!cv) return moo_bool(0);
        if (callback.tag != MOO_FUNC) {
            cv.motionTarget = nil;
            objc_setAssociatedObject(cv, kMooCBTargetMotionKey, nil,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            /* Tracking-Area abbauen ueber updateTrackingAreas. */
            [cv updateTrackingAreas];
            return moo_bool(1);
        }
        MooCallbackTarget *tgt = [[MooCallbackTarget alloc]
                                   initWithCallback:callback];
        cv.motionTarget = tgt;
        objc_setAssociatedObject(cv, kMooCBTargetMotionKey, tgt,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        /* Tracking-Area (neu) aufsetzen — liefert fortan mouseMoved-Events
         * sobald das Fenster Key-Window ist. */
        [cv updateTrackingAreas];
        return moo_bool(1);
    }
}

/* Text-Metrik: [NSString sizeWithAttributes:].width — nur innerhalb des
 * drawRect-Kontextes gueltig. Ausserhalb liefert 0. */
MooValue moo_ui_zeichne_text_breite(MooValue zeichner, MooValue text,
                                    MooValue groesse) {
    @autoreleasepool {
        MooZeichnerCG *z = unwrap_zeichner_cg(zeichner);
        if (!z) return moo_number(0.0);
        NSString *s = nsstr_or(text, @"");
        if (s.length == 0) return moo_number(0.0);
        CGFloat sz = (groesse.tag == MOO_NUMBER) ? MV_NUM(groesse) : 12.0;
        if (sz < 1.0) sz = 1.0;
        NSDictionary *attrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:sz],
        };
        NSSize sizePx = [s sizeWithAttributes:attrs];
        return moo_number((double)ceil(sizePx.width));
    }
}

/* ================================================================== *
 * Welle 2 — Keyboard-Shortcuts (NACHZIEHEN: Cocoa-Impl hat gefehlt)
 *
 * Pro Fenster ein einziger NSEvent-local-Monitor, der alle registrierten
 * Sequenzen dispatcht. Der Monitor wird lazy beim ersten Bind angelegt
 * und beim letzten Loese (oder Fenster-Close) entfernt.
 *
 * Eine Shortcuts-Registry pro Fenster (NSMutableArray von
 * MooShortcutEntry) wird via associated object "moo-shortcuts"
 * verwaltet. Die Registry enthaelt:
 *   - keyCode (virtual key, NSEvent keyCode)
 *   - characters (case-insensitive Lower-Case, z.B. "s")
 *   - modifier-Mask (NSEventModifierFlagCommand etc.)
 *   - callback-Target
 *   - monitor-Handle (nur im ersten Entry relevant — siehe unten)
 *
 * Modifier-Mapping:
 *   Ctrl/Strg/Control → NSEventModifierFlagControl
 *   Shift/Umschalt    → NSEventModifierFlagShift
 *   Alt / Option      → NSEventModifierFlagOption
 *   Cmd/Super/Meta/Win → NSEventModifierFlagCommand
 *
 * Das ist bewusst "Cocoa-literal": auf macOS ist Cmd der uebliche
 * App-Hotkey-Modifier, waehrend auf Linux/Windows Ctrl ueblich ist.
 * Moo-Apps sollten "Ctrl+S" schreiben — wir mappen das auf
 * NSEventModifierFlagControl. Wer explizit Cmd will, schreibt "Cmd+S".
 * ================================================================== */

/* Shortcut-Registry: pro Entry ein NSDictionary mit Keys
 *   "mods"    → NSNumber(NSEventModifierFlags)
 *   "keyChar" → NSString (Lower-Case 1-char, leer falls keyCode benutzt)
 *   "keyCode" → NSNumber(int, virtual-key — -1 falls keyChar benutzt)
 *   "target"  → MooCallbackTarget (hat strong-Ref ueber NSDictionary). */
@interface MooShortcutList : NSObject
@property (nonatomic, strong) NSMutableArray<NSDictionary *> *entries;
@property (nonatomic, strong, nullable) id monitor;
@end
@implementation MooShortcutList
- (instancetype)init {
    if ((self = [super init])) { _entries = [NSMutableArray array]; }
    return self;
}
- (void)dealloc {
    if (self.monitor) {
        [NSEvent removeMonitor:self.monitor];
        self.monitor = nil;
    }
}
@end

/* Key-Normalisierung analog GTK — DE/EN-Synonyme fuer Nicht-ASCII-Tasten.
 * Fuer ASCII-Buchstaben/Ziffern geben wir den Lower-Case-Char zurueck.
 * Fuer Spezial-Tasten (F1..F24, Pfeile, Home/End, ...) setzen wir *outKeyCode
 * auf den virtuellen KeyCode. Einer von beiden wird gesetzt, der andere
 * behaelt den Default-Wert (empty string / -1). */
static BOOL cocoa_normalize_key(const char *tok, NSString **outChar,
                                int *outKeyCode) {
    *outChar = @"";
    *outKeyCode = -1;
    if (!tok || !*tok) return NO;

    char low[64];
    size_t n = 0;
    while (tok[n] && n < sizeof(low) - 1) {
        low[n] = (char)tolower((unsigned char)tok[n]);
        n++;
    }
    low[n] = 0;

    /* Einzelnes ASCII-Zeichen (Buchstabe/Ziffer/Sonderzeichen). */
    if (low[0] && !low[1]) {
        *outChar = [NSString stringWithFormat:@"%c", low[0]];
        return YES;
    }

    /* F1..F24 */
    if (low[0] == 'f' && low[1]) {
        int all_digits = 1;
        for (size_t i = 1; low[i]; i++) {
            if (low[i] < '0' || low[i] > '9') { all_digits = 0; break; }
        }
        if (all_digits) {
            int fn = atoi(&low[1]);
            /* macOS virtual key-codes fuer F1..F20.
             * F1=0x7A, F2=0x78, F3=0x63, F4=0x76, F5=0x60, F6=0x61,
             * F7=0x62, F8=0x64, F9=0x65, F10=0x6D, F11=0x67, F12=0x6F,
             * F13=0x69, F14=0x6B, F15=0x71, F16=0x6A, F17=0x40,
             * F18=0x4F, F19=0x50, F20=0x5A. */
            static const int fkeys[] = {
                0, 0x7A, 0x78, 0x63, 0x76, 0x60, 0x61, 0x62, 0x64, 0x65,
                0x6D, 0x67, 0x6F, 0x69, 0x6B, 0x71, 0x6A, 0x40, 0x4F,
                0x50, 0x5A
            };
            if (fn >= 1 && fn <= 20) {
                *outKeyCode = fkeys[fn];
                return YES;
            }
        }
    }

    /* DE/EN Spezial-Tasten → KeyCode. */
    struct { const char *alias; int code; } keymap[] = {
        {"pos1",        0x73}, {"home",        0x73},
        {"ende",        0x77}, {"end",         0x77},
        {"bildhoch",    0x74}, {"pageup",      0x74},
        {"bildrunter",  0x79}, {"pagedown",    0x79},
        {"entf",        0x75}, {"delete",      0x75},
        {"rueckschritt",0x33}, {"backspace",   0x33},
        {"einfg",       0x72}, {"insert",      0x72},
        {"hoch",        0x7E}, {"up",          0x7E},
        {"runter",      0x7D}, {"down",        0x7D},
        {"links",       0x7B}, {"left",        0x7B},
        {"rechts",      0x7C}, {"right",       0x7C},
        {"esc",         0x35}, {"escape",      0x35},
        {"tab",         0x30},
        {"enter",       0x24}, {"return",      0x24},
        {"space",       0x31}, {"leertaste",   0x31},
        {NULL, 0}
    };
    for (int i = 0; keymap[i].alias; i++) {
        if (!strcmp(low, keymap[i].alias)) {
            *outKeyCode = keymap[i].code;
            return YES;
        }
    }

    /* Mehr-Zeichen-Text-Tokens (komma, punkt, slash, plus, minus) →
     * passende 1-Zeichen-Variante. */
    if (!strcmp(low, "komma") || !strcmp(low, "comma"))   { *outChar = @","; return YES; }
    if (!strcmp(low, "punkt") || !strcmp(low, "period"))  { *outChar = @"."; return YES; }
    if (!strcmp(low, "slash"))                            { *outChar = @"/"; return YES; }
    if (!strcmp(low, "plus"))                             { *outChar = @"+"; return YES; }
    if (!strcmp(low, "minus"))                            { *outChar = @"-"; return YES; }

    return NO;
}

/* Parst "Ctrl+Shift+S" → mods + keyChar/keyCode. */
static BOOL cocoa_parse_sequence(const char *seq, NSEventModifierFlags *outMods,
                                 NSString **outChar, int *outKeyCode) {
    *outMods = 0;
    *outChar = @"";
    *outKeyCode = -1;
    if (!seq || !*seq) return NO;

    NSString *s = [NSString stringWithUTF8String:seq];
    NSArray<NSString *> *toks = [s componentsSeparatedByString:@"+"];
    if (toks.count < 1) return NO;

    /* Alle bis auf das letzte Token sind Modifier. */
    for (NSUInteger i = 0; i + 1 < toks.count; i++) {
        NSString *mt = [[toks[i] stringByTrimmingCharactersInSet:
                         [NSCharacterSet whitespaceCharacterSet]]
                        lowercaseString];
        if ([mt isEqualToString:@"ctrl"] || [mt isEqualToString:@"control"] ||
            [mt isEqualToString:@"strg"]) {
            *outMods |= NSEventModifierFlagControl;
        } else if ([mt isEqualToString:@"shift"] ||
                   [mt isEqualToString:@"umschalt"]) {
            *outMods |= NSEventModifierFlagShift;
        } else if ([mt isEqualToString:@"alt"] ||
                   [mt isEqualToString:@"option"]) {
            *outMods |= NSEventModifierFlagOption;
        } else if ([mt isEqualToString:@"cmd"] || [mt isEqualToString:@"super"] ||
                   [mt isEqualToString:@"meta"] || [mt isEqualToString:@"win"]) {
            *outMods |= NSEventModifierFlagCommand;
        } else {
            return NO;  /* Unbekannter Modifier */
        }
    }

    NSString *last = [[toks lastObject] stringByTrimmingCharactersInSet:
                      [NSCharacterSet whitespaceCharacterSet]];
    return cocoa_normalize_key([last UTF8String], outChar, outKeyCode);
}

/* Relevante Modifier-Bits (Caps-Lock etc. ignorieren). */
static NSEventModifierFlags relevant_mods(NSEventModifierFlags f) {
    return f & (NSEventModifierFlagControl | NSEventModifierFlagShift
              | NSEventModifierFlagOption  | NSEventModifierFlagCommand);
}

/* Lazy-Getter fuer die Shortcut-Liste pro Fenster. */
static MooShortcutList *window_shortcuts(NSWindow *win, BOOL create) {
    id sl = objc_getAssociatedObject(win, kMooShortcutsKey);
    if (sl && [sl isKindOfClass:[MooShortcutList class]]) {
        return (MooShortcutList *)sl;
    }
    if (!create) return nil;
    MooShortcutList *list = [[MooShortcutList alloc] init];
    objc_setAssociatedObject(win, kMooShortcutsKey, list,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return list;
}

/* Event-Monitor fuer ein Fenster aufbauen (einmal pro Fenster). */
static void ensure_shortcut_monitor(NSWindow *win, MooShortcutList *list) {
    if (list.monitor) return;
    __weak NSWindow *weakWin = win;
    __weak MooShortcutList *weakList = list;
    list.monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
        handler:^NSEvent *(NSEvent *event) {
            NSWindow *strongWin = weakWin;
            MooShortcutList *strongList = weakList;
            if (!strongWin || !strongList) return event;
            /* Nur Events fuer DIESES Fenster dispatchen — NSEvent.window ist
             * das Key-Window bei Eingang, nicht unbedingt unseres. */
            if ([event window] != strongWin) return event;

            NSEventModifierFlags em = relevant_mods([event modifierFlags]);
            NSString *chars = [[event charactersIgnoringModifiers] lowercaseString];
            int kc = (int)[event keyCode];

            for (NSDictionary *ent in [strongList.entries copy]) {
                NSEventModifierFlags em2 = (NSEventModifierFlags)
                    [[ent objectForKey:@"mods"] unsignedIntegerValue];
                NSString *kChar = [ent objectForKey:@"keyChar"];
                int      kCode = [[ent objectForKey:@"keyCode"] intValue];
                if (em != em2) continue;
                BOOL match = NO;
                if (kChar.length > 0 && chars.length > 0) {
                    match = [chars isEqualToString:kChar];
                } else if (kCode >= 0) {
                    match = (kc == kCode);
                }
                if (match) {
                    MooCallbackTarget *tgt = [ent objectForKey:@"target"];
                    if (tgt) [tgt fire:nil];
                    return nil;  /* Event konsumieren */
                }
            }
            return event;
        }];
}

MooValue moo_ui_shortcut_bind(MooValue fenster, MooValue sequenz,
                              MooValue callback) {
    @autoreleasepool {
        id w = unwrap_objc(fenster);
        if (!w || ![w isKindOfClass:[NSWindow class]]) return moo_bool(0);
        if (sequenz.tag != MOO_STRING) return moo_bool(0);
        NSWindow *win = (NSWindow *)w;

        NSEventModifierFlags mods = 0;
        NSString *keyChar = @"";
        int keyCode = -1;
        if (!cocoa_parse_sequence(MV_STR(sequenz)->chars,
                                  &mods, &keyChar, &keyCode)) {
            return moo_bool(0);
        }

        MooShortcutList *list = window_shortcuts(win, YES);

        /* Still-replace: existiert bereits ein Binding fuer (mods, key)? */
        NSMutableArray *keep = [NSMutableArray array];
        for (NSDictionary *ent in list.entries) {
            NSEventModifierFlags em2 = (NSEventModifierFlags)
                [[ent objectForKey:@"mods"] unsignedIntegerValue];
            NSString *kChar = [ent objectForKey:@"keyChar"];
            int      kCode = [[ent objectForKey:@"keyCode"] intValue];
            BOOL sameKey = NO;
            if (keyChar.length > 0 && kChar.length > 0) {
                sameKey = [kChar isEqualToString:keyChar];
            } else if (keyCode >= 0 && kCode >= 0) {
                sameKey = (kCode == keyCode);
            }
            if (em2 == mods && sameKey) continue;  /* alten Eintrag rauswerfen */
            [keep addObject:ent];
        }
        list.entries = keep;

        MooCallbackTarget *tgt = [[MooCallbackTarget alloc]
                                   initWithCallback:callback];
        NSDictionary *entry = @{
            @"mods":    @((NSUInteger)mods),
            @"keyChar": keyChar,
            @"keyCode": @(keyCode),
            @"target":  tgt,
        };
        [list.entries addObject:entry];
        ensure_shortcut_monitor(win, list);

        return moo_bool(1);
    }
}

MooValue moo_ui_shortcut_loese(MooValue fenster, MooValue sequenz) {
    @autoreleasepool {
        id w = unwrap_objc(fenster);
        if (!w || ![w isKindOfClass:[NSWindow class]]) return moo_bool(0);
        if (sequenz.tag != MOO_STRING) return moo_bool(0);
        NSWindow *win = (NSWindow *)w;

        MooShortcutList *list = window_shortcuts(win, NO);
        if (!list || list.entries.count == 0) return moo_bool(0);

        NSEventModifierFlags mods = 0;
        NSString *keyChar = @"";
        int keyCode = -1;
        if (!cocoa_parse_sequence(MV_STR(sequenz)->chars,
                                  &mods, &keyChar, &keyCode)) {
            return moo_bool(0);
        }

        BOOL removed = NO;
        NSMutableArray *keep = [NSMutableArray array];
        for (NSDictionary *ent in list.entries) {
            NSEventModifierFlags em2 = (NSEventModifierFlags)
                [[ent objectForKey:@"mods"] unsignedIntegerValue];
            NSString *kChar = [ent objectForKey:@"keyChar"];
            int      kCode = [[ent objectForKey:@"keyCode"] intValue];
            BOOL sameKey = NO;
            if (keyChar.length > 0 && kChar.length > 0) {
                sameKey = [kChar isEqualToString:keyChar];
            } else if (keyCode >= 0 && kCode >= 0) {
                sameKey = (kCode == keyCode);
            }
            if (em2 == mods && sameKey) {
                removed = YES;  /* MooCallbackTarget wird via NSArray-Release freigegeben */
                continue;
            }
            [keep addObject:ent];
        }
        list.entries = keep;

        /* Wenn keine Entries mehr → Monitor abbauen. */
        if (list.entries.count == 0 && list.monitor) {
            [NSEvent removeMonitor:list.monitor];
            list.monitor = nil;
        }
        return moo_bool(removed);
    }
}

/* ==========================================================================
 * Plan-004 P1 — Widget-Introspection
 * --------------------------------------------------------------------------
 * Siehe moo_ui.h (Block "Widget-Introspection").
 * Speichert die String-ID per objc_setAssociatedObject(kMooUIIdKey) am
 * Widget-Objekt (NSWindow / NSView). ARC halt die NSString-Kopie am Leben,
 * Cleanup passiert automatisch wenn das Widget deallokiert wird.
 * ========================================================================== */

/* Typ-Mapping Cocoa-Klasse → moo-Typstring.
 * Reihenfolge wichtig: konkretere Subklassen zuerst pruefen (NSSecureTextField
 * vor NSTextField, NSPopUpButton vor NSButton etc.). */
static const char *cocoa_widget_typ(id obj) {
    if (!obj) return "unbekannt";
    if ([obj isKindOfClass:[NSWindow class]])            return "fenster";
    if ([obj isKindOfClass:[MooCanvasView class]])       return "leinwand";
    if ([obj isKindOfClass:[NSPopUpButton class]])       return "dropdown";
    if ([obj isKindOfClass:[NSButton class]]) {
        /* NSButton hat keinen oeffentlichen buttonType-Getter — wir lesen
         * showsStateBy/highlightsBy der Cell:
         *   Push:     showsStateBy = 0
         *   Switch:   showsStateBy & NSContentsCellMask, highlightsBy enthaelt
         *             NSChangeBackgroundCellMask
         *   Radio:    showsStateBy & NSContentsCellMask, highlightsBy enthaelt
         *             NSChangeGrayCellMask (aber NICHT Background). */
        NSButtonCell *cell = [(NSButton *)obj cell];
        NSInteger shows      = [cell showsStateBy];
        NSInteger highlights = [cell highlightsBy];
        if (shows & NSContentsCellMask) {
            if (highlights & NSChangeBackgroundCellMask) return "checkbox";
            return "radio";
        }
        return "knopf";
    }
    if ([obj isKindOfClass:[NSSecureTextField class]])   return "eingabe";
    if ([obj isKindOfClass:[NSTextField class]]) {
        NSTextField *tf = (NSTextField *)obj;
        if ([tf isEditable]) return "eingabe";
        return "label";
    }
    if ([obj isKindOfClass:[NSTextView class]])          return "textbereich";
    if ([obj isKindOfClass:[NSSlider class]])            return "slider";
    if ([obj isKindOfClass:[NSProgressIndicator class]]) return "fortschritt";
    if ([obj isKindOfClass:[NSTabView class]])           return "tabs";
    if ([obj isKindOfClass:[NSTableView class]])         return "liste";
    if ([obj isKindOfClass:[NSScrollView class]])        return "scroll";
    if ([obj isKindOfClass:[NSImageView class]])         return "bild";
    if ([obj isKindOfClass:[NSBox class]]) {
        NSBox *bx = (NSBox *)obj;
        if ([bx boxType] == NSBoxSeparator) return "trenner";
        return "rahmen";
    }
    if ([obj isKindOfClass:[NSMenu class]])              return "menue";
    if ([obj isKindOfClass:[NSView class]])              return "container";
    return "unbekannt";
}

/* Liefert den sichtbaren Text eines Widgets, falls sinnvoll.
 * Rueckgabe: MOO_STRING oder MOO_NONE. */
static MooValue cocoa_widget_text(id obj) {
    if (!obj) return moo_none();
    if ([obj isKindOfClass:[NSWindow class]]) {
        NSString *t = [(NSWindow *)obj title];
        if (!t) return moo_none();
        return moo_string_new([t UTF8String]);
    }
    if ([obj isKindOfClass:[NSButton class]]) {
        NSString *t = [(NSButton *)obj title];
        if (!t) return moo_none();
        return moo_string_new([t UTF8String]);
    }
    if ([obj isKindOfClass:[NSTextField class]]) {
        NSString *s = [(NSTextField *)obj stringValue];
        if (!s) return moo_none();
        return moo_string_new([s UTF8String]);
    }
    if ([obj isKindOfClass:[NSTextView class]]) {
        NSString *s = [[(NSTextView *)obj textStorage] string];
        if (!s) return moo_none();
        return moo_string_new([s UTF8String]);
    }
    if ([obj isKindOfClass:[NSPopUpButton class]]) {
        NSString *t = [(NSPopUpButton *)obj titleOfSelectedItem];
        if (!t) return moo_none();
        return moo_string_new([t UTF8String]);
    }
    if ([obj isKindOfClass:[NSBox class]]) {
        NSString *t = [(NSBox *)obj title];
        if (!t || [t length] == 0) return moo_none();
        return moo_string_new([t UTF8String]);
    }
    return moo_none();
}

/* Backend-spezifischer Widget-Name (Accessibility-Identifier oder
 * Klassenname als Fallback). */
static MooValue cocoa_widget_name(id obj) {
    if (!obj) return moo_none();
    if ([obj respondsToSelector:@selector(accessibilityIdentifier)]) {
        NSString *aid = [(NSView *)obj accessibilityIdentifier];
        if (aid && [aid length] > 0) {
            return moo_string_new([aid UTF8String]);
        }
    }
    NSString *cls = NSStringFromClass([obj class]);
    if (!cls) return moo_none();
    return moo_string_new([cls UTF8String]);
}

/* Liefert das Frame eines Widgets im Eltern-Koordinatensystem.
 * Fuer NSWindow wird frame() in Screen-Coords zurueckgegeben (top-left
 * umgerechnet ueber die main-Screen-Hoehe, analog zu moo_ui_fenster_position).
 * Fuer NSView wird [view frame] benutzt — da alle Container MooFlippedViews
 * sind, stimmt das mit der GTK/Win32-Semantik (top-left) ueberein. */
static void cocoa_widget_bounds(id obj, int *x, int *y, int *b, int *h) {
    *x = 0; *y = 0; *b = 0; *h = 0;
    if (!obj) return;
    if ([obj isKindOfClass:[NSWindow class]]) {
        NSRect f = [(NSWindow *)obj frame];
        CGFloat sh = [[NSScreen mainScreen] frame].size.height;
        *x = (int)f.origin.x;
        *y = (int)(sh - f.origin.y - f.size.height);  /* top-left flip */
        *b = (int)f.size.width;
        *h = (int)f.size.height;
        return;
    }
    if ([obj isKindOfClass:[NSView class]]) {
        NSRect f = [(NSView *)obj frame];
        *x = (int)f.origin.x;
        *y = (int)f.origin.y;
        *b = (int)f.size.width;
        *h = (int)f.size.height;
        return;
    }
}

/* Sichtbarkeit / Aktiv-Status. */
static BOOL cocoa_widget_sichtbar(id obj) {
    if (!obj) return NO;
    if ([obj isKindOfClass:[NSWindow class]]) return [(NSWindow *)obj isVisible];
    if ([obj isKindOfClass:[NSView class]])   return ![(NSView *)obj isHidden];
    return YES;
}
static BOOL cocoa_widget_aktiv(id obj) {
    if (!obj) return NO;
    if ([obj isKindOfClass:[NSControl class]]) return [(NSControl *)obj isEnabled];
    /* Non-Control-Views (NSBox/NSTabView/Canvas/Window) sind immer "aktiv". */
    return YES;
}

/* Liest die ID aus objc_getAssociatedObject oder liefert MOO_NONE. */
static MooValue cocoa_widget_id_value(id obj) {
    if (!obj) return moo_none();
    NSString *id_ns = objc_getAssociatedObject(obj, kMooUIIdKey);
    if (!id_ns || ![id_ns isKindOfClass:[NSString class]] || [id_ns length] == 0) {
        return moo_none();
    }
    return moo_string_new([id_ns UTF8String]);
}

/* Baut ein Info-Dict fuer ein Widget (ohne tiefe/eltern). */
static MooValue cocoa_build_info_dict(id obj) {
    MooValue d = moo_dict_new();
    if (!obj) return d;

    int x=0, y=0, b=0, h=0;
    cocoa_widget_bounds(obj, &x, &y, &b, &h);

    moo_dict_set(d, moo_string_new("typ"),      moo_string_new(cocoa_widget_typ(obj)));
    moo_dict_set(d, moo_string_new("x"),        moo_number((double)x));
    moo_dict_set(d, moo_string_new("y"),        moo_number((double)y));
    moo_dict_set(d, moo_string_new("b"),        moo_number((double)b));
    moo_dict_set(d, moo_string_new("h"),        moo_number((double)h));
    moo_dict_set(d, moo_string_new("sichtbar"), moo_bool(cocoa_widget_sichtbar(obj)));
    moo_dict_set(d, moo_string_new("aktiv"),    moo_bool(cocoa_widget_aktiv(obj)));
    moo_dict_set(d, moo_string_new("id"),       cocoa_widget_id_value(obj));
    moo_dict_set(d, moo_string_new("text"),     cocoa_widget_text(obj));
    moo_dict_set(d, moo_string_new("name"),     cocoa_widget_name(obj));
    return d;
}

/* moo_ui_widget_id_setze (Header zeilen 510–528) */
MooValue moo_ui_widget_id_setze(MooValue widget, MooValue id_string) {
    @autoreleasepool {
        id obj = unwrap_objc(widget);
        if (!obj) return moo_bool(0);
        /* NONE oder leer → ID loeschen. */
        if (id_string.tag != MOO_STRING) {
            objc_setAssociatedObject(obj, kMooUIIdKey, nil,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            return moo_bool(1);
        }
        const char *s = MV_STR(id_string)->chars;
        if (!s || s[0] == '\0') {
            objc_setAssociatedObject(obj, kMooUIIdKey, nil,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            return moo_bool(1);
        }
        NSString *ns = [NSString stringWithUTF8String:s];
        objc_setAssociatedObject(obj, kMooUIIdKey, ns,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return moo_bool(1);
    }
}

/* moo_ui_widget_id_hole (Header zeilen 530–540) */
MooValue moo_ui_widget_id_hole(MooValue widget) {
    @autoreleasepool {
        id obj = unwrap_objc(widget);
        if (!obj) return moo_none();
        return cocoa_widget_id_value(obj);
    }
}

/* moo_ui_widget_info (Header zeilen 542–572) */
MooValue moo_ui_widget_info(MooValue widget) {
    @autoreleasepool {
        id obj = unwrap_objc(widget);
        if (!obj) return moo_none();
        return cocoa_build_info_dict(obj);
    }
}

/* Rekursiver Baum-Walk: befuellt `list` pre-order.
 * - depth: aktuelle Tiefe (0 = Fenster-Wurzel).
 * - parent_id_ns: ID des unmittelbaren Eltern-Widgets (oder nil → MOO_NONE).
 * - obj: NSWindow oder NSView. */
static void cocoa_baum_walk(id obj, int depth, NSString *parent_id_ns,
                            MooValue list) {
    if (!obj) return;

    MooValue d = cocoa_build_info_dict(obj);
    moo_dict_set(d, moo_string_new("tiefe"), moo_number((double)depth));
    if (parent_id_ns && [parent_id_ns length] > 0) {
        moo_dict_set(d, moo_string_new("eltern"),
                     moo_string_new([parent_id_ns UTF8String]));
    } else {
        moo_dict_set(d, moo_string_new("eltern"), moo_none());
    }
    moo_list_append(list, d);

    /* Eigene ID als parent_id fuer Kinder (oder nil, wenn keine gesetzt). */
    NSString *own_id = objc_getAssociatedObject(obj, kMooUIIdKey);
    if (!own_id || ![own_id isKindOfClass:[NSString class]] || [own_id length] == 0) {
        own_id = nil;
    }

    /* Kinder ermitteln: NSWindow → contentView-Subviews; NSView → subviews;
     * NSBox → contentView-Subviews; NSTabView → alle Page-Views;
     * NSScrollView → documentView-Subviews. */
    NSArray<NSView *> *children = nil;
    if ([obj isKindOfClass:[NSWindow class]]) {
        NSView *cv = [(NSWindow *)obj contentView];
        if (cv) children = [cv subviews];
    } else if ([obj isKindOfClass:[NSBox class]]) {
        id cv = [(NSBox *)obj contentView];
        if ([cv isKindOfClass:[NSView class]]) children = [(NSView *)cv subviews];
    } else if ([obj isKindOfClass:[NSTabView class]]) {
        NSMutableArray *pages = [NSMutableArray array];
        for (NSTabViewItem *ti in [(NSTabView *)obj tabViewItems]) {
            NSView *pv = [ti view];
            if (pv) [pages addObject:pv];
        }
        children = pages;
    } else if ([obj isKindOfClass:[NSScrollView class]]) {
        NSView *dv = [(NSScrollView *)obj documentView];
        if (dv) children = [dv subviews];
    } else if ([obj isKindOfClass:[NSView class]]) {
        children = [(NSView *)obj subviews];
    }

    for (NSView *child in children) {
        cocoa_baum_walk(child, depth + 1, own_id, list);
    }
}

/* moo_ui_widget_baum (Header zeilen 574–593) */
MooValue moo_ui_widget_baum(MooValue fenster) {
    @autoreleasepool {
        id obj = unwrap_objc(fenster);
        if (!obj) return moo_none();
        /* Auch Views als Wurzel zulassen — Header sagt "Fenster", aber fuer
         * Test-Tooling ist Sub-Tree-Baum nuetzlich. */
        if (![obj isKindOfClass:[NSWindow class]] && ![obj isKindOfClass:[NSView class]]) {
            return moo_none();
        }
        MooValue list = moo_list_new(0);
        cocoa_baum_walk(obj, 0, nil, list);
        return list;
    }
}

/* Rekursive Pre-Order-Suche nach ID. Gibt das Handle als MOO_NUMBER oder
 * MOO_NONE zurueck. `target` ist bereits validiert (nicht-leerer NSString). */
static id cocoa_suche_walk(id obj, NSString *target) {
    if (!obj) return nil;
    NSString *own = objc_getAssociatedObject(obj, kMooUIIdKey);
    if (own && [own isKindOfClass:[NSString class]] && [own isEqualToString:target]) {
        return obj;
    }
    NSArray<NSView *> *children = nil;
    if ([obj isKindOfClass:[NSWindow class]]) {
        NSView *cv = [(NSWindow *)obj contentView];
        if (cv) children = [cv subviews];
    } else if ([obj isKindOfClass:[NSBox class]]) {
        id cv = [(NSBox *)obj contentView];
        if ([cv isKindOfClass:[NSView class]]) children = [(NSView *)cv subviews];
    } else if ([obj isKindOfClass:[NSTabView class]]) {
        NSMutableArray *pages = [NSMutableArray array];
        for (NSTabViewItem *ti in [(NSTabView *)obj tabViewItems]) {
            NSView *pv = [ti view];
            if (pv) [pages addObject:pv];
        }
        children = pages;
    } else if ([obj isKindOfClass:[NSScrollView class]]) {
        NSView *dv = [(NSScrollView *)obj documentView];
        if (dv) children = [dv subviews];
    } else if ([obj isKindOfClass:[NSView class]]) {
        children = [(NSView *)obj subviews];
    }
    for (NSView *child in children) {
        id hit = cocoa_suche_walk(child, target);
        if (hit) return hit;
    }
    return nil;
}

/* moo_ui_widget_suche (Header zeilen 595–613) */
MooValue moo_ui_widget_suche(MooValue fenster, MooValue id_string) {
    @autoreleasepool {
        id obj = unwrap_objc(fenster);
        if (!obj) return moo_none();
        if (id_string.tag != MOO_STRING) return moo_none();
        const char *s = MV_STR(id_string)->chars;
        if (!s || s[0] == '\0') return moo_none();
        NSString *target = [NSString stringWithUTF8String:s];
        id hit = cocoa_suche_walk(obj, target);
        if (!hit) return moo_none();
        return wrap_objc(hit);
    }
}

/* =========================================================================
 * Snapshot-API (Plan-004 P2) — PNG-Screenshots von Fenstern/Widgets
 *
 * Backend-Mapping laut Header (moo_ui.h):
 *   NSBitmapImageRep* rep =
 *     [v bitmapImageRepForCachingDisplayInRect:r];
 *   [v cacheDisplayInRect:r toBitmapImageRep:rep];
 *   [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
 *      writeToFile:pfad atomically:YES].
 *
 * `bitmapImageRepForCachingDisplayInRect:` ist ab macOS 14 deprecated,
 * funktioniert aber weiterhin und ist fuer CI-Artefakte vollkommen
 * ausreichend — der Aufrufer bekommt eine logische Widget-Darstellung,
 * keinen Compositor-Output (das deckt sich mit der Header-Semantik).
 *
 * Main-Thread-Pflicht: Alle UI-APIs duerfen nur vom Haupt-Thread
 * aufgerufen werden (siehe Datei-Header-Kommentar). Wir pruefen das
 * defensiv und liefern falsch zurueck statt zu crashen.
 * ========================================================================= */

/* Rendert `view` im Rechteck `bounds` in ein NSBitmapImageRep und schreibt
 * das Ergebnis als PNG nach `path_ns`. Liefert 1 bei Erfolg, 0 sonst. */
static int cocoa_snapshot_view_to_png(NSView *view, NSRect bounds,
                                      NSString *path_ns) {
    if (!view || !path_ns || [path_ns length] == 0) return 0;
    if (bounds.size.width <= 0 || bounds.size.height <= 0) return 0;

    NSBitmapImageRep *rep =
        [view bitmapImageRepForCachingDisplayInRect:bounds];
    if (!rep) return 0;
    [view cacheDisplayInRect:bounds toBitmapImageRep:rep];

    NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG
                                    properties:@{}];
    if (!png) return 0;

    NSError *err = nil;
    BOOL ok = [png writeToFile:path_ns
                       options:NSDataWritingAtomic
                         error:&err];
    if (!ok) {
        if (g_ui_debug_on) {
            NSLog(@"[moo-ui] snapshot writeToFile failed: %@", err);
        }
        return 0;
    }
    return 1;
}

/* moo_ui_test_snapshot (Header zeilen 640–678)
 *
 * Erwartet ein Fenster-Handle. Snapshot umfasst den kompletten
 * contentView (das ist der flipped Root-Container, den wir bei
 * moo_ui_fenster_erstelle installieren). */
MooValue moo_ui_test_snapshot(MooValue fenster, MooValue pfad) {
    @autoreleasepool {
        if (![NSThread isMainThread]) return moo_bool(0);
        if (pfad.tag != MOO_STRING) return moo_bool(0);
        const char *s = MV_STR(pfad)->chars;
        if (!s || s[0] == '\0') return moo_bool(0);

        id obj = unwrap_objc(fenster);
        if (!obj || ![obj isKindOfClass:[NSWindow class]]) return moo_bool(0);
        NSWindow *win = (NSWindow *)obj;
        if (![win isVisible]) return moo_bool(0);

        NSView *content = [win contentView];
        if (!content) return moo_bool(0);

        NSString *path_ns = [NSString stringWithUTF8String:s];
        int ok = cocoa_snapshot_view_to_png(content, [content bounds], path_ns);
        return moo_bool(ok);
    }
}

/* moo_ui_test_snapshot_widget (Header zeilen 680–714)
 *
 * Akzeptiert ein beliebiges Widget-Handle:
 *   - NSWindow    → identisch zu moo_ui_test_snapshot (contentView).
 *   - NSView      → das View-Rechteck.
 * Andere ObjC-Objekte (NSMenu, NSStatusItem, ...) werden abgelehnt. */
MooValue moo_ui_test_snapshot_widget(MooValue widget, MooValue pfad) {
    @autoreleasepool {
        if (![NSThread isMainThread]) return moo_bool(0);
        if (pfad.tag != MOO_STRING) return moo_bool(0);
        const char *s = MV_STR(pfad)->chars;
        if (!s || s[0] == '\0') return moo_bool(0);

        id obj = unwrap_objc(widget);
        if (!obj) return moo_bool(0);

        NSView *view = nil;
        if ([obj isKindOfClass:[NSWindow class]]) {
            NSWindow *win = (NSWindow *)obj;
            if (![win isVisible]) return moo_bool(0);
            view = [win contentView];
        } else if ([obj isKindOfClass:[NSView class]]) {
            view = (NSView *)obj;
            /* Sichtbarkeit: ein NSView gilt als "sichtbar", wenn es ein
             * Fenster hat, nicht hidden ist und das Fenster selbst visible
             * ist. Das entspricht der Header-Anforderung "realisiert und
             * sichtbar". */
            if ([view isHiddenOrHasHiddenAncestor]) return moo_bool(0);
            NSWindow *w = [view window];
            if (!w || ![w isVisible]) return moo_bool(0);
        } else {
            return moo_bool(0);
        }
        if (!view) return moo_bool(0);

        NSString *path_ns = [NSString stringWithUTF8String:s];
        int ok = cocoa_snapshot_view_to_png(view, [view bounds], path_ns);
        return moo_bool(ok);
    }
}

/* ==========================================================================
 * Plan-004 P3 — Test-/Debug-API: Automation
 * --------------------------------------------------------------------------
 * Siehe moo_ui.h Block "Test-/Debug-API: Automation" (Zeilen 716–945).
 *
 * Callback-Semantik: SYNCHRON. Backend-seitige Event-Queue wird NICHT
 * genutzt — wir rufen die registrierten Target/Action- bzw. MooCallbackTarget-
 * Handler direkt auf, damit Tests fokus- und timing-unabhaengig sind.
 * Sichtbarkeit/Aktivitaet wird vor jeder Aktion geprueft.
 * ========================================================================== */

/* Hilfs: pre-order-Suche nach dem tiefsten sichtbaren NSView, das einen
 * flipped-lokalen Punkt (Origin oben-links) enthaelt. Liefert nil wenn
 * der Punkt in keiner sichtbaren Subview liegt.
 *
 * Warum nicht -[NSView hitTest:]? hitTest erwartet den Punkt im *superview*-
 * Koord-System und interagiert mit dem Responder-/Drag-System (deaktivierte
 * Views, userInteractionEnabled). Fuer deterministische Tests wollen wir
 * die reine Geometrie inkl. disabled-Widgets (abgelehnt wird erst in
 * moo_ui_test_klick via isEnabled). */
static NSView *cocoa_hit_view(NSView *root, NSPoint p) {
    if (!root) return nil;
    if ([root isHidden]) return nil;
    NSRect b = [root bounds];
    if (!NSPointInRect(p, b)) return nil;
    /* Kinder in Reverse-Order (zuletzt gezeichnet = oben). */
    NSArray<NSView *> *subs = [root subviews];
    for (NSInteger i = (NSInteger)subs.count - 1; i >= 0; i--) {
        NSView *child = subs[i];
        NSPoint cp = [root convertPoint:p toView:child];
        NSView *hit = cocoa_hit_view(child, cp);
        if (hit) return hit;
    }
    return root;
}

/* Sichtbarkeits-Check fuer eine NSView: View hat ein Fenster, ist nicht
 * hidden (auch kein hidden ancestor), Fenster selbst ist visible. */
static BOOL cocoa_view_is_visible(NSView *view) {
    if (!view) return NO;
    if ([view isHiddenOrHasHiddenAncestor]) return NO;
    NSWindow *w = [view window];
    if (!w || ![w isVisible]) return NO;
    return YES;
}

/* Aktivitaets-Check: NSControl hat isEnabled, NSMenuItem hat isEnabled,
 * andere Views gelten als aktiv. */
static BOOL cocoa_widget_is_enabled(id obj) {
    if (!obj) return NO;
    if ([obj isKindOfClass:[NSControl class]]) {
        return [(NSControl *)obj isEnabled];
    }
    if ([obj isKindOfClass:[NSMenuItem class]]) {
        return [(NSMenuItem *)obj isEnabled];
    }
    return YES;
}

/* moo_ui_test_klick (Header zeilen 747–772)
 *
 * widget kann sein:
 *   - NSControl (NSButton/NSPopUpButton/NSSlider/...) → [performClick:nil]
 *     triggert target/action SYNCHRON im Main-Thread.
 *   - NSMenuItem → [performActionForItemAtIndex:] via enclosingMenu, sonst
 *     direkt target/action invoken.
 *   - MooCanvasView → synthetisches NSEvent mouseDown + [NSWindow sendEvent:]
 *     feuert unseren on_maus-Callback ueber -[MooCanvasView mouseDown:].
 */
MooValue moo_ui_test_klick(MooValue widget) {
    @autoreleasepool {
        if (![NSThread isMainThread]) return moo_bool(0);
        id obj = unwrap_objc(widget);
        if (!obj) return moo_bool(0);

        /* MooCanvasView vor NSControl pruefen: Canvas ist NSView-Subklasse. */
        if ([obj isKindOfClass:[MooCanvasView class]]) {
            MooCanvasView *cv = (MooCanvasView *)obj;
            if (!cocoa_view_is_visible(cv)) return moo_bool(0);
            NSWindow *win = [cv window];
            if (!win) return moo_bool(0);

            /* Klick in die Mitte der Leinwand. */
            NSRect b = [cv bounds];
            NSPoint center_local = NSMakePoint(b.origin.x + b.size.width  / 2.0,
                                               b.origin.y + b.size.height / 2.0);
            /* locationInWindow = nicht-flipped (bottom-left). Konvertieren. */
            NSPoint in_win = [cv convertPoint:center_local toView:nil];

            NSTimeInterval ts = [[NSProcessInfo processInfo] systemUptime];
            NSEvent *down = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                               location:in_win
                                          modifierFlags:0
                                              timestamp:ts
                                           windowNumber:[win windowNumber]
                                                context:nil
                                            eventNumber:0
                                             clickCount:1
                                               pressure:1.0];
            if (!down) return moo_bool(0);
            [win sendEvent:down];

            NSEvent *up = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
                                             location:in_win
                                        modifierFlags:0
                                            timestamp:ts
                                         windowNumber:[win windowNumber]
                                              context:nil
                                          eventNumber:0
                                           clickCount:1
                                             pressure:0.0];
            if (up) [win sendEvent:up];
            return moo_bool(1);
        }

        if ([obj isKindOfClass:[NSControl class]]) {
            NSControl *ctl = (NSControl *)obj;
            if (!cocoa_view_is_visible(ctl)) return moo_bool(0);
            if (![ctl isEnabled]) return moo_bool(0);
            [ctl performClick:nil];
            return moo_bool(1);
        }

        if ([obj isKindOfClass:[NSMenuItem class]]) {
            NSMenuItem *mi = (NSMenuItem *)obj;
            if (![mi isEnabled]) return moo_bool(0);
            NSMenu *menu = [mi menu];
            if (menu) {
                NSInteger idx = [menu indexOfItem:mi];
                if (idx >= 0) {
                    [menu performActionForItemAtIndex:idx];
                    return moo_bool(1);
                }
            }
            /* Fallback: target/action manuell invoken. */
            SEL act = [mi action];
            id tgt = [mi target];
            if (act && tgt && [tgt respondsToSelector:act]) {
                #pragma clang diagnostic push
                #pragma clang diagnostic ignored "-Warc-performSelector-leaks"
                [tgt performSelector:act withObject:mi];
                #pragma clang diagnostic pop
                return moo_bool(1);
            }
            return moo_bool(0);
        }

        return moo_bool(0);
    }
}

/* moo_ui_test_klick_xy (Header zeilen 774–806)
 *
 * Koordinaten sind Fenster-Content-lokal (flipped top-left, konsistent mit
 * moo_ui_widget_info). Wir laufen rekursiv durch den subview-Baum des
 * contentView und suchen das tiefste sichtbare Widget unter (x,y). Bei
 * MooCanvasView wird fireMouse:taste: direkt mit einem synthetischen
 * NSEvent gerufen, damit der on_maus-Callback lokale Koordinaten erhaelt.
 * Andere Treffer werden an moo_ui_test_klick delegiert. */
MooValue moo_ui_test_klick_xy(MooValue fenster, MooValue x, MooValue y) {
    @autoreleasepool {
        if (![NSThread isMainThread]) return moo_bool(0);
        id obj = unwrap_objc(fenster);
        if (!obj || ![obj isKindOfClass:[NSWindow class]]) return moo_bool(0);
        NSWindow *win = (NSWindow *)obj;
        if (![win isVisible]) return moo_bool(0);
        NSView *content = [win contentView];
        if (!content) return moo_bool(0);

        NSPoint p = NSMakePoint((CGFloat)num_or(x, -1), (CGFloat)num_or(y, -1));
        if (!NSPointInRect(p, [content bounds])) return moo_bool(0);

        NSView *hit = cocoa_hit_view(content, p);
        if (!hit) return moo_bool(0);

        if ([hit isKindOfClass:[MooCanvasView class]]) {
            MooCanvasView *cv = (MooCanvasView *)hit;
            if (!cocoa_view_is_visible(cv)) return moo_bool(0);
            /* Lokale Koords zu window-Koordinaten (nicht-flipped) umrechnen
             * und synthetisches mouseDown/mouseUp ueber sendEvent: zum
             * Fenster schicken — AppKit dispatcht an cv.mouseDown: und
             * damit an unseren on_maus-Callback. */
            NSPoint lp = [content convertPoint:p toView:cv];
            NSPoint in_win = [cv convertPoint:lp toView:nil];
            NSTimeInterval ts = [[NSProcessInfo processInfo] systemUptime];
            NSEvent *down = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                               location:in_win
                                          modifierFlags:0
                                              timestamp:ts
                                           windowNumber:[win windowNumber]
                                                context:nil
                                            eventNumber:0
                                             clickCount:1
                                               pressure:1.0];
            if (!down) return moo_bool(0);
            [win sendEvent:down];
            NSEvent *up = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
                                             location:in_win
                                        modifierFlags:0
                                            timestamp:ts
                                         windowNumber:[win windowNumber]
                                              context:nil
                                          eventNumber:0
                                           clickCount:1
                                             pressure:0.0];
            if (up) [win sendEvent:up];
            return moo_bool(1);
        }

        /* Nicht-Canvas → wie test_klick. */
        return moo_ui_test_klick(wrap_objc(hit));
    }
}

/* moo_ui_test_text_setze (Header zeilen 808–846)
 *
 * Zielwidgets:
 *   - NSTextField (inkl. NSSecureTextField): setStringValue +
 *     NSControlTextDidChangeNotification (AppKit feuert die Notification
 *     bei setStringValue: NICHT automatisch).
 *   - NSTextView: textStorage-Replacement + didChangeText, sowie
 *     textDidChange: am Delegate.
 *   - NSPopUpButton: Eintrag mit exakt passendem Titel suchen,
 *     selectItemWithTitle:, dann sendAction:to:.
 */
MooValue moo_ui_test_text_setze(MooValue widget, MooValue text) {
    @autoreleasepool {
        if (![NSThread isMainThread]) return moo_bool(0);
        if (text.tag != MOO_STRING) return moo_bool(0);
        const char *cs = MV_STR(text)->chars;
        if (!cs) cs = "";
        NSString *ns = [NSString stringWithUTF8String:cs];
        if (!ns) return moo_bool(0);

        id obj = unwrap_objc(widget);
        if (!obj) return moo_bool(0);

        /* NSPopUpButton ist eine NSButton-Subklasse, Reihenfolge wichtig. */
        if ([obj isKindOfClass:[NSPopUpButton class]]) {
            NSPopUpButton *pop = (NSPopUpButton *)obj;
            if (!cocoa_view_is_visible(pop)) return moo_bool(0);
            if (![pop isEnabled]) return moo_bool(0);
            if ([pop indexOfItemWithTitle:ns] < 0) return moo_bool(0);
            [pop selectItemWithTitle:ns];
            /* selectItemWithTitle feuert action NICHT automatisch. */
            SEL act = [pop action];
            id tgt = [pop target];
            if (act) [pop sendAction:act to:tgt];
            return moo_bool(1);
        }

        if ([obj isKindOfClass:[NSTextField class]]) {
            NSTextField *tf = (NSTextField *)obj;
            if (!cocoa_view_is_visible(tf)) return moo_bool(0);
            if (![tf isEnabled]) return moo_bool(0);
            [tf setStringValue:ns];
            /* on_change-Bridge (siehe moo_ui_eingabe_on_change) lauscht auf
             * NSControlTextDidChangeNotification — explizit posten, weil
             * setStringValue: sie nicht feuert. */
            [[NSNotificationCenter defaultCenter]
                postNotificationName:NSControlTextDidChangeNotification
                              object:tf];
            return moo_bool(1);
        }

        if ([obj isKindOfClass:[NSTextView class]]) {
            NSTextView *tv = (NSTextView *)obj;
            if (!cocoa_view_is_visible(tv)) return moo_bool(0);
            if (![tv isEditable]) return moo_bool(0);
            NSTextStorage *ts = [tv textStorage];
            NSRange all = NSMakeRange(0, [ts length]);
            [ts beginEditing];
            [ts replaceCharactersInRange:all withString:ns];
            [ts endEditing];
            /* didChangeText feuert textDidChange:-Delegate synchron. */
            [tv didChangeText];
            return moo_bool(1);
        }

        return moo_bool(0);
    }
}

/* moo_ui_test_shortcut (Header zeilen 848–879)
 *
 * Nutzt dieselbe Parser- und Lookup-Tabelle wie moo_ui_shortcut_bind
 * (cocoa_parse_sequence + window_shortcuts). Ruft den MooCallbackTarget
 * DIREKT auf, ohne synthetisches NSEvent — damit ist die Ausloesung
 * deterministisch und fokus-unabhaengig.
 */
MooValue moo_ui_test_shortcut(MooValue fenster, MooValue sequenz) {
    @autoreleasepool {
        if (![NSThread isMainThread]) return moo_bool(0);
        id obj = unwrap_objc(fenster);
        if (!obj || ![obj isKindOfClass:[NSWindow class]]) return moo_bool(0);
        NSWindow *win = (NSWindow *)obj;
        if (![win isVisible]) return moo_bool(0);
        if (sequenz.tag != MOO_STRING) return moo_bool(0);

        NSEventModifierFlags mods = 0;
        NSString *keyChar = @"";
        int keyCode = -1;
        if (!cocoa_parse_sequence(MV_STR(sequenz)->chars,
                                  &mods, &keyChar, &keyCode)) {
            return moo_bool(0);
        }

        MooShortcutList *list = window_shortcuts(win, NO);
        if (!list || list.entries.count == 0) return moo_bool(0);

        for (NSDictionary *ent in [list.entries copy]) {
            NSEventModifierFlags em2 = (NSEventModifierFlags)
                [[ent objectForKey:@"mods"] unsignedIntegerValue];
            NSString *kChar = [ent objectForKey:@"keyChar"];
            int      kCode = [[ent objectForKey:@"keyCode"] intValue];
            if (em2 != mods) continue;
            BOOL match = NO;
            if (keyChar.length > 0 && kChar.length > 0) {
                match = [kChar isEqualToString:keyChar];
            } else if (keyCode >= 0 && kCode >= 0) {
                match = (kCode == keyCode);
            }
            if (match) {
                MooCallbackTarget *tgt = [ent objectForKey:@"target"];
                if (tgt) {
                    [tgt fire:nil];
                    return moo_bool(1);
                }
            }
        }
        return moo_bool(0);
    }
}

/* moo_ui_test_warte (Header zeilen 881–910)
 *
 * Pumpt den NSRunLoop bis die Wartezeit abgelaufen ist. Wir laufen in
 * kurzen Slices (30 ms), damit moo_ui_beenden greifen kann — NSApp.isRunning
 * wird NACH [NSApp stop:] false, also brechen wir die Schleife in diesem
 * Fall ab. Werte <= 0 pumpen einmal und kehren zurueck. */
MooValue moo_ui_test_warte(MooValue millisekunden) {
    @autoreleasepool {
        if (![NSThread isMainThread]) return moo_bool(0);
        long ms = (long)num_or(millisekunden, 0);
        if (ms <= 0) {
            /* Einmaliges Pump-and-Return. */
            moo_ui_pump();
            return moo_bool(1);
        }

        NSDate *ende = [NSDate dateWithTimeIntervalSinceNow:(ms / 1000.0)];
        NSRunLoop *rl = [NSRunLoop currentRunLoop];
        while ([ende timeIntervalSinceNow] > 0.0) {
            /* Abbruch-Signal beachten: moo_ui_beenden stoppt NSApp. */
            if (![NSApp isRunning] && g_open_windows == 0) break;
            NSDate *slice = [NSDate dateWithTimeIntervalSinceNow:0.030];
            if ([slice compare:ende] == NSOrderedDescending) slice = ende;
            [rl runUntilDate:slice];
            /* Zusaetzlich die AppKit-Queue drainen, damit non-timer-Events
             * (mouse/key/notifications) direkt dispatcht werden. */
            NSEvent *ev;
            while ((ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate distantPast]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES]) != nil) {
                [NSApp sendEvent:ev];
            }
        }
        return moo_bool(1);
    }
}

/* moo_ui_test_pump (Header zeilen 912–945)
 *
 * Zweit-Exponierung von moo_ui_pump unter dem ui_test_*-Namensraum.
 * Siehe moo_ui_pump (Zeilen 389–401): dequeue alle wartenden Events in
 * NSDefaultRunLoopMode und dispatchen, danach sofort zurueck. */
MooValue moo_ui_test_pump(void) {
    @autoreleasepool {
        if (![NSThread isMainThread]) return moo_bool(0);
        ensure_cocoa();
        NSEvent *ev;
        while ((ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:[NSDate distantPast]
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES]) != nil) {
            [NSApp sendEvent:ev];
        }
        return moo_bool(1);
    }
}

/* ------------------------------------------------------------------ *
 * Tray-Bedarf: Resize / Enter / Key / Scroll-Hooks (Stubs Phase 1)
 * ------------------------------------------------------------------ */

MooValue moo_ui_fenster_on_resize(MooValue fenster, MooValue callback) {
    (void)fenster; (void)callback;
    return moo_none();
}

MooValue moo_ui_eingabe_on_enter(MooValue eingabe, MooValue callback) {
    (void)eingabe; (void)callback;
    return moo_none();
}

MooValue moo_ui_textbereich_on_key(MooValue tb, MooValue callback) {
    (void)tb; (void)callback;
    return moo_none();
}

MooValue moo_ui_liste_on_scroll(MooValue liste, MooValue callback) {
    (void)liste; (void)callback;
    return moo_none();
}

MooValue moo_ui_liste_scroll_zu(MooValue liste, MooValue index) {
    (void)liste; (void)index;
    return moo_none();
}

MooValue moo_ui_liste_scroll_unten(MooValue liste) {
    (void)liste;
    return moo_none();
}

MooValue moo_ui_textbereich_zeile_anzahl(MooValue tb) {
    (void)tb;
    return moo_number(0.0);
}
