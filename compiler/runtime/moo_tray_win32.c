/*
 * moo_tray_win32.c — Windows-Backend fuer moo_tray.h via Shell_NotifyIcon.
 * =========================================================================
 *
 * Implementiert die OS-neutrale Tray-API aus moo_tray.h mit der Win32-
 * Shell-API. Jeder Tray laeuft ueber ein unsichtbares Message-Only-Fenster,
 * das WM_USER+1 vom Tray-Icon empfaengt. Bei Rechts-Click wird das per
 * CreatePopupMenu aufgebaute HMENU via TrackPopupMenu angezeigt; Linksklick
 * feuert aktuell nichts (die GTK-Variante hat ebenfalls nur Menue-Callbacks).
 *
 * Handle-Layout:
 *   Tray        — MooTrayWin*   (gepackter Pointer in MooValue.data)
 *   Submenu     — HMENU         (gepackter Pointer)
 *   Menu-Item   — UINT id       (als double in MooValue fuer Check-Items,
 *                                damit _wert/_set funktioniert)
 *
 * Callback-Ownership:
 *   Pro Menue-Item eine Callback-Box (malloc + moo_retain). Bei
 *   moo_tray_menu_clear werden alle Items released + freigegeben, das Menu
 *   selbst neu erstellt. Beim Tray-Destroy (Implizit am Prozessende)
 *   wuerden Callbacks leaken — fuer einen Tray, der bis zum Prozessende
 *   lebt, ist das akzeptabel.
 *
 * Plan-Referenz: Memory `plan-002-moo-ui-cross-platform` (Phase 3).
 */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include "moo_runtime.h"
#include "moo_tray.h"

#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string.h>

#define MOO_TRAY_MSG  (WM_USER + 1)
#define MOO_TRAY_UID  1001
#define MOO_TRAY_WND_CLASS  L"MooTrayHidden"

/* =========================================================================
 * Callback-Tabelle (id → MooValue*)
 * ========================================================================= */

typedef struct {
    MooValue v;
} TrayCb;

#define TRAY_CB_MAX 4096
static TrayCb* g_tray_cb[TRAY_CB_MAX];
static UINT g_tray_next_id = 2000;  /* Eigener Range fuer Tray-Menu-IDs */

static UINT tray_alloc_id(void) {
    UINT id = g_tray_next_id++;
    if (g_tray_next_id >= 2000 + TRAY_CB_MAX) g_tray_next_id = 2000;
    return id;
}

static TrayCb* tray_cb_new(MooValue callback) {
    TrayCb* b = (TrayCb*)malloc(sizeof(TrayCb));
    if (!b) return NULL;
    b->v = callback;
    moo_retain(callback);
    return b;
}

static void tray_cb_free(TrayCb* b) {
    if (!b) return;
    moo_release(b->v);
    free(b);
}

static void tray_register_cb(UINT id, TrayCb* cb) {
    UINT slot = id - 2000;
    if (slot < TRAY_CB_MAX) {
        if (g_tray_cb[slot]) tray_cb_free(g_tray_cb[slot]);
        g_tray_cb[slot] = cb;
    }
}

static TrayCb* tray_lookup_cb(UINT id) {
    UINT slot = id - 2000;
    if (slot < TRAY_CB_MAX) return g_tray_cb[slot];
    return NULL;
}

/* =========================================================================
 * Haupt-Struktur
 * ========================================================================= */

typedef struct {
    HWND hwnd;             /* Unsichtbares Message-Fenster */
    NOTIFYICONDATAW nid;
    HMENU menu;            /* Haupt-Popup-Menu */
    int active;
} MooTrayWin;

/* ID-Liste fuer "menu_clear" — beim Clear loeschen wir alle am Haupt-Menu
 * haengenden Callbacks; Submenu-Callbacks ebenfalls. Einfacher Ansatz:
 * beim Clear iterieren wir g_tray_cb und freen alle, die zum Tray gehoeren.
 * Phase-3-pragmatisch: Clear freet ALLE Tray-Callbacks. Ein-Tray-Szenario
 * ist der Normalfall. */
static void tray_cb_clear_all(void) {
    for (UINT i = 0; i < TRAY_CB_MAX; i++) {
        if (g_tray_cb[i]) {
            tray_cb_free(g_tray_cb[i]);
            g_tray_cb[i] = NULL;
        }
    }
    g_tray_next_id = 2000;
}

