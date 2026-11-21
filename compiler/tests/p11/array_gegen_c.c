/* P11-T26: Treiber/Orakel, kein Produktcode. Externe ISA-L-C/ASM-Referenz. */
#define _POSIX_C_SOURCE 200809L
#include <isa-l.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <sys/utsname.h>

extern uint64_t cbench_erzeugen(uint8_t **p, uint64_t n);
extern uint64_t cbench_retten(uint8_t **p, uint64_t n);
extern uint32_t cbench_crc(uint8_t *p, uint64_t n);
enum { K = 14, N = 4096, RUNS = 5 };
static uint8_t *pages[16];
static uint8_t encode_tables[32*K*2], recover_tables[32*K*2];
static uint8_t *survivors[K], *outputs[2];
static volatile uint64_t sink;
static void fail(const char *why) {
    fprintf(stderr, "FAIL %s\n", why);
    exit(1);
}

extern uint64_t cbench_scrub_init(uint8_t *, uint64_t);
extern uint64_t cbench_scrub(void);
extern uint64_t ref_cbench_scrub_init(uint8_t *, uint64_t);
extern uint64_t ref_cbench_scrub(void);
/* Nur Kontrollobjekt: gleicher MooFS-Pruefer, CRC-Backend ersetzt durch ISA-L.
 * Der Produktcompiler/Stdlib wird dafuer NICHT umgestellt. */
uint32_t __moo_crc32c_update(uint32_t seed, uint8_t *data, uint64_t len) {
    while (len) {
        int chunk = len > 0x7fffffffu ? 0x7fffffff : (int)len;
        seed = crc32_iscsi(data, chunk, seed);
        data += chunk;
        len -= (unsigned)chunk;
    }
    return seed;
}
void moo_native_fehler(void) { fail("nativer Bereichsfehler"); }
static uint64_t moo_scrub(uint8_t **p, uint64_t n) {
    (void)p; (void)n; return cbench_scrub();
}
static uint64_t reference_scrub(uint8_t **p, uint64_t n) {
    (void)p; (void)n; return ref_cbench_scrub();
}

static uint64_t ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) fail("clock");
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}
/* Unabhängige Polynommultiplikation und Division, nicht ISA-L gf_mul. */
static uint8_t mul(unsigned a, unsigned b) {
    unsigned x = 0;
    for (unsigned i=0; i<8; ++i) if (b & (1u<<i)) x ^= a<<i;
    for (int i=14; i>=8; --i) if (x & (1u<<i)) x ^= 0x11du<<(i-8);
    return (uint8_t)x;
}

/* Konservative Aliasroute muss die bisherige 32-Byte-Passfolge erhalten.
 * Ein rein skalares Aliasorakel waere falsch: Teilueberlappung beobachtet
 * schon innerhalb eines 32-Byte-Blocks andere Rueckkopplungen. */
static void old_xor(uint8_t *q, const uint8_t *data, unsigned n, uint8_t factor) {
    unsigned i=0;
    for(;n-i>=32;i+=32) {
        uint8_t a[32], b[32];
        memcpy(a,data+i,32); memcpy(b,q+i,32);
        for(unsigned lane=0;lane<32;++lane) q[i+lane]=b[lane]^mul(a[lane],factor);
    }
    for(;i<n;++i) q[i]^=mul(data[i],factor);
}
static void old_encode(uint8_t **p,unsigned n) {
    memset(p[14],0,n); memset(p[15],0,n);
    uint8_t coefficient=1;
    for(unsigned slot=0;slot<14;++slot) {
        old_xor(p[14],p[slot],n,1);
        old_xor(p[15],p[slot],n,coefficient);
        coefficient=mul(coefficient,2);
    }
}
static void alias_test(void) {
    enum { STRIDE=N+16, TOTAL=16*STRIDE };
    unsigned lengths[]={0,1,31,32,33,255,N};
    uint8_t *a=malloc(TOTAL), *b=malloc(TOTAL), *x[16],*y[16];
    if(!a || !b) fail("Alias Speicher");
    for(unsigned length=0;length<sizeof lengths/sizeof *lengths;++length)
    for(unsigned variant=0;variant<7;++variant) {
        for(unsigned i=0;i<TOTAL;++i) a[i]=b[i]=(uint8_t)(i*13+(i>>5));
        for(unsigned i=0;i<16;++i) { x[i]=a+i*STRIDE+8; y[i]=b+i*STRIDE+8; }
        if(variant==1) { x[14]=x[0]; y[14]=y[0]; }
        if(variant==2) { x[15]=x[0]; y[15]=y[0]; }
        if(variant==3) { x[15]=x[14]; y[15]=y[14]; }
        if(variant==4) { x[14]=x[0]+1; y[14]=y[0]+1; }
        if(variant==5) { x[15]=x[14]+1; y[15]=y[14]+1; }
        if(variant==6) { x[1]=x[2]; y[1]=y[2]; }
        unsigned n=lengths[length];
        old_encode(y,n);
        if(cbench_erzeugen(x,n) || memcmp(a,b,TOTAL)) fail("Alias-Passreihenfolge");
    }
    free(b); free(a);
    printf("{\"alias_faelle\":49,\"orakel\":\"bisherige-32Byte-Passfolge\"}\n");
}


