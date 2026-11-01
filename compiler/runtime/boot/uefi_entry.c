/* P011-F1: UEFI-Entry fuer moo-UEFI-Apps (aus E1-PoC reproduzierbar).
 *
 * efi_main(ImageHandle, SystemTable) ist der UEFI-Standard-Entry. Beim
 * Triple x86_64-unknown-windows / -uefi ist die MS-x64-Calling-Convention
 * der Default fuer ALLE Funktionen — kein per-Funktion-CC-Hack noetig.
 *
 * AUSGABE: Wir gehen NICHT ueber SystemTable->ConOut (UTF-16-Methodenaufruf),
 * sondern direkt ueber COM1 (Port-I/O, in UEFI Ring 0 erlaubt) + den QEMU
 * debugcon-Port 0xE9. Letzterer ist der zuverlaessigste Beweiskanal: OVMF
 * initialisiert COM1 selbst, weshalb ein Byte ohne LSR-Polling verloren geht.
 *
 * SSE: Im UEFI-Boot-Services-Environment bereits aktiv (Firmware nutzt es) —
 * anders als beim bare-Stage2-Pfad (D1), wo SSE erst manuell an muss.
 *
 * P7-T2 (MOO_UEFI_SURFACE): C ist hier nur Hardware-Adapter. Der Entry
 * ermittelt den GOP-Framebuffer (LocateProtocol), legt per AllocatePages eine
 * Heap-Arena fuer den vorhandenen Bare-Allocator (moo_bare_alloc.c,
 * kern_heap_init_c) an und stellt die kern_bild_*-Builtins bereit
 * (compiler/src/codegen.rs). Das Bild selbst zeichnet Moo mit den vorhandenen
 * surface_*-Builtins (moo_surface.c) und zeigt es mit kern_bild_zeige(surface);
 * hier wird nur RGBA8 nach RGBX/BGRX (GOP PixelFormat 0/1) mit Zeilen-Stride
 * PixelsPerScanLine umgesetzt.
 */
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

extern void moo_efi_haupt(void);

static void outb(u16 p, u8 v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }

/* COM1 mit LSR-Polling (0x3FD Bit5 = THR leer) — ohne Polling Byte-Verlust. */
static void com1(u8 c) { while (!(inb(0x3FD) & 0x20)) {} outb(0x3F8, c); }

/* Marker auf BEIDE Kanaele: debugcon 0xE9 (QEMU-stdio) + COM1 (-serial). */
static void marker(const char *s) {
    for (const char *p = s; *p; ++p) { outb(0xE9, (u8)*p); com1((u8)*p); }
}
/* Diagnose nur auf debugcon 0xE9 (COM1 bleibt fuer die Moo-Marker sauber). */
static void debug(const char *s) { for (const char *p = s; *p; ++p) outb(0xE9, (u8)*p); }
static void debug_hex(u64 v) {
    static const char hx[] = "0123456789abcdef";
    int i;
    outb(0xE9, '0'); outb(0xE9, 'x');
    for (i = 60; i >= 0; i -= 4) outb(0xE9, (u8)hx[(v >> i) & 0xf]);
}

#ifdef MOO_UEFI_SURFACE
/* MooValue-Layout wie moo_runtime.h ({u64 tag; u64 data}); der Entry bleibt
 * header-frei (freestanding). Konstruktoren kommen aus moo_bare.c, der
 * Pixelzugriff aus moo_surface.c, der Heap aus moo_bare_alloc.c. */
typedef struct { u64 tag; u64 data; } MooValue;
extern MooValue moo_number(double n);
extern MooValue moo_bool(_Bool b);
extern MooValue moo_string_new(const char *chars);   /* boot/uefi_rt.c */
extern int moo_uefi_net_init(void *system_table);    /* boot/uefi_net.c (P7-T9) */
extern int moo_uefi_net_sende(const u8 *text, u64 len);
/* Layout wie struct MooString (moo_runtime.h); Tag MOO_STRING = 1. */
typedef struct { int refcount; char *chars; int length; int capacity; } MooUefiString;
extern const u8 *moo_surface_rgba(MooValue value, int *out_w, int *out_h);
extern void kern_heap_init_c(u64 start, u64 ende);

/* UEFI x86-64 table prefixes and GOP layout, per UEFI 2.11.
 * Boot services remain live while this application is running. */
typedef struct { u64 signature; u32 revision, size, crc, reserved; } EfiHeader;
typedef struct { u32 a; u16 b, c; u8 d[8]; } EfiGuid;
typedef u64 (*EfiAllocatePages)(u32 type, u32 memory_type, u64 pages, u64 *memory);
typedef u64 (*EfiWaitForEvent)(u64 count, void **events, u64 *index);
typedef u64 (*EfiLocateProtocol)(EfiGuid *, void *, void **);
typedef u64 (*EfiFreePool)(void *buffer);
typedef u64 (*EfiHandleProtocol)(void *handle, EfiGuid *protocol, void **iface);
typedef u64 (*EfiLocateHandleBuffer)(u32 search_type, EfiGuid *protocol, void *key, u64 *count, void ***buffer);
typedef u64 (*EfiConnectController)(void *controller, void **drivers, void *remaining_path, u8 recursive);
typedef u64 (*EfiSetWatchdogTimer)(u64 timeout_s, u64 code, u64 data_size, u16 *data);
/* Boot-Services: Header, dann Funktionszeiger in Spezifikationsreihenfolge
 * (RaiseTPL 0, RestoreTPL 1, AllocatePages 2, FreePages 3, GetMemoryMap 4,
 * AllocatePool 5, FreePool 6, CreateEvent 7, SetTimer 8, WaitForEvent 9,
 * ..., LocateProtocol 37). */