/* =========================================================================
 * Tray-Window-Proc
 * ========================================================================= */

static LRESULT CALLBACK tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MooTrayWin* tray = (MooTrayWin*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    if (msg == MOO_TRAY_MSG) {
        UINT ev = LOWORD(lp);
        if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) {
            POINT pt;
            GetCursorPos(&pt);
            if (tray && tray->menu) {
                SetForegroundWindow(hwnd);
                TrackPopupMenu(tray->menu,
                               TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                               pt.x, pt.y, 0, hwnd, NULL);
                PostMessageW(hwnd, WM_NULL, 0, 0);
            }
        }
        return 0;
    }

    if (msg == WM_COMMAND) {
        UINT id = LOWORD(wp);
        TrayCb* cb = tray_lookup_cb(id);
        if (cb && cb->v.tag == MOO_FUNC) {
            /* Check-Items automatisch togglen. */
            if (tray) {
                MENUITEMINFOW mi;
                memset(&mi, 0, sizeof(mi));
                mi.cbSize = sizeof(mi);
                mi.fMask  = MIIM_STATE | MIIM_FTYPE;
                if (GetMenuItemInfoW(tray->menu, id, FALSE, &mi)
                    || GetMenuItemInfoW(tray->menu, id, FALSE, &mi)) {
                    /* ok, aber wir wissen noch nicht ob Check-Type */
                }
            }
            MooValue rv = moo_func_call_0(cb->v);
            moo_release(rv);
        }
        return 0;
    }

    if (msg == WM_DESTROY) {
        if (tray) {
            Shell_NotifyIconW(NIM_DELETE, &tray->nid);
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* =========================================================================
 * Init: Fensterklasse, Icon laden
 * ========================================================================= */

static int g_tray_class_registered = 0;

static void ensure_tray_class(void) {
    if (g_tray_class_registered) return;
    g_tray_class_registered = 1;
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize      = sizeof(wc);
    wc.lpfnWndProc = tray_wnd_proc;
    wc.hInstance   = GetModuleHandleW(NULL);
    wc.lpszClassName = MOO_TRAY_WND_CLASS;
    RegisterClassExW(&wc);
}

static HICON load_tray_icon(const char* name) {
    if (!name || !*name) return LoadIcon(NULL, IDI_APPLICATION);
    /* Versuche Datei-Pfad zu laden. */
    int need = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
    if (need <= 0) return LoadIcon(NULL, IDI_APPLICATION);
    wchar_t* wn = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)need);
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wn, need);
    HICON ic = (HICON)LoadImageW(NULL, wn, IMAGE_ICON, 0, 0,
                                 LR_LOADFROMFILE | LR_DEFAULTSIZE);
    free(wn);
    if (!ic) ic = LoadIcon(NULL, IDI_APPLICATION);
    return ic;
}

/* =========================================================================
 * MooValue-Helpers
 * ========================================================================= */

static inline MooValue wrap_ptr(void* p) {
    MooValue v;
    v.tag = MOO_NUMBER;
    moo_val_set_ptr(&v, p);
    return v;
}

/* =========================================================================
 * API — Tray-Icon
 * ========================================================================= */

MooValue moo_tray_create(MooValue titel, MooValue icon_name) {
    ensure_tray_class();

    MooTrayWin* tray = (MooTrayWin*)calloc(1, sizeof(MooTrayWin));
    if (!tray) return moo_bool(0);

    HWND hw = CreateWindowExW(0, MOO_TRAY_WND_CLASS, L"MooTrayHidden",
        0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
        GetModuleHandleW(NULL), NULL);
    if (!hw) { free(tray); return moo_bool(0); }

    tray->hwnd = hw;
    SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)tray);

    tray->menu = CreatePopupMenu();

    memset(&tray->nid, 0, sizeof(tray->nid));
    tray->nid.cbSize = sizeof(tray->nid);
    tray->nid.hWnd = hw;
    tray->nid.uID = MOO_TRAY_UID;
    tray->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray->nid.uCallbackMessage = MOO_TRAY_MSG;

    const char* ico = (icon_name.tag == MOO_STRING) ? MV_STR(icon_name)->chars : "";
    tray->nid.hIcon = load_tray_icon(ico);

    const char* t = (titel.tag == MOO_STRING) ? MV_STR(titel)->chars : "Tray";
    int need = MultiByteToWideChar(CP_UTF8, 0, t, -1, NULL, 0);
    if (need > 0 && need < 128) {
        MultiByteToWideChar(CP_UTF8, 0, t, -1, tray->nid.szTip, 128);
    } else {
        wcsncpy(tray->nid.szTip, L"Tray", 128);
    }

    Shell_NotifyIconW(NIM_ADD, &tray->nid);
    tray->active = 1;

    return wrap_ptr(tray);
}

