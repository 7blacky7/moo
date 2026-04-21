/*
 * moo_ui_win32.c — Windows-Backend fuer moo_ui.h via Win32-API.
 * =============================================================
 *
 * Implementiert die OS-neutrale UI-API aus moo_ui.h mit rohen Win32-Controls
 * (user32 + comctl32 + comdlg32). API 1:1 zur GTK3-Implementierung in
 * moo_ui_gtk.c; nur die Implementierung unterscheidet sich.
 *
 * Architektur:
 *   - Widget-Handle: MooValue { tag=MOO_NUMBER, data=HWND } (wie bei GTK:
 *     gepackter Native-Pointer).
 *   - Jede Control-Instanz bekommt eine eindeutige numerische id (HMENU) im
 *     Bereich 1000..39999. Top-Window-WindowProc reagiert auf WM_COMMAND /
 *     WM_NOTIFY / WM_HSCROLL / WM_VSCROLL und dispatcht an die per SetPropW
 *     am Control hinterlegte MooValue*-Callback-Box.
 *   - "moo-fixed"-Pattern (GTK) → auf Win32 nutzen wir einen "MooContainer"-
 *     Child-Window-Class, die WM_COMMAND an GetParent() weiterreicht. Damit
 *     funktioniert resolve_container() gleichartig (Fenster → Root-Container;
 *     Rahmen/Tab-Page/Scroll → eigener MooContainer mit Child-Koordinaten).
 *   - Callback-Ownership: cb_box_new() retain-t und malloc-t; SetPropW setzt
 *     die Box ans HWND. Beim WM_NCDESTROY wird die Box released + freed.
 *     Alle Controls (und der MooContainer selbst) enden nach WM_NCDESTROY →
 *     cb_box_free wird immer genau einmal aufgerufen.
 *   - Strings: moo verwendet UTF-8; Win32 A-APIs interpretieren CP1252.
 *     Wir benutzen durchgehend W-Varianten (CreateWindowExW etc.) und
 *     konvertieren UTF-8 → UTF-16 via utf8_to_wide().
 *
 * Plan-Referenz: Memory `plan-002-moo-ui-cross-platform` (Phase 3).
 */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601   /* Windows 7+, wegen TaskDialog/Themes */
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#include "moo_runtime.h"
#include "moo_ui.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>

/* =========================================================================
 * Forward-Decls fuer interne Helfer
 * ========================================================================= */

static LRESULT CALLBACK moo_window_proc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK moo_container_proc(HWND, UINT, WPARAM, LPARAM);
static void cb_box_free_prop(HWND hwnd, LPCWSTR name);

/* =========================================================================
 * Globaler Zustand
 * ========================================================================= */

static int g_initialized = 0;
static int g_open_windows = 0;
static int g_ui_debug_on = 0;
static HINSTANCE g_hinstance = NULL;

/* eindeutige Control-IDs (Startwert fernab von Dialog-Default-IDs wie 1) */
static UINT g_next_ctrl_id = 1000;
static UINT g_next_menu_id = 40000;

/* Callback-Lookup-Tabelle per id (fuer WM_COMMAND-Dispatch).
 * Array-Groesse statisch gewaehlt (genug fuer ~60k Controls). */
#define MOO_CB_TABLE_SIZE 65536
static MooValue* g_cb_by_id[MOO_CB_TABLE_SIZE];

/* Fenster-Klasse-Namen */
#define MOO_WINDOW_CLASS     L"MooUIWindow"
#define MOO_CONTAINER_CLASS  L"MooUIContainer"

/* =========================================================================
 * Utility: Logging
 * ========================================================================= */

static void ui_log(const char* what, const char* arg) {
    if (!g_ui_debug_on) return;
    if (arg) fprintf(stderr, "[moo_ui_win32] %s: %s\n", what, arg);
    else     fprintf(stderr, "[moo_ui_win32] %s\n", what);
}

/* =========================================================================
 * UTF-8 ↔ UTF-16 Konvertierung
 * ========================================================================= */

/* Liefert neu alloc'ten WCHAR-Puffer; Caller ruft free(). NULL bei Fehler. */
static wchar_t* utf8_to_wide(const char* s) {
    if (!s) s = "";
    int need = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (need <= 0) return NULL;
    wchar_t* out = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)need);
    if (!out) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, need);
    return out;
}

/* Liefert neu alloc'ten char*-Puffer mit UTF-8 aus WCHAR. NULL bei Fehler. */
static char* wide_to_utf8(const wchar_t* w) {
    if (!w) w = L"";
    int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (need <= 0) return NULL;
    char* out = (char*)malloc((size_t)need);
    if (!out) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, need, NULL, NULL);
    return out;
}

/* =========================================================================
 * Callback-Ownership (analog GTK's cb_box_destroy)
 * ========================================================================= */

typedef struct {
    MooValue v;
} MooCbBox;

static MooCbBox* cb_box_new(MooValue callback) {
    MooCbBox* box = (MooCbBox*)malloc(sizeof(MooCbBox));
    if (!box) return NULL;
    box->v = callback;
    moo_retain(callback);
    return box;
}

static void cb_box_free(MooCbBox* box) {
    if (!box) return;
    moo_release(box->v);
    free(box);
}

/* =========================================================================
 * MooValue ↔ HWND Wrap
 * ========================================================================= */

static inline MooValue wrap_hwnd(HWND h) {
    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, (void*)h);
    return v;
}

static inline HWND unwrap_hwnd(MooValue v) {
    if (v.tag != MOO_NUMBER) return NULL;
    return (HWND)moo_val_as_ptr(v);
}

static inline const char* str_or(MooValue v, const char* fb) {
    return (v.tag == MOO_STRING && MV_STR(v)) ? MV_STR(v)->chars : fb;
}
static inline int num_or(MooValue v, int fb) {
    return (v.tag == MOO_NUMBER) ? (int)MV_NUM(v) : fb;
}
static inline int bool_or(MooValue v, int fb) {
    if (v.tag == MOO_BOOL) return MV_BOOL(v) ? 1 : 0;
    return fb;
}

/* =========================================================================
 * ensure_init — registriert Fenster-Klassen, INITCOMMONCONTROLSEX
 * ========================================================================= */

static void ensure_init(void) {
    if (g_initialized) return;
    g_initialized = 1;
    g_hinstance = GetModuleHandleW(NULL);

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_USEREX_CLASSES | ICC_LISTVIEW_CLASSES
              | ICC_TAB_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES
              | ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = moo_window_proc;
    wc.hInstance     = g_hinstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = MOO_WINDOW_CLASS;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExW(&wc);

    WNDCLASSEXW wcc;
    memset(&wcc, 0, sizeof(wcc));
    wcc.cbSize        = sizeof(wcc);
    wcc.style         = CS_HREDRAW | CS_VREDRAW;
    wcc.lpfnWndProc   = moo_container_proc;
    wcc.hInstance     = g_hinstance;
    wcc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcc.lpszClassName = MOO_CONTAINER_CLASS;
    RegisterClassExW(&wcc);
}

/* =========================================================================
 * Resolve-Container
 *
 * Kind-Widget mit (x,y,w,h) landet:
 *   - Wenn Parent ein Top-Level-Fenster: direkt in diesem Fenster.
 *     (Wir haben kein separates Fixed — Win32 erlaubt absolute Platzierung
 *      der Child-Controls direkt im Fenster-Content.)
 *   - Wenn Parent ein MooContainer (Rahmen/Tab-Page/Scroll-Inner): direkt.
 *   - Sonst: direkt im Parent (Unterstuetzung variabler Fallbacks).
 * Rueckgabe: HWND als Parent-Argument fuer CreateWindowExW.
 * ========================================================================= */

static HWND resolve_container(MooValue parent) {
    HWND p = unwrap_hwnd(parent);
    if (!p) return NULL;
    /* Bei Fenster: Wir nutzen das Fenster selbst als Container. Kinder werden
     * direkt an den Client-Bereich geheftet. */
    return p;
}

/* =========================================================================
 * Platzierung: CreateWindow uebernimmt x/y/w/h direkt.
 * ========================================================================= */