extern void cbench_zwei(uint8_t*,uint8_t*,uint8_t*,uint8_t*,uint64_t,uint8_t,uint8_t);
static void old_two(uint8_t *a,uint8_t *b,uint8_t *dp,uint8_t *dq,unsigned n) {
    unsigned i=0;
    for(;n-i>=32;i+=32) {
        uint8_t vp[32],vq[32],va[32],vb[32];
        memcpy(vp,dp+i,32); memcpy(vq,dq+i,32);
        for(unsigned lane=0;lane<32;++lane) {
            va[lane]=mul(vq[lane]^mul(vp[lane],0x87),0x53);
            vb[lane]=vp[lane]^va[lane];
        }
        for(unsigned lane=0;lane<32;++lane) { a[i+lane]=va[lane]; b[i+lane]=vb[lane]; }
    }
    for(;i<n;++i) {
        uint8_t v=mul(dq[i]^mul(dp[i],0x87),0x53);
        a[i]=v; b[i]=dp[i]^v;
    }
}
static void alias_two_test(void) {
    enum { STRIDE=N+32, TOTAL=4*STRIDE };
    unsigned lengths[]={0,1,31,32,33,255,N};
    uint8_t *a=malloc(TOTAL),*b=malloc(TOTAL),*x[4],*y[4];
    if(!a || !b) fail("Zwei Alias Speicher");
    for(unsigned l=0;l<sizeof lengths/sizeof *lengths;++l)
    for(unsigned variant=0;variant<7;++variant) {
        for(unsigned i=0;i<TOTAL;++i) a[i]=b[i]=(uint8_t)(i*37+(i>>3));
        for(unsigned i=0;i<4;++i) { x[i]=a+i*STRIDE; y[i]=b+i*STRIDE; }
        if(variant==1) { x[1]=x[0];y[1]=y[0]; }
        if(variant==2) { x[1]=x[0]+1;y[1]=y[0]+1; }
        if(variant==3) { x[0]=x[1]+1;y[0]=y[1]+1; }
        if(variant==4) { x[0]=x[2]+1;y[0]=y[2]+1; }
        if(variant==5) { x[1]=x[3]+1;y[1]=y[3]+1; }
        if(variant==6) { x[1]=x[0]+17;y[1]=y[0]+17; }
        unsigned n=lengths[l];
        old_two(y[0],y[1],y[2],y[3],n);
        cbench_zwei(x[0],x[1],x[2],x[3],n,0x87,0x53);
        if(memcmp(a,b,TOTAL)) fail("Zwei Alias-Passreihenfolge");
    }
    free(b);free(a);
    printf("{\"alias_zwei_faelle\":49,\"orakel\":\"bisherige-interleaved-Passfolge\"}\n");
}