MooValue moo_tray_titel_setze(MooValue tray_v, MooValue titel) {
    MooTrayWin* tray = (MooTrayWin*)moo_val_as_ptr(tray_v);
    if (!tray) return moo_bool(0);
    const char* t = (titel.tag == MOO_STRING) ? MV_STR(titel)->chars : "";
    tray->nid.uFlags = NIF_TIP;
    MultiByteToWideChar(CP_UTF8, 0, t, -1, tray->nid.szTip, 128);
    Shell_NotifyIconW(NIM_MODIFY, &tray->nid);
    tray->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;  /* restore */
    return moo_bool(1);
}

MooValue moo_tray_icon_setze(MooValue tray_v, MooValue icon_name) {
    MooTrayWin* tray = (MooTrayWin*)moo_val_as_ptr(tray_v);
    if (!tray) return moo_bool(0);
    const char* n = (icon_name.tag == MOO_STRING) ? MV_STR(icon_name)->chars : "";
    HICON new_ico = load_tray_icon(n);
    HICON old = tray->nid.hIcon;
    tray->nid.hIcon = new_ico;
    tray->nid.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &tray->nid);
    tray->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    if (old) DestroyIcon(old);
    return moo_bool(1);
}

MooValue moo_tray_aktiv(MooValue tray_v, MooValue aktiv) {
    MooTrayWin* tray = (MooTrayWin*)moo_val_as_ptr(tray_v);
    if (!tray) return moo_bool(0);
    int an = (aktiv.tag == MOO_BOOL) ? (int)MV_BOOL(aktiv) : 1;
    if (an && !tray->active) {
        Shell_NotifyIconW(NIM_ADD, &tray->nid);
        tray->active = 1;
    } else if (!an && tray->active) {
        Shell_NotifyIconW(NIM_DELETE, &tray->nid);
        tray->active = 0;
    }
    return moo_bool(1);
}

/* =========================================================================
 * Menu (flach, Haupt-Menu)
 * ========================================================================= */

MooValue moo_tray_menu_add(MooValue tray_v, MooValue label, MooValue callback) {
    MooTrayWin* tray = (MooTrayWin*)moo_val_as_ptr(tray_v);
    if (!tray || !tray->menu) return moo_bool(0);
    UINT id = tray_alloc_id();
    const char* l = (label.tag == MOO_STRING) ? MV_STR(label)->chars : "(?)";
    int need = MultiByteToWideChar(CP_UTF8, 0, l, -1, NULL, 0);
    wchar_t* wl = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)need);
    MultiByteToWideChar(CP_UTF8, 0, l, -1, wl, need);
    AppendMenuW(tray->menu, MF_STRING, id, wl);
    free(wl);
    if (callback.tag == MOO_FUNC) tray_register_cb(id, tray_cb_new(callback));
    return moo_number((double)id);
}

MooValue moo_tray_separator_add(MooValue tray_v) {
    MooTrayWin* tray = (MooTrayWin*)moo_val_as_ptr(tray_v);
    if (!tray || !tray->menu) return moo_bool(0);
    AppendMenuW(tray->menu, MF_SEPARATOR, 0, NULL);
    return moo_bool(1);
}

MooValue moo_tray_menu_clear(MooValue tray_v) {
    MooTrayWin* tray = (MooTrayWin*)moo_val_as_ptr(tray_v);
    if (!tray) return moo_bool(0);
    if (tray->menu) DestroyMenu(tray->menu);
    tray->menu = CreatePopupMenu();
    tray_cb_clear_all();
    return moo_bool(1);
}

/* =========================================================================
 * Submenus
 * ========================================================================= */