static UINT alloc_ctrl_id(void) {
    UINT id = g_next_ctrl_id++;
    if (g_next_ctrl_id >= 39999) g_next_ctrl_id = 1000;
    return id;
}

static void register_cb(UINT id, MooCbBox* box) {
    if (id < MOO_CB_TABLE_SIZE) {
        if (g_cb_by_id[id]) {
            /* Sollte nicht passieren, aber zur Sicherheit: alte Box freigeben. */
            MooCbBox* old = (MooCbBox*)g_cb_by_id[id];
            cb_box_free(old);
        }
        g_cb_by_id[id] = (MooValue*)box;
    }
}

static MooCbBox* lookup_cb(UINT id) {
    if (id < MOO_CB_TABLE_SIZE) return (MooCbBox*)g_cb_by_id[id];
    return NULL;
}

static void unregister_cb(UINT id) {
    if (id < MOO_CB_TABLE_SIZE) {
        MooCbBox* b = (MooCbBox*)g_cb_by_id[id];
        g_cb_by_id[id] = NULL;
        cb_box_free(b);
    }
}

/* =========================================================================
 * Container-WindowProc — reicht WM_COMMAND/WM_NOTIFY etc. an Parent weiter.
 * So funktioniert Frame/Tab-Page als Verschachtelung (Kinder-Buttons feuern
 * WM_COMMAND in ihrem direkten Parent-Container, der es weiterreicht an das
 * Fenster, welches die Callback-Box dispatcht). */

static LRESULT CALLBACK moo_container_proc(HWND hwnd, UINT msg,
                                           WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
        case WM_NOTIFY:
        case WM_HSCROLL:
        case WM_VSCROLL:
        case WM_DRAWITEM:
        case WM_MEASUREITEM:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORBTN: {
            HWND parent = GetParent(hwnd);
            if (parent) return SendMessageW(parent, msg, wp, lp);
            break;
        }
        case WM_ERASEBKGND: {
            /* Damit die Hintergrundfarbe sichtbar ist (fuer Frame/Tab-Page) */
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wp, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
            return 1;
        }
        case WM_NCDESTROY: {
            /* Alle Props freigeben */
            cb_box_free_prop(hwnd, L"moo-cb");
            cb_box_free_prop(hwnd, L"moo-change");
            cb_box_free_prop(hwnd, L"moo-draw");
            cb_box_free_prop(hwnd, L"moo-onclose");
            cb_box_free_prop(hwnd, L"moo-onsel");
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* =========================================================================
 * Haupt-Fenster-WindowProc
 * ========================================================================= */

static void cb_box_free_prop(HWND hwnd, LPCWSTR name) {
    MooCbBox* b = (MooCbBox*)GetPropW(hwnd, name);
    if (b) {
        RemovePropW(hwnd, name);
        cb_box_free(b);
    }
}

static LRESULT CALLBACK moo_window_proc(HWND hwnd, UINT msg,
                                        WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND: {
            UINT id = LOWORD(wp);
            UINT code = HIWORD(wp);
            HWND ctrl = (HWND)lp;

            /* Menue-Eintrag? (ctrl == NULL, code == 0) */
            if (ctrl == NULL && code == 0) {
                MooCbBox* cb = lookup_cb(id);
                if (cb && cb->v.tag == MOO_FUNC) {
                    MooValue rv = moo_func_call_0(cb->v);
                    moo_release(rv);
                }
                return 0;
            }

            /* Control: ggf. am Control selbst per SetProp hinterlegter
             * spezieller Handler (on_change fuer Edit = EN_CHANGE). */
            if (ctrl) {
                if (code == EN_CHANGE) {
                    MooCbBox* cb = (MooCbBox*)GetPropW(ctrl, L"moo-change");
                    if (cb && cb->v.tag == MOO_FUNC) {
                        MooValue rv = moo_func_call_0(cb->v);
                        moo_release(rv);
                    }
                    return 0;
                }
                if (code == CBN_SELCHANGE) {
                    /* Combobox selection change */
                    MooCbBox* cb = (MooCbBox*)GetPropW(ctrl, L"moo-cb");
                    if (cb && cb->v.tag == MOO_FUNC) {
                        MooValue rv = moo_func_call_0(cb->v);
                        moo_release(rv);
                    }
                    return 0;
                }
                /* Button-Click, Checkbox-Toggle, Radio-Toggle (code=BN_CLICKED)
                 * → generic callback am Control. */
                if (code == BN_CLICKED) {
                    MooCbBox* cb = (MooCbBox*)GetPropW(ctrl, L"moo-cb");
                    if (cb && cb->v.tag == MOO_FUNC) {
                        /* Bei Checkbox/Radio: Zustand automatisch togglen
                         * (BS_AUTOCHECKBOX/BS_AUTORADIOBUTTON macht das bereits). */
                        MooValue rv = moo_func_call_0(cb->v);
                        moo_release(rv);
                    }
                    return 0;
                }
            }
            return 0;
        }

        case WM_NOTIFY: {
            LPNMHDR nm = (LPNMHDR)lp;
            if (!nm) return 0;
            /* ListView-Selection / Tab-Select */
            if (nm->code == LVN_ITEMCHANGED || nm->code == TCN_SELCHANGE) {
                MooCbBox* cb = (MooCbBox*)GetPropW(nm->hwndFrom, L"moo-onsel");
                if (cb && cb->v.tag == MOO_FUNC) {
                    MooValue rv = moo_func_call_0(cb->v);
                    moo_release(rv);
                }
            }
            return 0;
        }

        case WM_HSCROLL:
        case WM_VSCROLL: {
            HWND ctrl = (HWND)lp;
            if (ctrl) {
                MooCbBox* cb = (MooCbBox*)GetPropW(ctrl, L"moo-cb");
                if (cb && cb->v.tag == MOO_FUNC) {
                    MooValue rv = moo_func_call_0(cb->v);
                    moo_release(rv);
                }
            }
            return 0;
        }

        case WM_DRAWITEM: {
            /* Fuer Leinwand (Owner-Draw STATIC): feuert on_draw. */
            LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)lp;
            if (di) {
                MooCbBox* cb = (MooCbBox*)GetPropW(di->hwndItem, L"moo-draw");
                if (cb && cb->v.tag == MOO_FUNC) {
                    MooValue handle = wrap_hwnd(di->hwndItem);
                    MooValue rv = moo_func_call_1(cb->v, handle);
                    moo_release(rv);
                }
            }
            return TRUE;
        }

        case WM_CLOSE: {
            MooCbBox* cb = (MooCbBox*)GetPropW(hwnd, L"moo-onclose");
            if (cb && cb->v.tag == MOO_FUNC) {
                MooValue rv = moo_func_call_0(cb->v);
                int allow = moo_is_truthy(rv);
                moo_release(rv);
                if (!allow) return 0;  /* abort close */
            }
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY: {
            if (g_open_windows > 0) g_open_windows--;
            if (g_open_windows == 0) PostQuitMessage(0);
            return 0;
        }

        case WM_NCDESTROY: {
            cb_box_free_prop(hwnd, L"moo-onclose");
            cb_box_free_prop(hwnd, L"moo-cb");
            break;
        }

        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wp, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
            return 1;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* =========================================================================
 * Initialisierung & Event-Loop
 * ========================================================================= */

MooValue moo_ui_init(void) {
    ensure_init();
    return moo_bool(1);
}

MooValue moo_ui_laufen(void) {
    ensure_init();
    ui_log("ui_laufen: enter message loop", NULL);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        /* IsDialogMessage waere fuer Tab-Navigation schoener, aber
         * erfordert ein Dialog-Template. Phase-3 ok ohne. */
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ui_log("ui_laufen: exit message loop", NULL);
    return moo_none();
}

MooValue moo_ui_beenden(void) {
    PostQuitMessage(0);
    return moo_none();
}

MooValue moo_ui_pump(void) {
    ensure_init();
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return moo_none();
}

MooValue moo_ui_debug(MooValue an) {
    g_ui_debug_on = bool_or(an, 0);
    return moo_bool(g_ui_debug_on);
}

/* =========================================================================
 * Fenster
 * ========================================================================= */

MooValue moo_ui_fenster(MooValue titel, MooValue breite, MooValue hoehe,
                        MooValue flags, MooValue parent) {
    ensure_init();
    const char* t = str_or(titel, "moo");
    int w = num_or(breite, 640);
    int h = num_or(hoehe, 480);
    unsigned f = (unsigned)num_or(flags, 0);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    DWORD exstyle = 0;
    if (f & MOO_UI_FLAG_RESIZABLE)   style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    if (f & MOO_UI_FLAG_NO_DECOR)    style = WS_POPUP;
    if (f & MOO_UI_FLAG_ALWAYS_TOP)  exstyle |= WS_EX_TOPMOST;

    HWND parent_hwnd = unwrap_hwnd(parent);
    wchar_t* wt = utf8_to_wide(t);

    HWND hwnd = CreateWindowExW(
        exstyle,
        MOO_WINDOW_CLASS,
        wt ? wt : L"moo",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        w, h,
        parent_hwnd,
        NULL,
        g_hinstance,
        NULL);
    free(wt);

    if (!hwnd) return moo_bool(0);

    if (f & MOO_UI_FLAG_MAXIMIZED)  ShowWindow(hwnd, SW_MAXIMIZE);
    if (f & MOO_UI_FLAG_FULLSCREEN) {
        SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowPos(hwnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOZORDER | SWP_FRAMECHANGED);
        }
    }
    if (f & MOO_UI_FLAG_MODAL && parent_hwnd) {
        EnableWindow(parent_hwnd, FALSE);
    }

    g_open_windows++;
    ui_log("ui_fenster", t);
    return wrap_hwnd(hwnd);
}

MooValue moo_ui_fenster_titel_setze(MooValue fenster, MooValue titel) {
    HWND h = unwrap_hwnd(fenster);
    if (!h) return moo_bool(0);
    wchar_t* wt = utf8_to_wide(str_or(titel, ""));
    BOOL ok = SetWindowTextW(h, wt ? wt : L"");
    free(wt);
    return moo_bool(ok ? 1 : 0);
}

MooValue moo_ui_fenster_icon_setze(MooValue fenster, MooValue pfad) {
    HWND h = unwrap_hwnd(fenster);
    if (!h) return moo_bool(0);
    wchar_t* wp = utf8_to_wide(str_or(pfad, ""));
    HICON ico = (HICON)LoadImageW(NULL, wp ? wp : L"",
                                  IMAGE_ICON, 0, 0,
                                  LR_LOADFROMFILE | LR_DEFAULTSIZE);
    free(wp);
    if (!ico) return moo_bool(0);
    SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)ico);
    SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)ico);
    return moo_bool(1);
}

