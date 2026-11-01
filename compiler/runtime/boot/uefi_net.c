/* P7-T9: Netzwerk-Konsole ueber UEFI-SimpleNetworkProtocol (SNP), alle NICs.
 *
 * moo_uefi_net_init(SystemTable): LocateHandleBuffer(ByProtocol, SNP) holt
 * ALLE SNP-Handles (N100: 6x I226-V), HandleProtocol je Handle, danach Start /
 * Initialize und MediaPresent per GetStatus. Den rekursiven ConnectController-
 * Lauf ueber alle Handles erledigt uefi_entry.c (moo_uefi_alles_verbinden, aus
 * moo_uefi_eingabe_init) vor diesem Aufruf; hier wird er nicht wiederholt.
 *
 * moo_uefi_net_sende(text, len): UDP-Broadcast an 255.255.255.255:7777 auf
 * JEDER NIC mit Link (Ethernet II + IPv4 + UDP, Quelle 0.0.0.0:7777).
 * kern_netz_status(): Moo-Text "NETZ L/N LINK", "NETZ 0/N KEIN LINK" oder
 * "NETZ FEHLT". Diagnose auf debugcon 0xE9: MOO-NET-NIC i LINK x, MOO-NET-NICS n LINKS l.
 *
 * Annahmen (getaggt, nicht am Testrechner belegt):
 *  - Quell-IP 0.0.0.0 (Review P7-T9: Log ohne eigene Adresse, wie DHCP-Discover;
 *    eine feste 10.0.2.15 scheitert im LAN am rp_filter des Empfaengers).
 *  - Quell-MAC je NIC = Mode->CurrentAddress; HeaderSize 0 bei Transmit.
 *  - UDP-Pruefsumme 0 (fuer IPv4 erlaubt).
 *
 * Freestanding C11, MS-x64-ABI wie uefi_entry.c. Eigene EFI-Typen mit Praefix
 * Net, damit uefi_entry.c unveraendert bleibt. MooValue/moo_string_new kommen
 * aus uefi_rt.c (Bare-Runtime).
 */
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

typedef struct { u64 tag; u64 data; } MooValue;
extern MooValue moo_string_new(const char *chars);

#define NET_PORT 7777u
#define NET_PAYLOAD_MAX 1472u
#define NET_FRAME_MIN 60u
#define NET_FRAME_MAX 1536u
#define NET_NIC_MAX 8u
#define NET_EFI_NOT_READY 0x8000000000000006ULL
#define NET_SNP_STATE_STOPPED 0u
#define NET_SNP_STATE_STARTED 1u
#define NET_SNP_STATE_INITIALIZED 2u
#define NET_SEARCH_BY_PROTOCOL 2u

typedef struct { u32 a; u16 b, c; u8 d[8]; } NetGuid;
typedef struct { u64 signature; u32 revision, size, crc, reserved; } NetHeader;
typedef u64 (*NetStallFn)(u64 microseconds);
typedef u64 (*NetLocateFn)(NetGuid *, void *, void **);
typedef u64 (*NetFreePoolFn)(void *buffer);
typedef u64 (*NetHandleProtocolFn)(void *handle, NetGuid *protocol, void **iface);
typedef u64 (*NetLocateHandleBufferFn)(u32 search_type, NetGuid *protocol, void *key,
                                       u64 *count, void ***buffer);

/* Boot-Services-Offsets nach UEFI-Spezifikation (wie uefi_entry.c):
 * 0,1 RaiseTPL/RestoreTPL | 2..5 (4 Eintraege) | 6 FreePool | 7..15 (9) |
 * 16 HandleProtocol | 17..27 (11) | 28 Stall | 29..35 (7) |
 * 36 LocateHandleBuffer | 37 LocateProtocol. */