MooValue moo_tray_submenu_add(MooValue tray_v, MooValue label) {
    MooTrayWin* tray = (MooTrayWin*)moo_val_as_ptr(tray_v);
    if (!tray || !tray->menu) return moo_bool(0);
    HMENU sub = CreatePopupMenu();
    const char* l = (label.tag == MOO_STRING) ? MV_STR(label)->chars : "(?)";
    int need = MultiByteToWideChar(CP_UTF8, 0, l, -1, NULL, 0);
    wchar_t* wl = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)need);
    MultiByteToWideChar(CP_UTF8, 0, l, -1, wl, need);
    AppendMenuW(tray->menu, MF_POPUP | MF_STRING, (UINT_PTR)sub, wl);
    free(wl);
    return wrap_ptr((void*)sub);
}

MooValue moo_tray_menu_add_to(MooValue submenu, MooValue label, MooValue callback) {
    HMENU m = (HMENU)moo_val_as_ptr(submenu);
    if (!m) return moo_bool(0);
    UINT id = tray_alloc_id();
    const char* l = (label.tag == MOO_STRING) ? MV_STR(label)->chars : "(?)";
    int need = MultiByteToWideChar(CP_UTF8, 0, l, -1, NULL, 0);
    wchar_t* wl = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)need);
    MultiByteToWideChar(CP_UTF8, 0, l, -1, wl, need);
    AppendMenuW(m, MF_STRING, id, wl);
    free(wl);
    if (callback.tag == MOO_FUNC) tray_register_cb(id, tray_cb_new(callback));
    return moo_number((double)id);
}

MooValue moo_tray_separator_add_to(MooValue submenu) {
    HMENU m = (HMENU)moo_val_as_ptr(submenu);
    if (!m) return moo_bool(0);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    return moo_bool(1);
}

/* =========================================================================
 * Check-Items
 * ========================================================================= */

/* Wir brauchen eine Zuordnung item-id → HMENU, damit wert/set greifen
 * koennen, obwohl der Item-Handle als pure id rauskommt. Simple Map: pro id
 * speichern wir das HMENU in einem Parallel-Array. */
static HMENU g_item_menu[TRAY_CB_MAX];  /* Slot = id - 2000 */

static void record_item(UINT id, HMENU m) {
    UINT slot = id - 2000;
    if (slot < TRAY_CB_MAX) g_item_menu[slot] = m;
}
static HMENU item_menu(UINT id) {
    UINT slot = id - 2000;
    if (slot < TRAY_CB_MAX) return g_item_menu[slot];
    return NULL;
}

static MooValue check_item_add_to_menu(HMENU m, MooValue label, MooValue initial,
                                       MooValue callback) {
    UINT id = tray_alloc_id();
    const char* l = (label.tag == MOO_STRING) ? MV_STR(label)->chars : "(?)";
    int need = MultiByteToWideChar(CP_UTF8, 0, l, -1, NULL, 0);
    wchar_t* wl = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)need);
    MultiByteToWideChar(CP_UTF8, 0, l, -1, wl, need);
    UINT flags = MF_STRING;
    int init = (initial.tag == MOO_BOOL && MV_BOOL(initial)) ? 1 : 0;
    if (init) flags |= MF_CHECKED;
    AppendMenuW(m, flags, id, wl);
    free(wl);
    record_item(id, m);
    if (callback.tag == MOO_FUNC) tray_register_cb(id, tray_cb_new(callback));
    return moo_number((double)id);
}

MooValue moo_tray_check_add(MooValue tray_v, MooValue label, MooValue initial,
                            MooValue callback) {
    MooTrayWin* tray = (MooTrayWin*)moo_val_as_ptr(tray_v);
    if (!tray || !tray->menu) return moo_bool(0);
    return check_item_add_to_menu(tray->menu, label, initial, callback);
}

MooValue moo_tray_check_add_to(MooValue submenu, MooValue label,
                               MooValue initial, MooValue callback) {
    HMENU m = (HMENU)moo_val_as_ptr(submenu);
    if (!m) return moo_bool(0);
    return check_item_add_to_menu(m, label, initial, callback);
}

MooValue moo_tray_check_wert(MooValue check_item) {
    if (check_item.tag != MOO_NUMBER) return moo_bool(0);
    UINT id = (UINT)MV_NUM(check_item);
    HMENU m = item_menu(id);
    if (!m) return moo_bool(0);
    UINT st = GetMenuState(m, id, MF_BYCOMMAND);
    if (st == (UINT)-1) return moo_bool(0);
    return moo_bool((st & MF_CHECKED) ? 1 : 0);
}