MooValue moo_ui_fenster_groesse_setze(MooValue fenster, MooValue b, MooValue hh) {
    HWND h = unwrap_hwnd(fenster);
    if (!h) return moo_bool(0);
    SetWindowPos(h, NULL, 0, 0, num_or(b, 100), num_or(hh, 100),
                 SWP_NOMOVE | SWP_NOZORDER);
    return moo_bool(1);
}

MooValue moo_ui_fenster_position_setze(MooValue fenster, MooValue x, MooValue y) {
    HWND h = unwrap_hwnd(fenster);
    if (!h) return moo_bool(0);
    SetWindowPos(h, NULL, num_or(x, 0), num_or(y, 0), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER);
    return moo_bool(1);
}

MooValue moo_ui_fenster_schliessen(MooValue fenster) {
    HWND h = unwrap_hwnd(fenster);
    if (!h) return moo_bool(0);
    DestroyWindow(h);
    return moo_bool(1);
}

MooValue moo_ui_zeige(MooValue fenster) {
    HWND h = unwrap_hwnd(fenster);
    if (!h) return moo_bool(0);
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
    return moo_bool(1);
}

MooValue moo_ui_zeige_nebenbei(MooValue fenster) {
    return moo_ui_zeige(fenster);
}

MooValue moo_ui_fenster_on_close(MooValue fenster, MooValue callback) {
    HWND h = unwrap_hwnd(fenster);
    if (!h) return moo_bool(0);
    MooCbBox* box = cb_box_new(callback);
    cb_box_free_prop(h, L"moo-onclose");
    SetPropW(h, L"moo-onclose", (HANDLE)box);
    return moo_bool(1);
}

/* =========================================================================
 * Label + Knopf
 * ========================================================================= */

MooValue moo_ui_label(MooValue parent, MooValue text,
                      MooValue x, MooValue y, MooValue b, MooValue h) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    wchar_t* wt = utf8_to_wide(str_or(text, ""));
    UINT id = alloc_ctrl_id();
    HWND lbl = CreateWindowExW(0, L"STATIC", wt ? wt : L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        num_or(x, 0), num_or(y, 0), num_or(b, 100), num_or(h, 20),
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    free(wt);
    if (!lbl) return moo_bool(0);
    return wrap_hwnd(lbl);
}

MooValue moo_ui_label_setze(MooValue label, MooValue text) {
    HWND h = unwrap_hwnd(label);
    if (!h) return moo_bool(0);
    wchar_t* wt = utf8_to_wide(str_or(text, ""));
    BOOL ok = SetWindowTextW(h, wt ? wt : L"");
    free(wt);
    return moo_bool(ok ? 1 : 0);
}

MooValue moo_ui_label_text(MooValue label) {
    HWND h = unwrap_hwnd(label);
    if (!h) return moo_string_new("");
    int n = GetWindowTextLengthW(h);
    wchar_t* buf = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)(n + 1));
    if (!buf) return moo_string_new("");
    GetWindowTextW(h, buf, n + 1);
    char* u = wide_to_utf8(buf);
    free(buf);
    MooValue rv = moo_string_new(u ? u : "");
    free(u);
    return rv;
}

MooValue moo_ui_knopf(MooValue parent, MooValue text,
                      MooValue x, MooValue y, MooValue b, MooValue h,
                      MooValue callback) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    wchar_t* wt = utf8_to_wide(str_or(text, "OK"));
    UINT id = alloc_ctrl_id();
    HWND btn = CreateWindowExW(0, L"BUTTON", wt ? wt : L"OK",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        num_or(x, 0), num_or(y, 0), num_or(b, 80), num_or(h, 28),
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    free(wt);
    if (!btn) return moo_bool(0);
    if (callback.tag == MOO_FUNC) {
        MooCbBox* box = cb_box_new(callback);
        SetPropW(btn, L"moo-cb", (HANDLE)box);
    }
    return wrap_hwnd(btn);
}

/* =========================================================================
 * Checkbox, Radio, Eingabe, Textbereich
 * ========================================================================= */

MooValue moo_ui_checkbox(MooValue parent, MooValue text,
                         MooValue x, MooValue y, MooValue b, MooValue h,
                         MooValue initial, MooValue callback) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    wchar_t* wt = utf8_to_wide(str_or(text, ""));
    UINT id = alloc_ctrl_id();
    HWND cb = CreateWindowExW(0, L"BUTTON", wt ? wt : L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        num_or(x, 0), num_or(y, 0), num_or(b, 120), num_or(h, 22),
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    free(wt);
    if (!cb) return moo_bool(0);
    SendMessageW(cb, BM_SETCHECK, bool_or(initial, 0) ? BST_CHECKED : BST_UNCHECKED, 0);
    if (callback.tag == MOO_FUNC) {
        MooCbBox* box = cb_box_new(callback);
        SetPropW(cb, L"moo-cb", (HANDLE)box);
    }
    return wrap_hwnd(cb);
}

MooValue moo_ui_checkbox_wert(MooValue checkbox) {
    HWND h = unwrap_hwnd(checkbox);
    if (!h) return moo_bool(0);
    LRESULT st = SendMessageW(h, BM_GETCHECK, 0, 0);
    return moo_bool(st == BST_CHECKED ? 1 : 0);
}

MooValue moo_ui_checkbox_setze(MooValue checkbox, MooValue wert) {
    HWND h = unwrap_hwnd(checkbox);
    if (!h) return moo_bool(0);
    SendMessageW(h, BM_SETCHECK,
                 bool_or(wert, 0) ? BST_CHECKED : BST_UNCHECKED, 0);
    return moo_bool(1);
}