static uint8_t datum(unsigned slot, unsigned pos) {
    return (uint8_t)(slot*37u + pos*13u + (pos>>3));
}
static void verify_data(void) {
    for (unsigned i=0; i<K; ++i)
        for (unsigned j=0; j<N; ++j)
            if (pages[i][j] != datum(i,j)) fail("Datenbyte");
}
static void verify_parity(void) {
    for (unsigned j=0; j<N; ++j) {
        uint8_t p=0, q=0, coefficient=1;
        for (unsigned i=0; i<K; ++i) {
            p ^= pages[i][j];
            q ^= mul(pages[i][j], coefficient);
            coefficient = mul(coefficient, 2);
        }
        if (pages[K][j]!=p || pages[K+1][j]!=q) fail("P/Q-Byte");
    }
}
static uint64_t reference_encode(uint8_t **p, uint64_t n) {
    ec_encode_data((int)n, K, 2, encode_tables, p, p+K);
    return 0;
}
static uint64_t reference_recover(uint8_t **p, uint64_t n) {
    (void)p;
    ec_encode_data((int)n, K, 2, recover_tables, survivors, outputs);
    return 0;
}
static uint64_t moo_crc(uint8_t **p, uint64_t n) {
    return cbench_crc(p[0], n);
}
extern uint32_t crc32c_ohne_dispatch(uint32_t, uint8_t *, uint64_t);
static uint64_t crc_control(uint8_t **p, uint64_t n) {
    return crc32c_ohne_dispatch(UINT32_MAX,p[0],n)^UINT32_MAX;
}
static uint64_t reference_crc(uint8_t **p, uint64_t n) {
    return crc32_iscsi(p[0], (int)n, 0xffffffffu) ^ 0xffffffffu;
}
static int compare_u64(const void *a, const void *b) {
    uint64_t x=*(const uint64_t *)a, y=*(const uint64_t *)b;
    return (x>y)-(x<y);
}
typedef uint64_t (*work)(uint8_t **, uint64_t);
static void measure(const char *name, const char *implementation, work fn,
                    unsigned loops, uint64_t useful_bytes, int crc) {
    uint64_t times[RUNS], ordered[RUNS], checksum=0;
    uint64_t expected=fn(pages,N);
    if (!crc && expected) fail("Warmup");
    for (unsigned run=0; run<RUNS; ++run) {
        uint64_t start=ns(), value=0;
        for (unsigned i=0; i<loops; ++i) {
            value=fn(pages,N);
            if ((!crc && value) || (crc && value!=expected)) fail("Moo Ergebnis");
            sink ^= value;
        }
        times[run]=ns()-start;
        if (crc) checksum=value;
        else {
            uint64_t sum=0;
            for (unsigned i=0; i<16; ++i)
                for (unsigned j=0; j<N; ++j) sum += pages[i][j];
            if (run && sum!=checksum) fail("Pruefsumme wechselte");
            checksum=sum;
        }
    }
    memcpy(ordered,times,sizeof times);
    qsort(ordered,RUNS,sizeof *ordered,compare_u64);
    double rate=(double)useful_bytes*loops*1000.0/(double)ordered[RUNS/2];
    printf("{\"arbeit\":\"%s\",\"impl\":\"%s\",\"seite\":%d,\"daten\":%d,"
           "\"runden\":%u,\"bytes_je_runde\":%" PRIu64 ",\"ns\":[",
           name,implementation,N,K,loops,useful_bytes);
    for(unsigned i=0;i<RUNS;++i) printf("%s%" PRIu64,i?",":"",times[i]);
    printf("],\"median_MB_s\":%.6f,\"pruefsumme\":%" PRIu64 "}\n",
           rate,checksum);
    fflush(stdout);
}
int main(void) {
    alias_test();
    alias_two_test();
    uint8_t coefficient=1, matrix[2*K], survivor_matrix[K*K];
    uint8_t inverse[K*K], decode[2*K];
    struct utsname sys;
    if (uname(&sys)) fail("uname");
    printf("{\"system\":\"%s\",\"maschine\":\"%s\",\"referenz\":\"ISA-L 2.32.1\","
           "\"zaehlung\":\"dezimal MB/s logischer Datenbytes; CRC nur Eingabebytes\"}\n",
           sys.sysname,sys.machine);
    for(unsigned i=0;i<16;++i) {
        void *allocation=0;
        if(posix_memalign(&allocation,64,N)) fail("Speicher");
        pages[i]=allocation;
        for(unsigned j=0;j<N;++j) pages[i][j]=datum(i,j);
    }
    for(unsigned i=0;i<K;++i) {
        matrix[i]=1;
        matrix[K+i]=coefficient;
        coefficient=mul(coefficient,2);
    }
    ec_init_tables(K,2,matrix,encode_tables);
    memset(survivor_matrix,0,sizeof survivor_matrix);
    for(unsigned row=0;row<K-2;++row) {
        survivors[row]=pages[row+1];
        survivor_matrix[row*K+row+1]=1;
    }
    survivors[K-2]=pages[K];
    survivors[K-1]=pages[K+1];
    memcpy(survivor_matrix+(K-2)*K,matrix,2*K);
    if(gf_invert_matrix(survivor_matrix,inverse,K)) fail("ISA-L Inverse");
    memcpy(decode,inverse,K);
    memcpy(decode+K,inverse+(K-1)*K,K);
    ec_init_tables(K,2,decode,recover_tables);
    outputs[0]=pages[0];
    outputs[1]=pages[K-1];
    reference_encode(pages,N);
    verify_parity();
    if(cbench_erzeugen(pages,N)) fail("Moo Erzeugung");
    verify_parity();
    memset(pages[0],0,N);
    memset(pages[K-1],0,N);
    if(cbench_retten(pages,N)) fail("Moo Rekonstruktion");
    verify_data();
    verify_parity();
    memset(pages[0],0,N);
    memset(pages[K-1],0,N);
    reference_recover(pages,N);
    verify_data();
    verify_parity();
    uint8_t check[]="123456789";
    if(cbench_crc(check,9)!=0xe3069283u) fail("Moo CRC32C Standardvektor");
    if((crc32_iscsi(check,9,0xffffffffu)^0xffffffffu)!=0xe3069283u)
        fail("ISA-L CRC32C Start/Endkomplement");
    if(moo_crc(pages,N)!=reference_crc(pages,N)) fail("CRC Datenidentitaet");
    measure("pq-erzeugen","Moo-geprueft",cbench_erzeugen,4093,K*N,0);
    measure("pq-erzeugen","ISA-L-raw",reference_encode,4093,K*N,0);
    measure("zwei-daten-retten","Moo-geprueft",cbench_retten,4093,K*N,0);
    measure("zwei-daten-retten","ISA-L-raw",reference_recover,4093,K*N,0);
    measure("crc32c","Moo",moo_crc,65521,N,1);
    measure("crc32c","ISA-L",reference_crc,65521,N,1);
    __builtin_cpu_init();
    if (__builtin_cpu_supports("sse4.2")) {
        if (crc_control(pages,N)!=reference_crc(pages,N)) fail("CRC Kontrolle");
        measure("crc32c","Moo-SSE42-Kontrolle-ohne-Dispatch",crc_control,65521,N,1);
    }

    enum { IMAGE_BYTES = 2097152 };
    uint8_t *image=calloc(1,IMAGE_BYTES), *ref_image=calloc(1,IMAGE_BYTES), *saved=malloc(IMAGE_BYTES);
    if (!image || !ref_image || !saved) fail("Scrub Speicher");
    if (cbench_scrub_init(image,IMAGE_BYTES) || ref_cbench_scrub_init(ref_image,IMAGE_BYTES))
        fail("Scrub Formatierung");
    if (memcmp(image,ref_image,IMAGE_BYTES)) fail("Scrub Abbilder nicht identisch");
    memcpy(saved,image,IMAGE_BYTES);
    uint64_t scrub_bytes=cbench_scrub();
    if (!scrub_bytes || scrub_bytes!=ref_cbench_scrub()) fail("Vollscrub Pruefzaehler");
    measure("moofs-vollscrub","MooFS-Moo-CRC",moo_scrub,127,scrub_bytes,1);
    measure("moofs-vollscrub","MooFS-ISA-L-CRC-Kontrolle",reference_scrub,127,scrub_bytes,1);
    if (memcmp(image,saved,IMAGE_BYTES) || memcmp(ref_image,saved,IMAGE_BYTES))
        fail("Scrub hat Abbild veraendert");
    free(saved); free(ref_image); free(image);

    verify_data();
    verify_parity();
    for(unsigned i=0;i<16;++i) free(pages[i]);
    return sink==UINT64_MAX ? 2 : 0;
}