MooValue moo_tray_check_set(MooValue check_item, MooValue wert) {
    if (check_item.tag != MOO_NUMBER) return moo_bool(0);
    UINT id = (UINT)MV_NUM(check_item);
    HMENU m = item_menu(id);
    if (!m) return moo_bool(0);
    int on = (wert.tag == MOO_BOOL && MV_BOOL(wert)) ? 1 : 0;
    CheckMenuItem(m, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    /* toggled-Callback manuell feuern (analog GTK set_active). */
    TrayCb* cb = tray_lookup_cb(id);
    if (cb && cb->v.tag == MOO_FUNC) {
        MooValue rv = moo_func_call_0(cb->v);
        moo_release(rv);
    }
    return moo_bool(1);
}

/* =========================================================================
 * Item-Manipulation
 * ========================================================================= */

MooValue moo_tray_item_aktiv(MooValue item, MooValue aktiv) {
    if (item.tag != MOO_NUMBER) return moo_bool(0);
    UINT id = (UINT)MV_NUM(item);
    HMENU m = item_menu(id);
    if (!m) return moo_bool(0);
    int on = (aktiv.tag == MOO_BOOL && MV_BOOL(aktiv)) ? 1 : 0;
    EnableMenuItem(m, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
    return moo_bool(1);
}

MooValue moo_tray_item_label_setze(MooValue item, MooValue label) {
    if (item.tag != MOO_NUMBER) return moo_bool(0);
    UINT id = (UINT)MV_NUM(item);
    HMENU m = item_menu(id);
    if (!m) return moo_bool(0);
    const char* l = (label.tag == MOO_STRING) ? MV_STR(label)->chars : "";
    int need = MultiByteToWideChar(CP_UTF8, 0, l, -1, NULL, 0);
    wchar_t* wl = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)need);
    MultiByteToWideChar(CP_UTF8, 0, l, -1, wl, need);
    MENUITEMINFOW mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    mi.fMask  = MIIM_STRING;
    mi.dwTypeData = wl;
    BOOL ok = SetMenuItemInfoW(m, id, FALSE, &mi);
    free(wl);
    return moo_bool(ok ? 1 : 0);
}

/* =========================================================================
 * Timer (wie moo_ui: SetTimer im Haupt-Loop)
 * ========================================================================= */

#define MOO_TRAY_TIMER_MAX 128
typedef struct {
    UINT_PTR id;
    TrayCb* cb;
} TrayTimer;
static TrayTimer g_tray_timers[MOO_TRAY_TIMER_MAX];

static void CALLBACK tray_timer_proc(HWND hwnd, UINT msg, UINT_PTR id, DWORD t) {
    (void)hwnd; (void)msg; (void)t;
    for (int i = 0; i < MOO_TRAY_TIMER_MAX; i++) {
        if (g_tray_timers[i].id == id && g_tray_timers[i].cb) {
            MooValue rv = moo_func_call_0(g_tray_timers[i].cb->v);
            moo_release(rv);
            return;
        }
    }
}

MooValue moo_tray_timer_add(MooValue interval_ms, MooValue callback) {
    int ms = (interval_ms.tag == MOO_NUMBER) ? (int)MV_NUM(interval_ms) : 1000;
    for (int i = 0; i < MOO_TRAY_TIMER_MAX; i++) {
        if (g_tray_timers[i].id == 0) {
            UINT_PTR tid = SetTimer(NULL, 0, (UINT)ms, tray_timer_proc);
            if (tid == 0) return moo_bool(0);
            g_tray_timers[i].id = tid;
            g_tray_timers[i].cb = tray_cb_new(callback);
            return moo_number((double)(uintptr_t)tid);
        }
    }
    return moo_bool(0);
}

MooValue moo_tray_timer_remove(MooValue timer_id) {
    if (timer_id.tag != MOO_NUMBER) return moo_bool(0);
    UINT_PTR id = (UINT_PTR)(uintptr_t)MV_NUM(timer_id);
    for (int i = 0; i < MOO_TRAY_TIMER_MAX; i++) {
        if (g_tray_timers[i].id == id) {
            KillTimer(NULL, id);
            tray_cb_free(g_tray_timers[i].cb);
            g_tray_timers[i].id = 0;
            g_tray_timers[i].cb = NULL;
            return moo_bool(1);
        }
    }
    return moo_bool(0);
}

/* =========================================================================
 * Event-Loop (Legacy-Wrapper)
 * ========================================================================= */

MooValue moo_tray_run(void) {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return moo_none();
}
