#ifndef MOO_UEFI_ABI_H
#define MOO_UEFI_ABI_H

/* x86-64 UEFI-ABI. Der Windows-Target-Triple setzt die MS-x64-Konvention.
 * Tabellen und Signaturen: UEFI 2.11, EFI System Table, Boot Services und
 * Simple Text Input / Simple Text Input Ex. Nicht aufgerufene Funktionen
 * belegen einen Zeigerplatz. */
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

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
typedef u64 (*EfiStall)(u64 microseconds);

typedef struct {
    EfiHeader hdr;
    void *raise_tpl, *restore_tpl;                       /* 0, 1 */
    EfiAllocatePages allocate_pages;                     /* 2 */
    void *free_pages, *get_memory_map, *allocate_pool;    /* 3..5 */
    EfiFreePool free_pool;                               /* 6 */
    void *create_event, *set_timer;                      /* 7, 8 */
    EfiWaitForEvent wait_for_event;                      /* 9 */
    void *signal_event, *close_event, *check_event;       /* 10..12 */
    void *install_pi, *reinstall_pi, *uninstall_pi;       /* 13..15 */
    EfiHandleProtocol handle_protocol;                   /* 16 */
    void *reserved, *register_notify, *locate_handle;     /* 17..19 */
    void *locate_device_path, *install_config_table;      /* 20, 21 */
    void *load_image, *start_image, *exit, *unload_image;  /* 22..25 */
    void *exit_boot_services, *get_next_monotonic;         /* 26, 27 */
    EfiStall stall;                                      /* 28 */
    EfiSetWatchdogTimer set_watchdog;                    /* 29 */
    EfiConnectController connect_controller;             /* 30 */
    void *disconnect_controller;                        /* 31 */
    void *open_protocol, *close_protocol, *open_protocol_info; /* 32..34 */
    void *protocols_per_handle;                          /* 35 */
    EfiLocateHandleBuffer locate_handle_buffer;          /* 36 */
    EfiLocateProtocol locate;                            /* 37 */
} EfiBootServices;

typedef struct { u16 scan; u16 chr; } EfiInputKey;
typedef struct EfiTextIn EfiTextIn;
struct EfiTextIn {
    u64 (*reset)(EfiTextIn *, u8 extended);
    u64 (*read_key)(EfiTextIn *, EfiInputKey *key);
    void *wait_for_key;
};

typedef struct { u32 shift; u8 toggle; } EfiKeyState;
typedef struct { EfiInputKey key; EfiKeyState state; } EfiKeyData;
typedef struct EfiTextInEx EfiTextInEx;
struct EfiTextInEx {
    u64 (*reset)(EfiTextInEx *, u8 extended);
    u64 (*read_key)(EfiTextInEx *, EfiKeyData *key);
    void *wait_for_key;
    void *set_state, *register_key_notify, *unregister_key_notify;
};

#define EFI_SHIFT_STATE_VALID 0x80000000u
#define EFI_RIGHT_SHIFT_PRESSED 0x00000001u
#define EFI_LEFT_SHIFT_PRESSED 0x00000002u
#define EFI_NOT_READY 0x8000000000000006ull

typedef struct {
    EfiHeader hdr; void *vendor; u32 revision; void *in_handle; EfiTextIn *in;
    void *out_handle, *out, *err_handle, *err, *runtime;
    EfiBootServices *boot;
    u64 n_config;
    struct { EfiGuid guid; void *table; } *config;
} EfiSystemTable;

_Static_assert(sizeof(EfiGuid) == 16 && sizeof(EfiHeader) == 24, "UEFI Header/GUID ABI");
_Static_assert(__builtin_offsetof(EfiSystemTable, boot) == 96 &&
               sizeof(EfiSystemTable) == 120, "UEFI System Table ABI");
_Static_assert(__builtin_offsetof(EfiBootServices, stall) == 248 &&
               __builtin_offsetof(EfiBootServices, locate) == 320, "UEFI Boot Services ABI");
_Static_assert(__builtin_offsetof(EfiKeyData, state) == 4 &&
               sizeof(EfiKeyData) == 12 &&
               __builtin_offsetof(EfiTextInEx, wait_for_key) == 16, "UEFI Text Input Ex ABI");

int moo_uefi_net_init(EfiSystemTable *system_table);
int moo_uefi_net_sende(const u8 *text, u64 len);

#ifndef MOO_UEFI_DEBUGCON
#define MOO_UEFI_DEBUGCON 1
#endif

static inline void uefi_outb(u16 port, u8 value) {
    __asm__ volatile("outb %0,%1" :: "a"(value), "Nd"(port));
}

static inline u8 uefi_inb(u16 port) {
    u8 value;
    __asm__ volatile("inb %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void uefi_debug_byte(u8 value) {
#if MOO_UEFI_DEBUGCON
    uefi_outb(0xe9, value);
#else
    (void)value;
#endif
}

static inline void uefi_marker_byte(u8 value) {
    uefi_debug_byte(value);
    while (!(uefi_inb(0x3fd) & 0x20)) {}
    uefi_outb(0x3f8, value);
}

#endif