/* Radio-Gruppen: Per Fenster + Gruppennamen tracken wir den "ersten" Radio.
 * Folge-Radios bekommen KEIN WS_GROUP; nur der erste hat WS_GROUP-Flag.
 * Das sorgt dafuer, dass die Win32-Auto-Radio-Logik nur Radios dieser Gruppe
 * toggelt. */
MooValue moo_ui_radio(MooValue parent, MooValue gruppe, MooValue text,
                      MooValue x, MooValue y, MooValue b, MooValue h,
                      MooValue callback) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    HWND top = GetAncestor(par, GA_ROOT);
    const char* grp = str_or(gruppe, "default");
    wchar_t prop_name[64];
    swprintf(prop_name, 64, L"moo-radio-grp:%hs", grp);

    HWND first = (HWND)GetPropW(top, prop_name);
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON;
    if (!first) style |= WS_GROUP;

    wchar_t* wt = utf8_to_wide(str_or(text, ""));
    UINT id = alloc_ctrl_id();
    HWND rb = CreateWindowExW(0, L"BUTTON", wt ? wt : L"", style,
        num_or(x, 0), num_or(y, 0), num_or(b, 120), num_or(h, 22),
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    free(wt);
    if (!rb) return moo_bool(0);
    if (!first) SetPropW(top, prop_name, (HANDLE)rb);
    if (callback.tag == MOO_FUNC) {
        MooCbBox* box = cb_box_new(callback);
        SetPropW(rb, L"moo-cb", (HANDLE)box);
    }
    return wrap_hwnd(rb);
}

MooValue moo_ui_radio_wert(MooValue radio) {
    HWND h = unwrap_hwnd(radio);
    if (!h) return moo_bool(0);
    LRESULT st = SendMessageW(h, BM_GETCHECK, 0, 0);
    return moo_bool(st == BST_CHECKED ? 1 : 0);
}

MooValue moo_ui_eingabe(MooValue parent,
                        MooValue x, MooValue y, MooValue b, MooValue h,
                        MooValue platzhalter, MooValue passwort) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
    if (bool_or(passwort, 0)) style |= ES_PASSWORD;
    UINT id = alloc_ctrl_id();
    HWND en = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", style,
        num_or(x, 0), num_or(y, 0), num_or(b, 160), num_or(h, 24),
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    if (!en) return moo_bool(0);
    if (platzhalter.tag == MOO_STRING) {
        wchar_t* wp = utf8_to_wide(MV_STR(platzhalter)->chars);
        if (wp) {
            /* EM_SETCUEBANNER: Win Vista+; 2. Param: retain on focus */
            SendMessageW(en, 0x1501 /* EM_SETCUEBANNER */, TRUE, (LPARAM)wp);
            free(wp);
        }
    }
    return wrap_hwnd(en);
}

MooValue moo_ui_eingabe_text(MooValue eingabe) {
    HWND h = unwrap_hwnd(eingabe);
    if (!h) return moo_string_new("");
    int n = GetWindowTextLengthW(h);
    wchar_t* buf = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)(n + 1));
    if (!buf) return moo_string_new("");
    GetWindowTextW(h, buf, n + 1);
    char* u = wide_to_utf8(buf);
    free(buf);
    MooValue rv = moo_string_new(u ? u : "");
    free(u);
    return rv;
}

MooValue moo_ui_eingabe_setze(MooValue eingabe, MooValue text) {
    HWND h = unwrap_hwnd(eingabe);
    if (!h) return moo_bool(0);
    wchar_t* wt = utf8_to_wide(str_or(text, ""));
    BOOL ok = SetWindowTextW(h, wt ? wt : L"");
    free(wt);
    return moo_bool(ok ? 1 : 0);
}

MooValue moo_ui_eingabe_on_change(MooValue eingabe, MooValue callback) {
    HWND h = unwrap_hwnd(eingabe);
    if (!h) return moo_bool(0);
    MooCbBox* box = cb_box_new(callback);
    cb_box_free_prop(h, L"moo-change");
    SetPropW(h, L"moo-change", (HANDLE)box);
    return moo_bool(1);
}

MooValue moo_ui_textbereich(MooValue parent,
                            MooValue x, MooValue y, MooValue b, MooValue h) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    UINT id = alloc_ctrl_id();
    HWND tv = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER
      | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
        num_or(x, 0), num_or(y, 0), num_or(b, 200), num_or(h, 100),
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    if (!tv) return moo_bool(0);
    return wrap_hwnd(tv);
}

MooValue moo_ui_textbereich_text(MooValue tb) {
    return moo_ui_eingabe_text(tb);
}

MooValue moo_ui_textbereich_setze(MooValue tb, MooValue text) {
    return moo_ui_eingabe_setze(tb, text);
}

MooValue moo_ui_textbereich_anhaengen(MooValue tb, MooValue text) {
    HWND h = unwrap_hwnd(tb);
    if (!h) return moo_bool(0);
    int len = GetWindowTextLengthW(h);
    SendMessageW(h, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    wchar_t* wt = utf8_to_wide(str_or(text, ""));
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)(wt ? wt : L""));
    free(wt);
    return moo_bool(1);
}

/* =========================================================================
 * Dropdown (ComboBox)
 * ========================================================================= */

MooValue moo_ui_dropdown(MooValue parent, MooValue optionen,
                         MooValue x, MooValue y, MooValue b, MooValue h,
                         MooValue callback) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    UINT id = alloc_ctrl_id();
    /* Hoehe ist die TOTAL-Hoehe inkl. Dropdown-Liste — Win32 Combo
     * verlangt genug Platz fuer die Listendarstellung. */
    int hh = num_or(h, 24);
    if (hh < 160) hh = 160;  /* Dropdown-Liste braucht Platz */
    HWND dd = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        num_or(x, 0), num_or(y, 0), num_or(b, 140), hh,
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    if (!dd) return moo_bool(0);
    if (optionen.tag == MOO_LIST) {
        int32_t n = moo_list_iter_len(optionen);
        for (int32_t i = 0; i < n; i++) {
            MooValue it = moo_list_iter_get(optionen, i);
            if (it.tag == MOO_STRING) {
                wchar_t* w = utf8_to_wide(MV_STR(it)->chars);
                SendMessageW(dd, CB_ADDSTRING, 0, (LPARAM)(w ? w : L""));
                free(w);
            }
        }
    }
    if (callback.tag == MOO_FUNC) {
        MooCbBox* box = cb_box_new(callback);
        SetPropW(dd, L"moo-cb", (HANDLE)box);
    }
    return wrap_hwnd(dd);
}

MooValue moo_ui_dropdown_auswahl(MooValue dd) {
    HWND h = unwrap_hwnd(dd);
    if (!h) return moo_number(-1);
    LRESULT idx = SendMessageW(h, CB_GETCURSEL, 0, 0);
    return moo_number(idx == CB_ERR ? -1.0 : (double)idx);
}

MooValue moo_ui_dropdown_auswahl_setze(MooValue dd, MooValue index) {
    HWND h = unwrap_hwnd(dd);
    if (!h) return moo_bool(0);
    SendMessageW(h, CB_SETCURSEL, (WPARAM)num_or(index, -1), 0);
    return moo_bool(1);
}

MooValue moo_ui_dropdown_text(MooValue dd) {
    HWND h = unwrap_hwnd(dd);
    if (!h) return moo_string_new("");
    LRESULT idx = SendMessageW(h, CB_GETCURSEL, 0, 0);
    if (idx == CB_ERR) return moo_string_new("");
    LRESULT len = SendMessageW(h, CB_GETLBTEXTLEN, (WPARAM)idx, 0);
    if (len == CB_ERR || len < 0) return moo_string_new("");
    wchar_t* buf = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)(len + 1));
    SendMessageW(h, CB_GETLBTEXT, (WPARAM)idx, (LPARAM)buf);
    char* u = wide_to_utf8(buf);
    free(buf);
    MooValue rv = moo_string_new(u ? u : "");
    free(u);
    return rv;
}

/* =========================================================================
 * Liste (ListView, SysListView32)
 * ========================================================================= */