typedef struct {
    NetHeader hdr;
    void *raise_tpl, *restore_tpl;
    void *b2_5[4];
    NetFreePoolFn free_pool;
    void *b7_15[9];
    NetHandleProtocolFn handle_protocol;
    void *b17_27[11];
    NetStallFn stall;
    void *b29_35[7];
    NetLocateHandleBufferFn locate_handle_buffer;
    NetLocateFn locate;
} NetBootServices;

typedef struct {
    NetHeader hdr; void *vendor; u32 revision;
    void *in_handle, *in, *out_handle, *out, *err_handle, *err, *runtime;
    NetBootServices *boot;
} NetSystemTable;

typedef struct NetSnp NetSnp;

/* EFI_SIMPLE_NETWORK_MODE: Offsets 0..652 nach Spezifikation. */
typedef struct {
    u32 state, hw_address_size, media_header_size, max_packet_size;
    u32 nv_ram_size, nv_ram_access_size, receive_filter_setting, receive_filter_mask;
    u32 max_mcast_filter_count, mcast_filter_count;
    u8 mcast_filter[16][32];
    u8 current_address[32], broadcast_address[32], permanent_address[32];
    u8 if_type, mac_changeable, multiple_tx, media_present_supported, media_present;
} NetSnpMode;

/* EFI_SIMPLE_NETWORK_PROTOCOL: genutzte Funktionen typisiert, Rest 8-Byte-Platzhalter. */
struct NetSnp {
    u64 revision;
    u64 (*start)(NetSnp *);
    u64 (*stop)(NetSnp *);
    u64 (*initialize)(NetSnp *, u64 extra_rx, u64 extra_tx);
    void *reset, *shutdown, *receive_filters, *station_address, *statistics;
    void *mcast_ip_to_mac, *nv_data;
    u64 (*get_status)(NetSnp *, u32 *irq, void **tx_buf);
    u64 (*transmit)(NetSnp *, u64 header_size, u64 buffer_size, const void *buffer,
                    const u8 *src, const u8 *dest, const u16 *proto);
    void *receive, *wait_for_packet;
    NetSnpMode *mode;
};

static NetStallFn net_stall;
static NetSnp *net_nic[NET_NIC_MAX];
static u8 net_link[NET_NIC_MAX];
static u32 net_nic_count, net_link_count;
/* Je NIC ein eigener Rahmenpuffer: die NIC haelt ihn bis GetStatus ihn zurueckgibt. */
static u8 net_frames[NET_NIC_MAX][NET_FRAME_MAX];