typedef struct {
    EfiHeader hdr;
    void *raise_tpl, *restore_tpl;                       /* 0, 1 */
    EfiAllocatePages allocate_pages;                     /* 2 */
    void *free_pages, *get_memory_map, *allocate_pool;   /* 3, 4, 5 */
    EfiFreePool free_pool;                               /* 6 */
    void *create_event, *set_timer;                      /* 7, 8 */
    EfiWaitForEvent wait_for_event;                      /* 9 */
    void *signal_event, *close_event, *check_event;      /* 10, 11, 12 */
    void *install_pi, *reinstall_pi, *uninstall_pi;      /* 13, 14, 15 */
    EfiHandleProtocol handle_protocol;                   /* 16 */
    void *reserved, *register_notify, *locate_handle;    /* 17, 18, 19 */
    void *locate_device_path, *install_config_table;     /* 20, 21 */
    void *load_image, *start_image, *exit, *unload_image;/* 22..25 */
    void *exit_boot_services, *get_next_monotonic, *stall;/* 26, 27, 28 */
    EfiSetWatchdogTimer set_watchdog;                    /* 29 */
    EfiConnectController connect_controller;             /* 30 */
    void *disconnect_controller;                         /* 31 */
    void *open_protocol, *close_protocol, *open_protocol_info; /* 32..34 */
    void *protocols_per_handle;                          /* 35 */
    EfiLocateHandleBuffer locate_handle_buffer;          /* 36 */
    EfiLocateProtocol locate;                            /* 37 */
} EfiBootServices;
/* EFI_SIMPLE_TEXT_INPUT_PROTOCOL (ConIn): Reset, ReadKeyStroke, WaitForKey. */
typedef struct { u16 scan; u16 chr; } EfiInputKey;
typedef struct EfiTextIn EfiTextIn;
struct EfiTextIn {
    u64 (*reset)(EfiTextIn *, u8 extended);
    u64 (*read_key)(EfiTextIn *, EfiInputKey *key);
    void *wait_for_key;
};
typedef struct {
    EfiHeader hdr; void *vendor; u32 revision; void *in_handle; EfiTextIn *in;
    void *out_handle, *out, *err_handle, *err, *runtime;
    EfiBootServices *boot;
    u64 n_config;                                        /* NumberOfTableEntries */
    struct { EfiGuid guid; void *table; } *config;       /* EFI_CONFIGURATION_TABLE[] */
} EfiSystemTable;
/* EFI_ABSOLUTE_POINTER_PROTOCOL (USB-Tablet/Touch) und
 * EFI_SIMPLE_POINTER_PROTOCOL (relative Maus): Reset, GetState, WaitForInput, Mode. */
typedef struct { u64 min_x, min_y, min_z, max_x, max_y, max_z; u32 attributes; } EfiAbsPtrMode;
typedef struct { u64 x, y, z; u32 buttons; } EfiAbsPtrState;
typedef struct EfiAbsPtr EfiAbsPtr;
struct EfiAbsPtr {
    u64 (*reset)(EfiAbsPtr *, u8 extended);
    u64 (*get_state)(EfiAbsPtr *, EfiAbsPtrState *state);
    void *wait_for_input;
    EfiAbsPtrMode *mode;
};
typedef struct { u64 res_x, res_y, res_z; u8 left, right; } EfiSimplePtrMode;
typedef struct { int dx, dy, dz; u8 left, right; } EfiSimplePtrState;
typedef struct EfiSimplePtr EfiSimplePtr;
struct EfiSimplePtr {
    u64 (*reset)(EfiSimplePtr *, u8 extended);
    u64 (*get_state)(EfiSimplePtr *, EfiSimplePtrState *state);
    void *wait_for_input;
    EfiSimplePtrMode *mode;
};
#define EFI_NOT_READY 0x8000000000000006ull
typedef struct {
    u32 version, width, height, format;
    u32 masks[4], pixels_per_scanline;
} EfiGopInfo;
typedef struct {
    u32 max_mode, mode; EfiGopInfo *info; u64 info_size, base, size;
} EfiGopMode;
typedef struct { void *query, *set, *blt; EfiGopMode *mode; } EfiGop;

static EfiGop *moo_gop;