MooValue moo_ui_liste(MooValue parent, MooValue spalten,
                      MooValue x, MooValue y, MooValue b, MooValue h) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    UINT id = alloc_ctrl_id();
    HWND lv = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER
      | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        num_or(x, 0), num_or(y, 0), num_or(b, 300), num_or(h, 150),
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    if (!lv) return moo_bool(0);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    int32_t ncols = 1;
    if (spalten.tag == MOO_LIST) ncols = moo_list_iter_len(spalten);
    if (ncols < 1) ncols = 1;

    RECT rc;
    GetClientRect(lv, &rc);
    int col_w = (rc.right > 0) ? (int)(rc.right / ncols) : 100;
    if (col_w < 60) col_w = 60;

    for (int32_t i = 0; i < ncols; i++) {
        const char* title = "";
        if (spalten.tag == MOO_LIST) {
            MooValue c = moo_list_iter_get(spalten, i);
            if (c.tag == MOO_STRING) title = MV_STR(c)->chars;
        }
        wchar_t* wt = utf8_to_wide(title);
        LVCOLUMNW col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = wt ? wt : L"";
        col.cx = col_w;
        col.iSubItem = (int)i;
        SendMessageW(lv, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
        free(wt);
    }

    /* Anzahl Spalten am HWND merken (fuer liste_zeile Rueckgabe). */
    SetPropW(lv, L"moo-ncols", (HANDLE)(UINT_PTR)ncols);
    return wrap_hwnd(lv);
}

MooValue moo_ui_liste_zeile_hinzu(MooValue liste, MooValue zeile) {
    HWND h = unwrap_hwnd(liste);
    if (!h) return moo_bool(0);
    int row = (int)SendMessageW(h, LVM_GETITEMCOUNT, 0, 0);
    int32_t n = (zeile.tag == MOO_LIST) ? moo_list_iter_len(zeile) : 0;

    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = row;

    /* Erstes Feld (Spalte 0) per InsertItem. */
    wchar_t* w0 = NULL;
    if (n > 0) {
        MooValue c = moo_list_iter_get(zeile, 0);
        if (c.tag == MOO_STRING) w0 = utf8_to_wide(MV_STR(c)->chars);
    }
    it.pszText = w0 ? w0 : (wchar_t*)L"";
    int inserted = (int)SendMessageW(h, LVM_INSERTITEMW, 0, (LPARAM)&it);
    free(w0);
    if (inserted < 0) return moo_bool(0);

    for (int32_t i = 1; i < n; i++) {
        MooValue c = moo_list_iter_get(zeile, i);
        wchar_t* wi = NULL;
        if (c.tag == MOO_STRING) wi = utf8_to_wide(MV_STR(c)->chars);
        LVITEMW sub;
        memset(&sub, 0, sizeof(sub));
        sub.mask = LVIF_TEXT;
        sub.iItem = inserted;
        sub.iSubItem = (int)i;
        sub.pszText = wi ? wi : (wchar_t*)L"";
        SendMessageW(h, LVM_SETITEMW, 0, (LPARAM)&sub);
        free(wi);
    }
    return moo_bool(1);
}

MooValue moo_ui_liste_auswahl(MooValue liste) {
    HWND h = unwrap_hwnd(liste);
    if (!h) return moo_number(-1);
    int idx = (int)SendMessageW(h, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    return moo_number((double)idx);
}

MooValue moo_ui_liste_zeile(MooValue liste, MooValue index) {
    HWND h = unwrap_hwnd(liste);
    int ncols = (int)(UINT_PTR)GetPropW(h, L"moo-ncols");
    if (ncols < 1) ncols = 1;
    int idx = num_or(index, -1);
    MooValue out = moo_list_new(ncols);
    if (!h || idx < 0) return out;
    for (int i = 0; i < ncols; i++) {
        wchar_t buf[512];
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.iSubItem = i;
        it.pszText = buf;
        it.cchTextMax = 512;
        buf[0] = L'\0';
        SendMessageW(h, LVM_GETITEMTEXTW, (WPARAM)idx, (LPARAM)&it);
        char* u = wide_to_utf8(buf);
        moo_list_append(out, moo_string_new(u ? u : ""));
        free(u);
    }
    return out;
}

MooValue moo_ui_liste_leeren(MooValue liste) {
    HWND h = unwrap_hwnd(liste);
    if (!h) return moo_bool(0);
    SendMessageW(h, LVM_DELETEALLITEMS, 0, 0);
    return moo_bool(1);
}

MooValue moo_ui_liste_on_auswahl(MooValue liste, MooValue callback) {
    HWND h = unwrap_hwnd(liste);
    if (!h) return moo_bool(0);
    MooCbBox* box = cb_box_new(callback);
    cb_box_free_prop(h, L"moo-onsel");
    SetPropW(h, L"moo-onsel", (HANDLE)box);
    return moo_bool(1);
}

/* =========================================================================
 * Slider (Trackbar) + Fortschritt
 * ========================================================================= */

MooValue moo_ui_slider(MooValue parent, MooValue min, MooValue max, MooValue start,
                       MooValue x, MooValue y, MooValue b, MooValue h,
                       MooValue callback) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    UINT id = alloc_ctrl_id();
    HWND sl = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
        num_or(x, 0), num_or(y, 0), num_or(b, 200), num_or(h, 30),
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    if (!sl) return moo_bool(0);
    int mn = (min.tag == MOO_NUMBER) ? (int)MV_NUM(min) : 0;
    int mx = (max.tag == MOO_NUMBER) ? (int)MV_NUM(max) : 100;
    int st = (start.tag == MOO_NUMBER) ? (int)MV_NUM(start) : mn;
    SendMessageW(sl, TBM_SETRANGE, TRUE, MAKELPARAM(mn, mx));
    SendMessageW(sl, TBM_SETPOS, TRUE, st);
    if (callback.tag == MOO_FUNC) {
        MooCbBox* box = cb_box_new(callback);
        SetPropW(sl, L"moo-cb", (HANDLE)box);
    }
    return wrap_hwnd(sl);
}

MooValue moo_ui_slider_wert(MooValue slider) {
    HWND h = unwrap_hwnd(slider);
    if (!h) return moo_number(0.0);
    LRESULT pos = SendMessageW(h, TBM_GETPOS, 0, 0);
    return moo_number((double)pos);
}

MooValue moo_ui_slider_setze(MooValue slider, MooValue wert) {
    HWND h = unwrap_hwnd(slider);
    if (!h) return moo_bool(0);
    int v = (wert.tag == MOO_NUMBER) ? (int)MV_NUM(wert) : 0;
    SendMessageW(h, TBM_SETPOS, TRUE, v);
    return moo_bool(1);
}

MooValue moo_ui_fortschritt(MooValue parent,
                            MooValue x, MooValue y, MooValue b, MooValue h) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    UINT id = alloc_ctrl_id();
    HWND pb = CreateWindowExW(0, PROGRESS_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        num_or(x, 0), num_or(y, 0), num_or(b, 200), num_or(h, 20),
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    if (!pb) return moo_bool(0);
    SendMessageW(pb, PBM_SETRANGE32, 0, 1000);  /* 0..1000 fuer 0.0..1.0 */
    return wrap_hwnd(pb);
}

MooValue moo_ui_fortschritt_setze(MooValue bar, MooValue wert) {
    HWND h = unwrap_hwnd(bar);
    if (!h) return moo_bool(0);
    double v = (wert.tag == MOO_NUMBER) ? MV_NUM(wert) : 0.0;
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    SendMessageW(h, PBM_SETPOS, (WPARAM)(int)(v * 1000.0), 0);
    return moo_bool(1);
}

/* =========================================================================
 * Bild (STATIC + SS_BITMAP) + Leinwand (Owner-Draw STATIC)
 * ========================================================================= */

MooValue moo_ui_bild(MooValue parent, MooValue pfad,
                     MooValue x, MooValue y, MooValue b, MooValue h) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    UINT id = alloc_ctrl_id();
    HWND img = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_BITMAP,
        num_or(x, 0), num_or(y, 0), num_or(b, 100), num_or(h, 100),
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    if (!img) return moo_bool(0);
    MooValue dummy = wrap_hwnd(img);
    moo_ui_bild_setze(dummy, pfad);
    return dummy;
}