static void net_outb(u16 p, u8 v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static void net_dbg(const char *s) { for (; *s; ++s) net_outb(0xE9, (u8)*s); }
static void net_dbg_zif(u32 v) { net_outb(0xE9, (u8)(v < 10u ? '0' + v : '+')); }

static void net_be16(u8 *p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }

static u16 net_ip_checksum(const u8 *p, u32 n) {
    u32 sum = 0, i;
    for (i = 0; i + 1 < n; i += 2) sum += ((u32)p[i] << 8) | p[i + 1];
    if (n & 1u) sum += (u32)p[n - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (u16)(~sum & 0xFFFFu);
}

/* Baut Ethernet II + IPv4 + UDP um text[0..len) in frame; gibt Rahmenlaenge zurueck. */
static u32 net_baue_rahmen(u8 *frame, const u8 *text, u32 len, const u8 *src_mac) {
    static const u8 src_ip[4] = {0, 0, 0, 0};
    static const u8 dst_ip[4] = {255, 255, 255, 255};
    u8 *ip = frame + 14;
    u8 *udp = frame + 34;
    u32 i, udp_len = 8u + len, ip_len = 20u + udp_len, total;
    for (i = 0; i < 6; ++i) { frame[i] = 0xFF; frame[6 + i] = src_mac[i]; }
    frame[12] = 0x08;
    frame[13] = 0x00;
    ip[0] = 0x45;
    ip[1] = 0;
    net_be16(ip + 2, ip_len);
    net_be16(ip + 4, 0);
    net_be16(ip + 6, 0);
    ip[8] = 64;
    ip[9] = 17;
    net_be16(ip + 10, 0);
    for (i = 0; i < 4; ++i) { ip[12 + i] = src_ip[i]; ip[16 + i] = dst_ip[i]; }
    net_be16(ip + 10, net_ip_checksum(ip, 20));
    net_be16(udp, NET_PORT);
    net_be16(udp + 2, NET_PORT);
    net_be16(udp + 4, udp_len);
    net_be16(udp + 6, 0);
    for (i = 0; i < len; ++i) udp[8 + i] = text[i];
    total = 14u + ip_len;
    while (total < NET_FRAME_MIN) frame[total++] = 0;
    return total;
}

/* Link einer NIC: MediaPresent nach GetStatus. Ohne Unterstuetzung gilt 'an'. */
static u32 net_link_lesen(NetSnp *snp) {
    NetSnpMode *m = snp->mode;
    u32 irq = 0;
    void *tx = 0;
    if (!m) return 0;
    snp->get_status(snp, &irq, &tx);
    if (!m->media_present_supported) return 1;
    return m->media_present ? 1u : 0u;
}

static void net_link_aktualisieren(void) {
    u32 n, l = 0;
    for (n = 0; n < net_nic_count; ++n) {
        net_link[n] = (u8)net_link_lesen(net_nic[n]);
        l += net_link[n];
    }
    net_link_count = l;
}

/* Alle SNP-Instanzen der Firmware einsammeln, starten und initialisieren.
 * Rueckgabe 1 = mindestens eine nutzbare NIC (Link egal), 0 = keine. */
int moo_uefi_net_init(void *system_table) {
    static NetGuid guid = {0xA19832B9, 0xAC25, 0x11D3,
                           {0x9A, 0x2D, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D}};
    NetSystemTable *st = (NetSystemTable *)system_table;
    NetBootServices *bs;
    void **handles = 0;
    u64 count = 0, i;
    net_nic_count = 0;
    net_link_count = 0;
    if (!st || !st->boot) return 0;
    bs = st->boot;
    if (!bs->locate_handle_buffer || !bs->handle_protocol) return 0;
    if (bs->locate_handle_buffer(NET_SEARCH_BY_PROTOCOL, &guid, 0, &count, &handles) != 0 || !handles) {
        net_dbg("MOO-NET-NICS 0 LINKS 0\n");
        return 0;
    }
    net_stall = bs->stall;
    for (i = 0; i < count && net_nic_count < NET_NIC_MAX; ++i) {
        NetSnp *snp = 0;
        NetSnpMode *m;
        if (bs->handle_protocol(handles[i], &guid, (void **)&snp) != 0 || !snp) continue;
        m = snp->mode;
        if (!m || m->hw_address_size != 6 || m->media_header_size != 14 ||
            m->max_packet_size < 20u + 8u + NET_PAYLOAD_MAX)
            continue;
        if (m->state == NET_SNP_STATE_STOPPED && snp->start(snp) != 0) continue;
        if (m->state == NET_SNP_STATE_STARTED && snp->initialize(snp, 0, 0) != 0) continue;
        if (m->state != NET_SNP_STATE_INITIALIZED) continue;
        /* Ein physischer Port kann mehrere SNP-Instanzen haben (Befund N100 mit
         * iPXE: jede Logzeile kam doppelt, gleiche Quell-MAC). Pro MAC nur eine
         * Instanz verwenden, sonst sendet jede Zeile mehrfach. */
        {
            u32 k, j, gleich = 0;
            for (k = 0; k < net_nic_count && !gleich; ++k) {
                const u8 *a = net_nic[k]->mode->current_address;
                gleich = 1;
                for (j = 0; j < 6; ++j) if (a[j] != m->current_address[j]) gleich = 0;
            }
            if (gleich) { net_dbg("MOO-NET-DOPPELT\n"); continue; }
        }
        net_nic[net_nic_count] = snp;
        net_link[net_nic_count] = (u8)net_link_lesen(snp);
        net_link_count += net_link[net_nic_count];
        net_dbg("MOO-NET-NIC "); net_dbg_zif(net_nic_count);
        net_dbg(" LINK "); net_dbg_zif(net_link[net_nic_count]); net_dbg("\n");
        ++net_nic_count;
    }
    if (bs->free_pool) bs->free_pool(handles);
    net_dbg("MOO-NET-NICS "); net_dbg_zif(net_nic_count);
    net_dbg(" LINKS "); net_dbg_zif(net_link_count); net_dbg("\n");
    return net_nic_count > 0;
}

/* Wartet, bis SNP den Sendepuffer dieser NIC per GetStatus zurueckgibt (der Puffer
 * gehoert dem Treiber, bis er als TxBuf gemeldet wird). Begrenzt auf ~100 ms;
 * danach wird trotzdem fortgesetzt (Log-Pfad). */
static void net_tx_abholen(NetSnp *snp, const u8 *frame) {
    u32 tries;
    for (tries = 0; tries < 10000u; ++tries) {
        u32 irq = 0;
        void *done = 0;
        if (snp->get_status(snp, &irq, &done) != 0) return;
        if (done == (const void *)frame) return;
        if (net_stall) net_stall(10);
    }
}

/* Ein Rahmen auf einer NIC; EFI_NOT_READY -> Tx-Queue leeren, begrenzt wiederholen. */
static int net_sende_auf(u32 nic, const u8 *text, u32 len) {
    NetSnp *snp = net_nic[nic];
    NetSnpMode *m = snp->mode;
    u8 *frame = net_frames[nic];
    u32 flen, tries;
    u64 status;
    if (!m || m->state != NET_SNP_STATE_INITIALIZED) return 0;
    flen = net_baue_rahmen(frame, text, len, m->current_address);
    for (tries = 0; tries < 100000u; ++tries) {
        status = snp->transmit(snp, 0, flen, frame, 0, 0, 0);
        if (status == 0) { net_tx_abholen(snp, frame); return 1; }
        if (status != NET_EFI_NOT_READY) return 0;
        {
            u32 irq = 0;
            void *done = 0;
            snp->get_status(snp, &irq, &done);
        }
        if (net_stall) net_stall(10);
    }
    return 0;
}

/* Sendet text[0..len) als UDP-Broadcast auf allen NICs mit Link (Link wird vorher
 * neu gelesen). Gekuerzt auf 1472 Byte. 1 = mindestens auf einer NIC angenommen. */
int moo_uefi_net_sende(const u8 *text, u64 len) {
    u32 n, gesendet = 0;
    if (!text) return 0;
    if (len > NET_PAYLOAD_MAX) len = NET_PAYLOAD_MAX;
    net_link_aktualisieren();
    for (n = 0; n < net_nic_count; ++n) {
        if (!net_link[n]) continue;
        if (net_sende_auf(n, text, (u32)len)) ++gesendet;
    }
    return gesendet > 0;
}

static void net_put(char *b, u32 *p, const char *s) { while (*s) b[(*p)++] = *s++; }

/* kern_netz_status(): Netzstatus als Moo-Text fuer die Statuszeile. */
MooValue kern_netz_status(void) {
    char b[32];
    u32 p = 0;
    net_link_aktualisieren();
    if (net_nic_count == 0) {
        net_put(b, &p, "NETZ FEHLT");
    } else if (net_link_count == 0) {
        net_put(b, &p, "NETZ 0/");
        b[p++] = (char)(net_nic_count < 10u ? '0' + net_nic_count : '+');
        net_put(b, &p, " KEIN LINK");
    } else {
        net_put(b, &p, "NETZ ");
        b[p++] = (char)(net_link_count < 10u ? '0' + net_link_count : '+');
        b[p++] = '/';
        b[p++] = (char)(net_nic_count < 10u ? '0' + net_nic_count : '+');
        net_put(b, &p, " LINK");
    }
    b[p] = '\0';
    return moo_string_new(b);
}