/* Reject unsupported pixel formats and invalid framebuffer bounds. */
static int moo_uefi_gop_init(void *system_table) {
    static EfiGuid guid = {0x9042a9de, 0x23dc, 0x4a38,
                           {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
    EfiSystemTable *st = (EfiSystemTable *)system_table;
    EfiGop *gop = 0;
    EfiGopMode *mode;
    EfiGopInfo *info;
    u64 bytes;
    if (!st || st->hdr.size < sizeof(*st) || !st->boot ||
        st->boot->hdr.size < sizeof(*st->boot) || !st->boot->locate ||
        st->boot->locate(&guid, 0, (void **)&gop) != 0 || !gop ||
        !(mode = gop->mode) || !(info = mode->info) ||
        mode->info_size < sizeof(*info) || !mode->base ||
        !info->width || !info->height || info->format > 1 ||
        info->pixels_per_scanline < info->width)
        return 0;
    bytes = (u64)info->pixels_per_scanline * (u64)info->height;
    if (bytes > (~(u64)0) / 4 || bytes * 4 > mode->size)
        return 0;
    moo_gop = gop;
    return 1;
}

/* Heap-Arena fuer den Bare-Allocator: EfiLoaderData-Seiten, bemessen nach
 * Framebuffergroesse (Surface + Guard-Bytes + Strings) plus 16 MiB Reserve. */
static int moo_uefi_heap_init(void *system_table, u64 fb_bytes) {
    EfiSystemTable *st = (EfiSystemTable *)system_table;
    u64 want = fb_bytes * 3u + ((u64)16u << 20);
    u64 pages = (want + 4095u) / 4096u;
    u64 mem = 0;
    if (!st->boot->allocate_pages ||
        st->boot->allocate_pages(0 /* AllocateAnyPages */, 2 /* EfiLoaderData */,
                                 pages, &mem) != 0 || !mem)
        return 0;
    kern_heap_init_c(mem, mem + pages * 4096u);
    return 1;
}

/* Source is tightly packed straight RGBA; GOP rows may contain padding.
 * Pixel format 0 is RGBX and 1 is BGRX. Reserved byte is written as zero.
 * Groessen duerfen abweichen: kopiert wird der gemeinsame Ausschnitt. */
int moo_uefi_present_rgba(const u8 *pixels, u32 width, u32 height) {
    EfiGopMode *mode;
    EfiGopInfo *info;
    volatile u32 *dst;
    u32 x, y, cw, ch;
    int bgr;
    if (!moo_gop || !pixels || !width || !height) return 0;
    mode = moo_gop->mode;
    info = mode->info;
    cw = width < info->width ? width : info->width;
    ch = height < info->height ? height : info->height;
    bgr = info->format == 1;
    dst = (volatile u32 *)(unsigned long long)mode->base;
    for (y = 0; y < ch; ++y) {
        const u8 *s = pixels + (u64)y * width * 4u;
        volatile u32 *d = dst + (u64)y * info->pixels_per_scanline;
        for (x = 0; x < cw; ++x, s += 4) {
            u32 r = s[0], g = s[1], b = s[2];
            d[x] = bgr ? ((r << 16) | (g << 8) | b) : ((b << 16) | (g << 8) | r);
        }
    }
    return 1;
}

/* ---------------------------------------------------------------------------
 * Eingabe (P7-T2): Tastatur ueber ConIn, Maus ueber Absolute- oder Simple-
 * Pointer-Protokoll. Alles wird von Moo gepollt (kern_*-Builtins), C haelt nur
 * den letzten Zeigerzustand. Fehlt ein physisches Zeigergeraet, ist die Maus
 * fail-closed (kern_maus_vorhanden = falsch), die Tastatur geht weiter.
 * BEFUND QEMU/OVMF (P7-T2): OVMF bringt keinen USB-/PS2-Zeigertreiber mit;
 * nach ConnectController ueber alle Handles existieren nur die ConSplitter-
 * Instanzen (Mode 0..0x10000) auf dem ConIn-Handle, die ohne Geraet nie
 * Daten liefern. Darum werden sie nicht gezaehlt -> MAUS-FEHLT in QEMU.
 * ------------------------------------------------------------------------- */
#define MOO_MAUS_MAX 4
static EfiSystemTable *moo_st;
static EfiTextIn *moo_kbd;
static EfiAbsPtr *moo_abs[MOO_MAUS_MAX];
static EfiSimplePtr *moo_rel[MOO_MAUS_MAX];
static u32 moo_n_abs, moo_n_rel;
static long long moo_maus_px, moo_maus_py;
static int moo_maus_links;

/* Alle Handles mit einem Protokoll holen. LocateProtocol liefert nur die
 * erste Instanz - in OVMF der ConSplitter (Mode 0..0x10000), der ohne
 * ConIn-Eintrag kein Geraet fuehrt und nie Daten liefert; die physischen
 * USB-Geraete haengen an eigenen Handles. Darum alle Instanzen einsammeln. */
static u64 moo_uefi_handles(EfiGuid *guid, void ***handles, u64 *count) {
    *handles = 0; *count = 0;
    if (!moo_st->boot->locate_handle_buffer) return 1;
    return moo_st->boot->locate_handle_buffer(guid ? 2u /* ByProtocol */ : 0u /* AllHandles */,
                                              guid, 0, count, handles);
}

/* OVMF bindet USB-HID-Geraete nicht von selbst an ihre Treiber (nur Konsolen
 * aus ConIn werden verbunden). Wie 'connect -r' der UEFI-Shell: alle Handles
 * rekursiv verbinden, damit xHCI/USB-Bus enumerieren und UsbMouse*Dxe ihre
 * Pointer-Protokolle installieren. Fehler einzelner Handles sind egal. */
static void moo_uefi_alles_verbinden(void) {
    void **handles;
    u64 count, i;
    if (!moo_st->boot->connect_controller) return;
    if (moo_uefi_handles(0, &handles, &count) != 0) return;
    for (i = 0; i < count; ++i) moo_st->boot->connect_controller(handles[i], 0, 0, 1);
    if (handles && moo_st->boot->free_pool) moo_st->boot->free_pool(handles);
    debug("MOO-UEFI-CONNECT handles "); debug_hex(count); debug("\n");
}

static int moo_uefi_eingabe_init(void *system_table) {
    static EfiGuid abs_guid = {0x8d59d32b, 0xc655, 0x4ae9,
                               {0x9b,0x15,0xf2,0x59,0x04,0x99,0x2a,0x43}};
    static EfiGuid rel_guid = {0x31878c87, 0x0b75, 0x11d5,
                               {0x9a,0x4f,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
    EfiSystemTable *st = (EfiSystemTable *)system_table;
    void **handles;
    u64 count, i;
    moo_st = st;
    moo_kbd = (st->in && st->in->read_key) ? st->in : 0;
    moo_uefi_alles_verbinden();
    if (moo_uefi_handles(&abs_guid, &handles, &count) == 0) {
        for (i = 0; i < count && moo_n_abs < MOO_MAUS_MAX; ++i) {
            void *p = 0;
            EfiAbsPtr *a;
            /* ConSplitter (virtuelles Geraet auf dem ConIn-Handle) ueberspringen:
             * ohne eingetragenes Geraet liefert er nie Daten. */
            if (handles[i] == st->in_handle) continue;
            if (st->boot->handle_protocol(handles[i], &abs_guid, &p) != 0 || !p) continue;
            a = (EfiAbsPtr *)p;
            if (!a->get_state || !a->mode || a->mode->max_x <= a->mode->min_x ||
                a->mode->max_y <= a->mode->min_y) continue;
            moo_abs[moo_n_abs++] = a;
            debug("MOO-UEFI-MAUS-ABS x "); debug_hex(a->mode->min_x); debug(".."); debug_hex(a->mode->max_x);
            debug(" y "); debug_hex(a->mode->min_y); debug(".."); debug_hex(a->mode->max_y); debug("\n");
        }
        if (handles && st->boot->free_pool) st->boot->free_pool(handles);
    }
    if (moo_uefi_handles(&rel_guid, &handles, &count) == 0) {
        for (i = 0; i < count && moo_n_rel < MOO_MAUS_MAX; ++i) {
            void *p = 0;
            EfiSimplePtr *r;
            if (handles[i] == st->in_handle) continue;
            if (st->boot->handle_protocol(handles[i], &rel_guid, &p) != 0 || !p) continue;
            r = (EfiSimplePtr *)p;
            if (!r->get_state) continue;
            moo_rel[moo_n_rel++] = r;
            debug("MOO-UEFI-MAUS-REL\n");
        }
        if (handles && st->boot->free_pool) st->boot->free_pool(handles);
    }
    if (moo_gop) {
        moo_maus_px = moo_gop->mode->info->width / 2;
        moo_maus_py = moo_gop->mode->info->height / 2;
    }
    return moo_n_abs + moo_n_rel > 0;
}

/* UEFI-Scan-Codes/Unicode -> ui_moo-Tastennamen (stdlib/ui_moo_kern.moos:
 * "Tab", "Return", "Escape", "Up", "Down", "Left", "Right", "BackSpace",
 * "Home", "End", "space"); druckbares ASCII als Einzelzeichen. */
static const char *moo_uefi_tastenname(EfiInputKey k, char *einzel) {
    switch (k.scan) {
    case 0x01: return "Up";
    case 0x02: return "Down";
    case 0x03: return "Right";
    case 0x04: return "Left";
    case 0x05: return "Home";
    case 0x06: return "End";
    case 0x08: return "Delete";
    case 0x09: return "Page_Up";
    case 0x0a: return "Page_Down";
    case 0x17: return "Escape";
    default: break;
    }
    switch (k.chr) {
    case 0x09: return "Tab";
    case 0x0d: case 0x0a: return "Return";
    case 0x08: return "BackSpace";
    case 0x20: return "space";
    default: break;
    }
    if (k.chr >= 0x21 && k.chr <= 0x7e) { einzel[0] = (char)k.chr; einzel[1] = 0; return einzel; }
    return "";
}

MooValue kern_taste_lesen(void) {
    EfiInputKey k = {0, 0};
    char einzel[2];
    if (!moo_kbd || moo_kbd->read_key(moo_kbd, &k) != 0) return moo_string_new("");
    return moo_string_new(moo_uefi_tastenname(k, einzel));
}

MooValue kern_maus_vorhanden(void) { return moo_bool(moo_n_abs + moo_n_rel > 0); }

/* Neuen Zeigerzustand uebernehmen (geklemmt); 1, wenn sich etwas aenderte. */
static int moo_uefi_maus_setze(long long nx, long long ny, int links, long long w, long long h) {
    int geaendert;
    if (nx < 0) nx = 0;
    if (nx >= w) nx = w - 1;
    if (ny < 0) ny = 0;
    if (ny >= h) ny = h - 1;
    geaendert = nx != moo_maus_px || ny != moo_maus_py || links != moo_maus_links;
    moo_maus_px = nx; moo_maus_py = ny; moo_maus_links = links;
    return geaendert;
}

/* Alle anstehenden Zustaende einer Instanz einlesen (GetState bis EFI_NOT_READY).
 * Absolut: auf den Framebuffer skaliert. */
static int moo_uefi_maus_abs(EfiAbsPtr *a, long long w, long long h) {
    int geaendert = 0;
    for (;;) {
        EfiAbsPtrState s;
        long long nx, ny;
        u64 rc = a->get_state(a, &s);
        if (rc != 0) {
            if (rc != EFI_NOT_READY) { debug("MOO-UEFI-MAUS-GETSTATE-ERR "); debug_hex(rc); debug("\n"); }
            break;
        }
        debug("MOO-UEFI-MAUS-ROH "); debug_hex(s.x); debug(" "); debug_hex(s.y);
        debug(" "); debug_hex(s.buttons); debug("\n");
        nx = (long long)((s.x - a->mode->min_x) * (u64)(w - 1) / (a->mode->max_x - a->mode->min_x));
        ny = (long long)((s.y - a->mode->min_y) * (u64)(h - 1) / (a->mode->max_y - a->mode->min_y));
        geaendert |= moo_uefi_maus_setze(nx, ny, (s.buttons & 1u) != 0, w, h);
    }
    return geaendert;
}
/* Relativ: Deltas aufaddieren. */
static int moo_uefi_maus_rel(EfiSimplePtr *r, long long w, long long h) {
    int geaendert = 0;
    for (;;) {
        EfiSimplePtrState s;
        if (r->get_state(r, &s) != 0) break;
        geaendert |= moo_uefi_maus_setze(moo_maus_px + s.dx, moo_maus_py + s.dy, s.left != 0, w, h);
    }
    return geaendert;
}

/* Alle Zeigerinstanzen pollen; wahr, wenn sich Position oder Taste aenderte. */
MooValue kern_maus_lesen(void) {
    int geaendert = 0;
    u32 i;
    long long w = moo_gop ? (long long)moo_gop->mode->info->width : 1;
    long long h = moo_gop ? (long long)moo_gop->mode->info->height : 1;
    for (i = 0; i < moo_n_abs; ++i) geaendert |= moo_uefi_maus_abs(moo_abs[i], w, h);
    for (i = 0; i < moo_n_rel; ++i) geaendert |= moo_uefi_maus_rel(moo_rel[i], w, h);
    return moo_bool(geaendert != 0);
}
MooValue kern_maus_x(void) { return moo_number((double)moo_maus_px); }
MooValue kern_maus_y(void) { return moo_number((double)moo_maus_py); }
MooValue kern_maus_gedrueckt(void) { return moo_bool(moo_maus_links != 0); }

/* Blockiert bis Tastatur oder ein Zeiger ein Ereignis melden (WaitForEvent auf
 * ConIn.WaitForKey und allen Pointer.WaitForInput). Ohne Ereignisquelle oder
 * bei Fehler sofort falsch zurueck (Moo-Schleife pollt dann weiter). */
MooValue kern_eingabe_warten(void) {
    void *ev[1 + 2 * MOO_MAUS_MAX];
    u64 n = 0, idx = 0, rc;
    u32 i;
    static int gemeldet;
    if (moo_kbd && moo_kbd->wait_for_key) ev[n++] = moo_kbd->wait_for_key;
    for (i = 0; i < moo_n_abs; ++i) if (moo_abs[i]->wait_for_input) ev[n++] = moo_abs[i]->wait_for_input;
    for (i = 0; i < moo_n_rel; ++i) if (moo_rel[i]->wait_for_input) ev[n++] = moo_rel[i]->wait_for_input;
    if (!n || !moo_st || !moo_st->boot->wait_for_event) return moo_bool(0);
    rc = moo_st->boot->wait_for_event(n, ev, &idx);
    if (!gemeldet) {
        gemeldet = 1;
        debug("MOO-UEFI-WARTEN rc "); debug_hex(rc); debug(" n "); debug_hex(n);
        debug(" idx "); debug_hex(idx); debug("\n");
    }
    return moo_bool(rc == 0);
}

/* P7-T9: Netzwerk-Konsole. kern_netz_sende(text) schickt eine Moo-Textzeile
 * als UDP-Broadcast (boot/uefi_net.c). Ohne SNP fail-closed (falsch). */
static int moo_netz_da;
MooValue kern_netz_sende(MooValue text) {
    const MooUefiString *s;
    if (!moo_netz_da || text.tag != 1u) return moo_bool(0);
    s = (const MooUefiString *)(unsigned long long)text.data;
    if (!s || !s->chars || s->length <= 0) return moo_bool(0);
    return moo_bool(moo_uefi_net_sende((const u8 *)s->chars, (u64)s->length) != 0);
}

/* P7-T9: Fremde UEFI-Treiber vom eigenen Datentraeger laden, fuer Geraete,
 * deren Treiber die Firmware nicht mitbringt (N100: AMI ohne I226-UNDI).
 * Weg: LoadedImage(eigenes Image) -> DeviceHandle -> SimpleFileSystem ->
 * Verzeichnis \EFI\MOO\treiber lesen -> je *.efi AllocatePool + Read ->
 * LoadImage (aus dem Puffer) -> StartImage. Danach verbindet der vorhandene
 * Lauf moo_uefi_alles_verbinden() alle Controller neu, so dass die Treiber
 * ihre Protokolle (z. B. SNP) installieren. Fehlt das Verzeichnis oder steht
 * keine .efi-Datei darin: fail-closed, Status "TREIBER FEHLT".
 * Layouts nach UEFI 2.11 (EFI_LOADED_IMAGE_PROTOCOL, EFI_FILE_PROTOCOL,
 * EFI_FILE_INFO: FileSize @8, Attribute @72, FileName @80). */
typedef struct EfiFile EfiFile;
struct EfiFile {
    u64 revision;
    u64 (*open)(EfiFile *, EfiFile **neu, u16 *name, u64 modus, u64 attribute);
    u64 (*close)(EfiFile *);
    void *loeschen;
    u64 (*read)(EfiFile *, u64 *groesse, void *puffer);
};
typedef struct { u64 revision; u64 (*open_volume)(void *self, EfiFile **wurzel); } EfiSimpleFs;
typedef struct { u32 revision; void *parent, *system_table, *device_handle; } EfiLoadedImage;
typedef u64 (*EfiAllocatePool)(u32 typ, u64 groesse, void **puffer);
typedef u64 (*EfiLoadImage)(u8 boot_policy, void *parent, void *pfad, void *quelle,
                             u64 groesse, void **bild);
typedef u64 (*EfiStartImage)(void *bild, u64 *exit_groesse, u16 **exit_daten);
#define MOO_TREIBER_MAX 8u
#define MOO_TREIBER_MAX_BYTES (16ull << 20)
static u32 moo_treiber_ok, moo_treiber_fehler;

static int moo_uefi_ist_efi(const u16 *n) {
    u64 l = 0;
    while (n[l]) ++l;
    if (l < 5) return 0;
    n += l - 4;
    return n[0] == '.' && (n[1] | 0x20) == 'e' && (n[2] | 0x20) == 'f' && (n[3] | 0x20) == 'i';
}

/* Geraetepfad des Volumes + Media-FilePath-Knoten (Typ 4, Subtyp 4) fuer
 * \EFI\MOO\treiber\<name>, abgeschlossen mit dem Endknoten (0x7F/0xFF).
 * Ohne Pfad setzt LoadImage kein DeviceHandle im LoadedImage des Treibers;
 * Treiber wie iPXE (efi_init) brechen dann mit EFI_INVALID_PARAMETER ab.
 * Knoten sind nicht ausgerichtet, darum byteweise schreiben. Aufrufer gibt
 * den Puffer mit FreePool frei. */
static u8 *moo_uefi_treiber_pfad(EfiSystemTable *st, void *geraet, const u16 *name) {
    static EfiGuid dp_guid = {0x09576e91, 0x6d3f, 0x11d2,
                              {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
    static const u16 vor[] = u"\\EFI\\MOO\\treiber\\";
    EfiAllocatePool alloc = (EfiAllocatePool)st->boot->allocate_pool;
    u8 *dp = 0, *p, *neu = 0;
    u64 basis, vl = 0, nl = 0, knoten, i, o;
    if (!geraet || st->boot->handle_protocol(geraet, &dp_guid, (void **)&dp) != 0 || !dp) return 0;
    for (p = dp; !(p[0] == 0x7f && p[1] == 0xff); ) {
        u64 l = (u64)p[2] | ((u64)p[3] << 8);
        if (l < 4) return 0;
        p += l;
    }
    basis = (u64)(p - dp);
    while (vor[vl]) ++vl;
    while (name[nl]) ++nl;
    knoten = 4 + (vl + nl + 1) * 2;
    if (knoten > 0xffffu) return 0;
    if (alloc(2u, basis + knoten + 4, (void **)&neu) != 0 || !neu) return 0;
    for (i = 0; i < basis; ++i) neu[i] = dp[i];
    o = basis;
    neu[o++] = 4; neu[o++] = 4; neu[o++] = (u8)(knoten & 0xff); neu[o++] = (u8)(knoten >> 8);
    for (i = 0; i < vl; ++i) { neu[o++] = (u8)(vor[i] & 0xff); neu[o++] = (u8)(vor[i] >> 8); }
    for (i = 0; i < nl; ++i) { neu[o++] = (u8)(name[i] & 0xff); neu[o++] = (u8)(name[i] >> 8); }
    neu[o++] = 0; neu[o++] = 0;
    neu[o++] = 0x7f; neu[o++] = 0xff; neu[o++] = 4; neu[o] = 0;
    return neu;
}

/* Eine Treiberdatei laden und starten; 1 = gestartet. */
static int moo_uefi_treiber_starte(void *image, EfiSystemTable *st, void *geraet, EfiFile *dir,
                                   u16 *name, u64 groesse) {
    EfiAllocatePool alloc = (EfiAllocatePool)st->boot->allocate_pool;
    EfiLoadImage lade = (EfiLoadImage)st->boot->load_image;
    EfiStartImage starte = (EfiStartImage)st->boot->start_image;
    EfiFile *f = 0;
    void *puffer = 0, *bild = 0;
    u64 n = groesse, status;
    if (groesse == 0 || groesse > MOO_TREIBER_MAX_BYTES) return 0;
    if (dir->open(dir, &f, name, 1u /* READ */, 0) != 0 || !f) return 0;
    if (alloc(2u /* EfiLoaderData */, groesse, &puffer) != 0 || !puffer) { f->close(f); return 0; }
    status = f->read(f, &n, puffer);
    f->close(f);
    if (status != 0 || n != groesse) { st->boot->free_pool(puffer); return 0; }
    {
        u8 *pfad = moo_uefi_treiber_pfad(st, geraet, name);
        if (!pfad) debug("MOO-UEFI-TREIBER ohne Geraetepfad\n");
        status = lade(0, image, pfad, puffer, groesse, &bild);
        if (pfad) st->boot->free_pool(pfad);
    }
    st->boot->free_pool(puffer);
    if (status != 0 || !bild) {
        debug("MOO-UEFI-TREIBER LoadImage "); debug_hex(status); debug("\n");
        return 0;
    }
    /* Liefert der Einstieg eines Boot-Service-Treibers einen Fehler, entlaedt
     * die Firmware das Image selbst (UEFI StartImage). */
    status = starte(bild, 0, 0);
    debug("MOO-UEFI-TREIBER StartImage "); debug_hex(status); debug("\n");
    return status == 0;
}

static void moo_uefi_treiber_laden(void *image, EfiSystemTable *st) {
    static EfiGuid li_guid = {0x5b1b31a1, 0x9562, 0x11d2,
                              {0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
    static EfiGuid fs_guid = {0x964e5b22, 0x6459, 0x11d2,
                              {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
    EfiLoadedImage *li = 0;
    EfiSimpleFs *fs = 0;
    EfiFile *wurzel = 0, *dir = 0;
    u64 info[80];               /* EFI_FILE_INFO + Name, 8-Byte-ausgerichtet */
    u32 versuche = 0;
    if (!image || !st->boot->load_image || !st->boot->start_image || !st->boot->allocate_pool)
        return;
    if (st->boot->handle_protocol(image, &li_guid, (void **)&li) != 0 || !li || !li->device_handle)
        return;
    if (st->boot->handle_protocol(li->device_handle, &fs_guid, (void **)&fs) != 0 || !fs)
        return;
    if (fs->open_volume(fs, &wurzel) != 0 || !wurzel) return;
    if (wurzel->open(wurzel, &dir, (u16 *)u"\\EFI\\MOO\\treiber", 1u, 0) != 0 || !dir) {
        wurzel->close(wurzel);
        return;
    }
    for (;;) {
        u64 groesse = sizeof info;
        const u8 *e = (const u8 *)info;
        u16 *name = (u16 *)(info + 10);  /* FileName @80 */
        if (dir->read(dir, &groesse, info) != 0 || groesse <= 80) break;
        if (*(const u64 *)(e + 72) & 0x10u /* EFI_FILE_DIRECTORY */) continue;
        if (!moo_uefi_ist_efi(name)) continue;
        if (versuche++ >= MOO_TREIBER_MAX) break;
        if (moo_uefi_treiber_starte(image, st, li->device_handle, dir, name,
                                    info[1] /* FileSize @8 */))
            ++moo_treiber_ok;
        else
            ++moo_treiber_fehler;
    }
    dir->close(dir);
    wurzel->close(wurzel);
    debug("MOO-UEFI-TREIBER ok "); debug_hex(moo_treiber_ok);
    debug(" fehler "); debug_hex(moo_treiber_fehler); debug("\n");
}

/* P7-T14: kern_acpi_rsdp() -> Adresse der ACPI-RSDP aus der UEFI-
 * Konfigurationstabelle (ACPI 2.0-GUID bevorzugt, sonst ACPI 1.0), 0 wenn
 * keine da ist. Alles Weitere (XSDT, MCFG, ECAM, PCI) liest Moo selbst. */
MooValue kern_acpi_rsdp(void) {
    static const EfiGuid acpi20 = {0x8868e871, 0xe4f1, 0x11d3,
                                   {0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};
    static const EfiGuid acpi10 = {0xeb9d2d30, 0x2d88, 0x11d3,
                                   {0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}};
    u64 i, alt = 0;
    if (!moo_st || !moo_st->config) return moo_number(0.0);
    for (i = 0; i < moo_st->n_config; ++i) {
        const EfiGuid *g = &moo_st->config[i].guid;
        u64 a = (u64)moo_st->config[i].table;
        int j, gleich20 = g->a == acpi20.a && g->b == acpi20.b && g->c == acpi20.c;
        int gleich10 = g->a == acpi10.a && g->b == acpi10.b && g->c == acpi10.c;
        for (j = 0; j < 8; ++j) {
            if (g->d[j] != acpi20.d[j]) gleich20 = 0;
            if (g->d[j] != acpi10.d[j]) gleich10 = 0;
        }
        if (gleich20 && a) return moo_number((double)a);
        if (gleich10 && a && !alt) alt = a;
    }
    return moo_number((double)alt);
}

/* P7-T15: Hilfen fuer Treiber in Moo. Zahlen kommen als MooValue (double). */
extern MooValue moo_string_new_len(const char *chars, int len);   /* boot/uefi_rt.c */
static double moo_uefi_zahl(MooValue v) {
    double d;
    __builtin_memcpy(&d, &v.data, sizeof d);
    return d == d ? d : 0.0;     /* NaN -> 0 */
}

/* kern_warte_us(us): Boot-Services Stall, begrenzt auf 10 s je Aufruf. */
MooValue kern_warte_us(MooValue us) {
    typedef u64 (*EfiStall)(u64 mikrosekunden);
    double d = moo_uefi_zahl(us);
    u64 n;
    if (!moo_st || !moo_st->boot->stall) return moo_bool(0);
    if (!(d > 0.0)) return moo_bool(1);
    n = d > 10000000.0 ? 10000000ull : (u64)d;
    ((EfiStall)moo_st->boot->stall)(n);
    return moo_bool(1);
}

/* kern_text_in_puffer(text, ziel, max): Bytes eines Moo-Texts nach ziel
 * kopieren (hoechstens max, max <= 65536). Rueckgabe: kopierte Laenge. */
MooValue kern_text_in_puffer(MooValue text, MooValue ziel, MooValue max) {
    const MooUefiString *s;
    double z = moo_uefi_zahl(ziel), m = moo_uefi_zahl(max);
    u8 *p;
    int i, n;
    if (text.tag != 1u || !(z > 0.0) || !(m > 0.0) || m > 65536.0) return moo_number(0.0);
    s = (const MooUefiString *)(unsigned long long)text.data;
    if (!s || !s->chars || s->length <= 0) return moo_number(0.0);
    p = (u8 *)(unsigned long long)z;
    n = s->length < (int)m ? s->length : (int)m;
    for (i = 0; i < n; ++i) p[i] = (u8)s->chars[i];
    return moo_number((double)n);
}

/* kern_text_aus_puffer(quelle, laenge): neuer Moo-Text aus Bytes (laenge <= 4096). */
MooValue kern_text_aus_puffer(MooValue quelle, MooValue laenge) {
    double q = moo_uefi_zahl(quelle), l = moo_uefi_zahl(laenge);
    if (!(q > 0.0) || l < 0.0 || l > 4096.0) return moo_string_new("");
    return moo_string_new_len((const char *)(unsigned long long)q, (int)l);
}

/* P7-T10: kern_bild_starten(adresse, laenge) startet ein EFI-Image, das schon
 * im Speicher liegt (Netz-Lader). LoadImage aus dem Puffer (ohne Geraetepfad,
 * Eltern-Image = diese App), danach StartImage. Rueckgabe: 0 = gestartet und
 * mit Erfolg zurueckgekehrt, 1 = LoadImage abgelehnt, 2 = StartImage meldete
 * einen Fehler, -1 = ungueltige Argumente. Diagnose auf 0xE9. */
static void *moo_image_handle;
MooValue kern_bild_starten(MooValue adresse, MooValue laenge) {
    EfiLoadImage lade;
    EfiStartImage starte;
    double a = moo_uefi_zahl(adresse), l = moo_uefi_zahl(laenge);
    void *bild = 0;
    u64 status;
    if (!moo_st || !moo_image_handle || !moo_st->boot->load_image || !moo_st->boot->start_image)
        return moo_number(-1.0);
    if (!(a > 0.0) || !(l >= 64.0) || l > 268435456.0) return moo_number(-1.0);
    lade = (EfiLoadImage)moo_st->boot->load_image;
    starte = (EfiStartImage)moo_st->boot->start_image;
    status = lade(0, moo_image_handle, 0, (void *)(u64)a, (u64)l, &bild);
    debug("MOO-UEFI-BILD LoadImage "); debug_hex(status); debug("\n");
    if (status != 0 || !bild) return moo_number(1.0);
    marker("MOO-UEFI-BILD-START\n");
    status = starte(bild, 0, 0);
    debug("MOO-UEFI-BILD StartImage "); debug_hex(status); debug("\n");
    return moo_number(status == 0 ? 0.0 : 2.0);
}

/* kern_treiber_status(): Moo-Text fuer die Statuszeile. */
MooValue kern_treiber_status(void) {
    if (moo_treiber_ok) return moo_string_new("TREIBER OK");
    if (moo_treiber_fehler) return moo_string_new("TREIBER FEHLER");
    return moo_string_new("TREIBER FEHLT");
}

/* kern_bild_*-Builtins (compiler/src/codegen.rs, MooValue-Konvention wie die
 * uebrigen kern_*). Breite/Hoehe des Framebuffers; Zeige kopiert eine Surface. */
MooValue kern_bild_breite(void) {
    return moo_number(moo_gop ? (double)moo_gop->mode->info->width : 0.0);
}
MooValue kern_bild_hoehe(void) {
    return moo_number(moo_gop ? (double)moo_gop->mode->info->height : 0.0);
}
MooValue kern_bild_zeige(MooValue surface) {
    int w = 0, h = 0;
    const u8 *px = moo_surface_rgba(surface, &w, &h);
    if (!px || w <= 0 || h <= 0) return moo_bool(0);
    return moo_bool(moo_uefi_present_rgba(px, (u32)w, (u32)h) != 0);
}
#endif

u64 efi_main(void *ImageHandle, void *SystemTable) {
    (void)ImageHandle; (void)SystemTable;
    marker("MOO-UEFI-ENTRY\n");
#ifdef MOO_UEFI_SURFACE
    if (!moo_uefi_gop_init(SystemTable)) {
        marker("MOO-UEFI-GOP-FAIL\n");
        return 1;
    }
    marker("MOO-UEFI-GOP-OK\n");
    /* Der Boot-Manager setzt vor dem Start einer Boot-Option einen 5-Minuten-
     * Watchdog; ohne ExitBootServices oder SetWatchdogTimer(0) setzt die
     * Firmware das System zurueck (Befund N100: Neustart nach einigen Minuten).
     * Timeout 0 schaltet ihn ab (UEFI 2.x, EFI_BOOT_SERVICES.SetWatchdogTimer). */
    {
        EfiSystemTable *st = (EfiSystemTable *)SystemTable;
        if (st->boot->set_watchdog && st->boot->set_watchdog(0, 0, 0, 0) == 0)
            marker("MOO-UEFI-WATCHDOG-AUS\n");
        else
            marker("MOO-UEFI-WATCHDOG-FAIL\n");
    }
    if (!moo_uefi_heap_init(SystemTable, moo_gop->mode->size)) {
        marker("MOO-UEFI-HEAP-FAIL\n");
        return 1;
    }
    marker("MOO-UEFI-HEAP-OK\n");
    /* Treiber VOR dem Connect-Lauf (in moo_uefi_eingabe_init) laden, damit
     * sie beim rekursiven ConnectController ihre Geraete binden. */
    moo_image_handle = ImageHandle;
    moo_uefi_treiber_laden(ImageHandle, (EfiSystemTable *)SystemTable);
    marker(moo_treiber_ok ? "MOO-UEFI-TREIBER-OK\n"
           : moo_treiber_fehler ? "MOO-UEFI-TREIBER-FEHLER\n" : "MOO-UEFI-TREIBER-FEHLT\n");
    marker(moo_uefi_eingabe_init(SystemTable) ? "MOO-UEFI-MAUS-OK\n"
                                              : "MOO-UEFI-MAUS-FEHLT\n");
    moo_netz_da = moo_uefi_net_init(SystemTable);
    marker(moo_netz_da ? "MOO-UEFI-NETZ-OK\n" : "MOO-UEFI-NETZ-FEHLT\n");
#endif
    moo_efi_haupt();            /* in moo kompilierter Code */
    marker("MOO-UEFI-OK\n");    /* nach moo -> beweist saubere Rueckkehr */
    for (;;) { __asm__ volatile("hlt"); }
    return 0;
}