MooValue moo_ui_bild_setze(MooValue bild, MooValue pfad) {
    HWND h = unwrap_hwnd(bild);
    if (!h) return moo_bool(0);
    const char* p = str_or(pfad, "");
    if (!*p) return moo_bool(0);
    wchar_t* wp = utf8_to_wide(p);
    HBITMAP bmp = (HBITMAP)LoadImageW(NULL, wp ? wp : L"",
                                      IMAGE_BITMAP, 0, 0,
                                      LR_LOADFROMFILE);
    free(wp);
    if (!bmp) return moo_bool(0);
    SendMessageW(h, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)bmp);
    return moo_bool(1);
}

MooValue moo_ui_leinwand(MooValue parent,
                         MooValue x, MooValue y, MooValue b, MooValue h,
                         MooValue on_zeichne) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    UINT id = alloc_ctrl_id();
    /* SS_OWNERDRAW triggert WM_DRAWITEM im Parent (Fenster-WndProc). */
    HWND da = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        num_or(x, 0), num_or(y, 0), num_or(b, 200), num_or(h, 200),
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    if (!da) return moo_bool(0);
    if (on_zeichne.tag == MOO_FUNC) {
        MooCbBox* box = cb_box_new(on_zeichne);
        SetPropW(da, L"moo-draw", (HANDLE)box);
    }
    return wrap_hwnd(da);
}

MooValue moo_ui_leinwand_anfordern(MooValue leinwand) {
    HWND h = unwrap_hwnd(leinwand);
    if (!h) return moo_bool(0);
    InvalidateRect(h, NULL, TRUE);
    return moo_bool(1);
}

/* =========================================================================
 * Rahmen (Groupbox), Trenner, Tabs, Scroll
 * ========================================================================= */

MooValue moo_ui_rahmen(MooValue parent, MooValue titel,
                       MooValue x, MooValue y, MooValue b, MooValue h) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    UINT id = alloc_ctrl_id();
    wchar_t* wt = utf8_to_wide(str_or(titel, ""));

    /* GroupBox ist BS_GROUPBOX, kann aber nicht selbst Kind-Container fuer
     * WM_COMMAND sein. Wir legen einen MooContainer dahinter (gleiche
     * Geometrie, aber transparent), der die Kinder traegt und WM_COMMAND
     * weiterreicht. Visuelle Groupbox-Rahmen ist das BS_GROUPBOX-Button. */
    HWND gb = CreateWindowExW(0, L"BUTTON", wt ? wt : L"",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        num_or(x, 0), num_or(y, 0), num_or(b, 200), num_or(h, 100),
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    free(wt);
    if (!gb) return moo_bool(0);
    /* Container-Widget-Handle ist die GroupBox selbst — Kinder werden als
     * Direkt-Kinder des Top-Fensters angelegt und absolut positioniert.
     * Das ist pragmatisch: Win32-BS_GROUPBOX nimmt keine echten Child-Ctrls
     * via CreateWindow-parent-Argument (Kinder waeren Z-Order-verdeckt).
     * Nutzer positioniert Kinder manuell INSIDE der Groupbox-Geometrie. */
    return wrap_hwnd(gb);
}

MooValue moo_ui_trenner(MooValue parent,
                        MooValue x, MooValue y, MooValue b, MooValue h) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    int bb = num_or(b, 100), hh = num_or(h, 2);
    DWORD style = WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ;
    if (hh > bb) style = (style & ~SS_ETCHEDHORZ) | SS_ETCHEDVERT;
    UINT id = alloc_ctrl_id();
    HWND s = CreateWindowExW(0, L"STATIC", L"", style,
        num_or(x, 0), num_or(y, 0), bb, hh,
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    if (!s) return moo_bool(0);
    return wrap_hwnd(s);
}

MooValue moo_ui_tabs(MooValue parent,
                     MooValue x, MooValue y, MooValue b, MooValue h) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    UINT id = alloc_ctrl_id();
    HWND tc = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        num_or(x, 0), num_or(y, 0), num_or(b, 300), num_or(h, 200),
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    if (!tc) return moo_bool(0);
    return wrap_hwnd(tc);
}

MooValue moo_ui_tab_hinzu(MooValue tabs, MooValue titel) {
    HWND tc = unwrap_hwnd(tabs);
    if (!tc) return moo_bool(0);
    wchar_t* wt = utf8_to_wide(str_or(titel, ""));
    TCITEMW it;
    memset(&it, 0, sizeof(it));
    it.mask = TCIF_TEXT;
    it.pszText = wt ? wt : L"";
    int idx = (int)SendMessageW(tc, TCM_GETITEMCOUNT, 0, 0);
    SendMessageW(tc, TCM_INSERTITEMW, (WPARAM)idx, (LPARAM)&it);
    free(wt);

    /* Tab-Page als MooContainer-Child des Tab-Controls, positioniert im
     * Display-Rect des Tab-Controls. */
    RECT rc;
    GetClientRect(tc, &rc);
    SendMessageW(tc, TCM_ADJUSTRECT, FALSE, (LPARAM)&rc);
    UINT pid = alloc_ctrl_id();
    HWND page = CreateWindowExW(0, MOO_CONTAINER_CLASS, L"",
        WS_CHILD | ((idx == 0) ? WS_VISIBLE : 0),
        rc.left, rc.top,
        rc.right - rc.left, rc.bottom - rc.top,
        tc, (HMENU)(UINT_PTR)pid, g_hinstance, NULL);
    if (!page) return moo_bool(0);
    return wrap_hwnd(page);
}

MooValue moo_ui_tabs_auswahl(MooValue tabs) {
    HWND tc = unwrap_hwnd(tabs);
    if (!tc) return moo_number(-1);
    LRESULT idx = SendMessageW(tc, TCM_GETCURSEL, 0, 0);
    return moo_number((double)idx);
}

MooValue moo_ui_tabs_auswahl_setze(MooValue tabs, MooValue index) {
    HWND tc = unwrap_hwnd(tabs);
    if (!tc) return moo_bool(0);
    SendMessageW(tc, TCM_SETCURSEL, (WPARAM)num_or(index, 0), 0);
    return moo_bool(1);
}

MooValue moo_ui_scroll(MooValue parent,
                       MooValue x, MooValue y, MooValue b, MooValue h) {
    HWND par = resolve_container(parent);
    if (!par) return moo_bool(0);
    UINT id = alloc_ctrl_id();
    HWND sc = CreateWindowExW(WS_EX_CLIENTEDGE, MOO_CONTAINER_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL | WS_BORDER,
        num_or(x, 0), num_or(y, 0), num_or(b, 200), num_or(h, 200),
        par, (HMENU)(UINT_PTR)id, g_hinstance, NULL);
    if (!sc) return moo_bool(0);
    return wrap_hwnd(sc);
}

/* =========================================================================
 * Allgemeine Widget-Operationen
 * ========================================================================= */

MooValue moo_ui_sichtbar(MooValue widget, MooValue sichtbar) {
    HWND h = unwrap_hwnd(widget);
    if (!h) return moo_bool(0);
    ShowWindow(h, bool_or(sichtbar, 1) ? SW_SHOW : SW_HIDE);
    return moo_bool(1);
}

MooValue moo_ui_aktiv(MooValue widget, MooValue aktiv) {
    HWND h = unwrap_hwnd(widget);
    if (!h) return moo_bool(0);
    EnableWindow(h, bool_or(aktiv, 1) ? TRUE : FALSE);
    return moo_bool(1);
}

MooValue moo_ui_position_setze(MooValue widget, MooValue x, MooValue y) {
    HWND h = unwrap_hwnd(widget);
    if (!h) return moo_bool(0);
    SetWindowPos(h, NULL, num_or(x, 0), num_or(y, 0), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER);
    return moo_bool(1);
}

MooValue moo_ui_groesse_setze(MooValue widget, MooValue b, MooValue hh) {
    HWND h = unwrap_hwnd(widget);
    if (!h) return moo_bool(0);
    SetWindowPos(h, NULL, 0, 0, num_or(b, 100), num_or(hh, 100),
                 SWP_NOMOVE | SWP_NOZORDER);
    return moo_bool(1);
}

