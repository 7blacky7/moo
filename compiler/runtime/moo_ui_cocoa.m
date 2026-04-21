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
static const void *kMooTableDSKey        = &kMooTableDSKey;
static const void *kMooTVKey             = &kMooTVKey;
static const void *kMooScrollViewKey     = &kMooScrollViewKey;
static const void *kMooNColsKey          = &kMooNColsKey;
static const void *kMooRadioGroupKey     = &kMooRadioGroupKey;
static const void *kMooWindowDelegateKey = &kMooWindowDelegateKey;

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

/* Leinwand: NSView-Subklasse mit drawRect → moo-callback. */
@interface MooCanvasView : MooFlippedView
@property (nonatomic, strong) MooCallbackTarget *drawTarget;
@end
@implementation MooCanvasView
- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];
    if (self.drawTarget) [self.drawTarget fireWithHandle:self];
}
@end

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
        else if ([w isKindOfClass:[NSCell class]]) [(NSCell *)w setToolTip:s];
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