/* Farbe: Win32 hat keine trivialen per-Widget-CSS-Hooks. Wir speichern die
 * gewuenschte Farbe am HWND als Prop; ein WM_CTLCOLOR*-Handler im Fenster
 * koennte sie spaeter auswerten. Phase-3 stub (setzt nur Prop, visuelle
 * Wirkung nur wenn parent-WndProc das auswertet). */
MooValue moo_ui_farbe_setze(MooValue widget, MooValue hex) {
    HWND h = unwrap_hwnd(widget);
    if (!h) return moo_bool(0);
    const char* c = str_or(hex, "#000000");
    unsigned int r = 0, g = 0, bl = 0;
    if (c[0] == '#' && strlen(c) == 7) {
        sscanf(c + 1, "%02x%02x%02x", &r, &g, &bl);
    }
    COLORREF col = RGB(r, g, bl);
    SetPropW(h, L"moo-color", (HANDLE)(UINT_PTR)col);
    InvalidateRect(h, NULL, TRUE);
    return moo_bool(1);
}

MooValue moo_ui_schrift_setze(MooValue widget, MooValue groesse, MooValue fett) {
    HWND h = unwrap_hwnd(widget);
    if (!h) return moo_bool(0);
    int sz = num_or(groesse, 10);
    int bold = bool_or(fett, 0);
    HDC dc = GetDC(h);
    int lfHeight = -MulDiv(sz, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(h, dc);
    HFONT font = CreateFontW(lfHeight, 0, 0, 0,
                             bold ? FW_BOLD : FW_NORMAL,
                             FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                             L"Segoe UI");
    if (!font) return moo_bool(0);
    /* Alte Font-Ref entsorgen (falls wir eine gesetzt hatten). */
    HFONT old = (HFONT)GetPropW(h, L"moo-font");
    if (old) DeleteObject(old);
    SetPropW(h, L"moo-font", (HANDLE)font);
    SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
    return moo_bool(1);
}

/* Tooltip: erzeugt bei Bedarf einen gemeinsamen Tooltip-Control pro Fenster.
 * Phase-3 vereinfacht: pro Control ein eigener Tooltip. */
MooValue moo_ui_tooltip_setze(MooValue widget, MooValue text) {
    HWND h = unwrap_hwnd(widget);
    if (!h) return moo_bool(0);
    HWND top = GetAncestor(h, GA_ROOT);
    HWND tt = (HWND)GetPropW(top, L"moo-tooltip");
    if (!tt) {
        tt = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, L"",
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            top, NULL, g_hinstance, NULL);
        if (!tt) return moo_bool(0);
        SetPropW(top, L"moo-tooltip", (HANDLE)tt);
    }
    wchar_t* wt = utf8_to_wide(str_or(text, ""));
    TOOLINFOW ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize   = sizeof(ti);
    ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd     = top;
    ti.uId      = (UINT_PTR)h;
    ti.lpszText = wt ? wt : L"";
    /* Falls bereits existierend: loeschen, dann neu einfuegen. */
    SendMessageW(tt, TTM_DELTOOLW, 0, (LPARAM)&ti);
    SendMessageW(tt, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    free(wt);
    return moo_bool(1);
}

MooValue moo_ui_zerstoere(MooValue widget) {
    HWND h = unwrap_hwnd(widget);
    if (!h) return moo_bool(0);
    DestroyWindow(h);
    return moo_bool(1);
}

/* =========================================================================
 * Timer — SetTimer mit Callback-Lookup via TimerProc
 * ========================================================================= */

typedef struct {
    UINT_PTR id;
    MooCbBox* cb;
} MooTimer;

#define MOO_TIMER_MAX 256
static MooTimer g_timers[MOO_TIMER_MAX];

static void CALLBACK moo_timer_proc(HWND hwnd, UINT msg, UINT_PTR id, DWORD t) {
    (void)hwnd; (void)msg; (void)t;
    for (int i = 0; i < MOO_TIMER_MAX; i++) {
        if (g_timers[i].id == id && g_timers[i].cb) {
            MooValue rv = moo_func_call_0(g_timers[i].cb->v);
            moo_release(rv);
            return;
        }
    }
}

MooValue moo_ui_timer_hinzu(MooValue ms, MooValue callback) {
    ensure_init();
    int ival = num_or(ms, 1000);
    /* freien Slot suchen */
    for (int i = 0; i < MOO_TIMER_MAX; i++) {
        if (g_timers[i].id == 0) {
            UINT_PTR tid = SetTimer(NULL, 0, (UINT)ival, moo_timer_proc);
            if (tid == 0) return moo_bool(0);
            g_timers[i].id = tid;
            g_timers[i].cb = cb_box_new(callback);
            return moo_number((double)(uintptr_t)tid);
        }
    }
    return moo_bool(0);
}

MooValue moo_ui_timer_entfernen(MooValue timer_id) {
    UINT_PTR id = (UINT_PTR)(uintptr_t)num_or(timer_id, 0);
    if (id == 0) return moo_bool(0);
    for (int i = 0; i < MOO_TIMER_MAX; i++) {
        if (g_timers[i].id == id) {
            KillTimer(NULL, id);
            cb_box_free(g_timers[i].cb);
            g_timers[i].id = 0;
            g_timers[i].cb = NULL;
            return moo_bool(1);
        }
    }
    return moo_bool(0);
}

/* =========================================================================
 * Dialoge
 * ========================================================================= */

static HWND dlg_parent(MooValue parent) {
    HWND h = unwrap_hwnd(parent);
    if (!h) return NULL;
    return GetAncestor(h, GA_ROOT);
}

static MooValue msgbox(MooValue parent, MooValue titel, MooValue text, UINT type) {
    ensure_init();
    wchar_t* wt = utf8_to_wide(str_or(titel, ""));
    wchar_t* wx = utf8_to_wide(str_or(text, ""));
    int rv = MessageBoxW(dlg_parent(parent), wx ? wx : L"", wt ? wt : L"", type);
    free(wt); free(wx);
    if (type & MB_YESNO) return moo_bool(rv == IDYES ? 1 : 0);
    return moo_none();
}

MooValue moo_ui_info(MooValue parent, MooValue titel, MooValue text) {
    return msgbox(parent, titel, text, MB_OK | MB_ICONINFORMATION);
}

MooValue moo_ui_warnung(MooValue parent, MooValue titel, MooValue text) {
    return msgbox(parent, titel, text, MB_OK | MB_ICONWARNING);
}

MooValue moo_ui_fehler(MooValue parent, MooValue titel, MooValue text) {
    return msgbox(parent, titel, text, MB_OK | MB_ICONERROR);
}

MooValue moo_ui_frage(MooValue parent, MooValue titel, MooValue text) {
    return msgbox(parent, titel, text, MB_YESNO | MB_ICONQUESTION);
}

/* Eingabe-Dialog: Win32 hat keinen eingebauten Input-Dialog. Wir bauen ein
 * minimales Template-less Dialog-Fenster selbst — Fenster + Label + Edit +
 * zwei Buttons, eigener WndProc. Laeuft synchron via lokalem Message-Loop. */

typedef struct {
    wchar_t buf[1024];
    int ok;
    int done;
} InputCtx;

static LRESULT CALLBACK moo_input_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    InputCtx* ctx = (InputCtx*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_COMMAND: {
            UINT id = LOWORD(wp);
            if (id == IDOK) {
                HWND ed = GetDlgItem(hwnd, 101);
                if (ctx) GetWindowTextW(ed, ctx->buf, 1024);
                if (ctx) { ctx->ok = 1; ctx->done = 1; }
                DestroyWindow(hwnd);
                return 0;
            }
            if (id == IDCANCEL) {
                if (ctx) { ctx->ok = 0; ctx->done = 1; }
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            if (ctx) { ctx->ok = 0; ctx->done = 1; }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

MooValue moo_ui_eingabe_dialog(MooValue parent, MooValue titel,
                               MooValue prompt, MooValue vorgabe) {
    ensure_init();

    static int registered = 0;
    if (!registered) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = moo_input_proc;
        wc.hInstance     = g_hinstance;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"MooUIInputDlg";
        RegisterClassExW(&wc);
        registered = 1;
    }

    HWND par = dlg_parent(parent);
    wchar_t* wtitle = utf8_to_wide(str_or(titel, "Eingabe"));
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"MooUIInputDlg", wtitle ? wtitle : L"Eingabe",
        WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 160,
        par, NULL, g_hinstance, NULL);
    free(wtitle);
    if (!dlg) return moo_none();

    InputCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&ctx);

    wchar_t* wprompt = utf8_to_wide(str_or(prompt, ""));
    wchar_t* wdef = utf8_to_wide(str_or(vorgabe, ""));

    CreateWindowExW(0, L"STATIC", wprompt ? wprompt : L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        12, 12, 368, 20, dlg, (HMENU)100, g_hinstance, NULL);
    HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wdef ? wdef : L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        12, 38, 368, 24, dlg, (HMENU)101, g_hinstance, NULL);
    CreateWindowExW(0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        220, 78, 80, 26, dlg, (HMENU)(UINT_PTR)IDOK, g_hinstance, NULL);
    CreateWindowExW(0, L"BUTTON", L"Abbrechen",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        305, 78, 80, 26, dlg, (HMENU)(UINT_PTR)IDCANCEL, g_hinstance, NULL);
    free(wprompt); free(wdef);

    if (par) EnableWindow(par, FALSE);

    MSG msg;
    while (!ctx.done && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (par) EnableWindow(par, TRUE);

    if (!ctx.ok) return moo_none();
    char* u = wide_to_utf8(ctx.buf);
    MooValue rv = moo_string_new(u ? u : "");
    free(u);
    return rv;
}

/* Datei-Filter-Liste → OFN-Style-String "Name\0Pattern\0...\0\0".
 * Caller muss free() aufrufen. */
static wchar_t* build_ofn_filter(MooValue filter) {
    if (filter.tag != MOO_LIST) {
        wchar_t* buf = (wchar_t*)malloc(sizeof(wchar_t) * 32);
        wcscpy(buf, L"Alle\0*.*\0");
        buf[9] = L'\0';  /* doppel-null */
        return buf;
    }
    int32_t n = moo_list_iter_len(filter);
    /* grob ueberdimensionieren */
    size_t cap = 16;
    for (int32_t i = 0; i < n; i++) {
        MooValue pair = moo_list_iter_get(filter, i);
        if (pair.tag != MOO_LIST || moo_list_iter_len(pair) < 2) continue;
        MooValue nm = moo_list_iter_get(pair, 0);
        MooValue pt = moo_list_iter_get(pair, 1);
        if (nm.tag == MOO_STRING) cap += MV_STR(nm)->length + 4;
        if (pt.tag == MOO_STRING) cap += MV_STR(pt)->length + 4;
    }
    wchar_t* buf = (wchar_t*)calloc(cap, sizeof(wchar_t));
    size_t pos = 0;
    for (int32_t i = 0; i < n; i++) {
        MooValue pair = moo_list_iter_get(filter, i);
        if (pair.tag != MOO_LIST || moo_list_iter_len(pair) < 2) continue;
        MooValue nm = moo_list_iter_get(pair, 0);
        MooValue pt = moo_list_iter_get(pair, 1);
        if (nm.tag != MOO_STRING || pt.tag != MOO_STRING) continue;
        wchar_t* wn = utf8_to_wide(MV_STR(nm)->chars);
        wchar_t* wp = utf8_to_wide(MV_STR(pt)->chars);
        if (wn) { wcscpy(buf + pos, wn); pos += wcslen(wn) + 1; free(wn); }
        if (wp) { wcscpy(buf + pos, wp); pos += wcslen(wp) + 1; free(wp); }
    }
    buf[pos] = L'\0';  /* terminierendes doppel-null */
    return buf;
}

static MooValue file_dialog(MooValue parent, MooValue titel, MooValue filter,
                            int save) {
    ensure_init();
    wchar_t file[1024];
    file[0] = L'\0';
    wchar_t* wt = utf8_to_wide(str_or(titel, ""));
    wchar_t* wf = build_ofn_filter(filter);

    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = dlg_parent(parent);
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = 1024;
    ofn.lpstrFilter = wf;
    ofn.lpstrTitle  = wt;
    ofn.Flags       = OFN_EXPLORER | OFN_NOCHANGEDIR
                    | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);

    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    free(wt); free(wf);
    if (!ok) return moo_none();
    char* u = wide_to_utf8(file);
    MooValue rv = moo_string_new(u ? u : "");
    free(u);
    return rv;
}

MooValue moo_ui_datei_oeffnen(MooValue parent, MooValue titel, MooValue filter) {
    return file_dialog(parent, titel, filter, 0);
}

MooValue moo_ui_datei_speichern(MooValue parent, MooValue titel, MooValue filter) {
    return file_dialog(parent, titel, filter, 1);
}

MooValue moo_ui_ordner_waehlen(MooValue parent, MooValue titel) {
    ensure_init();
    wchar_t path[MAX_PATH];
    path[0] = L'\0';
    wchar_t* wt = utf8_to_wide(str_or(titel, ""));

    BROWSEINFOW bi;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = dlg_parent(parent);
    bi.lpszTitle = wt;
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST il = SHBrowseForFolderW(&bi);
    free(wt);
    if (!il) return moo_none();
    SHGetPathFromIDListW(il, path);
    CoTaskMemFree(il);
    char* u = wide_to_utf8(path);
    MooValue rv = moo_string_new(u ? u : "");
    free(u);
    return rv;
}

/* =========================================================================
 * Menueleiste
 * ========================================================================= */

MooValue moo_ui_menueleiste(MooValue fenster) {
    HWND h = unwrap_hwnd(fenster);
    if (!h) return moo_bool(0);
    HMENU mb = GetMenu(h);
    if (!mb) {
        mb = CreateMenu();
        SetMenu(h, mb);
        DrawMenuBar(h);
    }
    /* Menueleiste als MooValue: Wir packen HMENU als Pointer. */
    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, (void*)mb);
    return v;
}

MooValue moo_ui_menue(MooValue leiste, MooValue titel) {
    HMENU mb = (HMENU)moo_val_as_ptr(leiste);
    if (!mb) return moo_bool(0);
    HMENU sub = CreatePopupMenu();
    wchar_t* wt = utf8_to_wide(str_or(titel, ""));
    AppendMenuW(mb, MF_POPUP | MF_STRING, (UINT_PTR)sub, wt ? wt : L"");
    free(wt);
    /* DrawMenuBar auf naechstem Top-Window erzwingen (optional). */
    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, (void*)sub);
    return v;
}

MooValue moo_ui_menue_eintrag(MooValue menue, MooValue titel, MooValue callback) {
    HMENU m = (HMENU)moo_val_as_ptr(menue);
    if (!m) return moo_bool(0);
    UINT id = g_next_menu_id++;
    if (g_next_menu_id >= 57343) g_next_menu_id = 40000;
    wchar_t* wt = utf8_to_wide(str_or(titel, ""));
    AppendMenuW(m, MF_STRING, id, wt ? wt : L"");
    free(wt);
    if (callback.tag == MOO_FUNC) {
        MooCbBox* box = cb_box_new(callback);
        register_cb(id, box);
    }
    /* Item-Handle als "pseudo-id" in MooValue. */
    return moo_number((double)id);
}

MooValue moo_ui_menue_trenner(MooValue menue) {
    HMENU m = (HMENU)moo_val_as_ptr(menue);
    if (!m) return moo_bool(0);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    return moo_bool(1);
}

MooValue moo_ui_menue_untermenue(MooValue menue, MooValue titel) {
    HMENU m = (HMENU)moo_val_as_ptr(menue);
    if (!m) return moo_bool(0);
    HMENU sub = CreatePopupMenu();
    wchar_t* wt = utf8_to_wide(str_or(titel, ""));
    AppendMenuW(m, MF_POPUP | MF_STRING, (UINT_PTR)sub, wt ? wt : L"");
    free(wt);
    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, (void*)sub);
    return v;
}
