/* Unabhaengiges bitweises Orakel; Guardpages erzwingen Inbounds-Loads. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>

extern uint32_t crc_gate(uint8_t *, uint64_t, uint32_t);
extern uint32_t crc_bereich(uint8_t *, uint64_t, uint32_t, uint64_t, uint64_t);
extern uint64_t crc_moofs(uint8_t *, uint64_t);
_Noreturn void moo_native_fehler(const char *message) { (void)message; abort(); }
static uint32_t oracle(uint32_t c, const uint8_t *p, size_t n) {
    for (size_t i=0; i<n; ++i) {
        c ^= p[i];
        for (unsigned b=0; b<8; ++b)
            c = (c >> 1) ^ ((0u-(c&1u)) & UINT32_C(0x82f63b78));
    }
    return c;
}
static void check(int ok, const char *name) {
    if (!ok) { fprintf(stderr,"FAIL %s\n",name); exit(1); }
}
int main(void) {
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t span = 4*page;
    uint8_t *map = mmap(NULL,span+2*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    check(map != MAP_FAILED,"mmap");
    uint8_t *data=map+page;
    check(!mprotect(data,span,PROT_READ|PROT_WRITE),"mprotect");
    for (size_t i=0;i<span;++i) data[i]=(uint8_t)(i*37u+(i>>5));
    uint32_t seeds[]={0,UINT32_MAX,UINT32_C(0x12345678)};
    uint64_t cases=0;
    for (unsigned s=0;s<3;++s) {
        check(crc_gate(NULL,0,seeds[s])==seeds[s],"empty NULL");
        for (size_t n=0;n<=3073;++n) {
            uint8_t *p=data+span-n;
            uint32_t expected=oracle(seeds[s],p,n);
            check(crc_gate(p,n,seeds[s])==expected,"end guard/all remainders");
            check(crc_gate(data,n,seeds[s])==oracle(seeds[s],data,n),"start guard");
            size_t half=n/2;
            uint32_t first=crc_bereich(p,n,seeds[s],0,half);
            check(crc_bereich(p,n,first,half,n-half)==expected,"split update");
            check(crc_bereich(p,n,seeds[s],n,0)==seeds[s],"empty at end");
            ++cases;
        }
        for(size_t n=4095;n<=span;n+=4096) {
            uint8_t *p=data+span-n;
            check(crc_gate(p,n,seeds[s])==oracle(seeds[s],p,n),"large");
        }
    }
    check((crc_gate((uint8_t *)"123456789",9,UINT32_MAX)^UINT32_MAX)==UINT32_C(0xe3069283),"known CRC32C");
    uint8_t copy[4096];
    memcpy(copy,data,4096);
    memset(copy+40,0,4);
    check(crc_moofs(data,4096)==(oracle(UINT32_MAX,copy,4096)^UINT32_MAX),"MooFS logical zeros");
    check(memcmp(data+40,copy+40,4)!=0,"MooFS input unchanged");
    check(crc_moofs(data,4095)==UINT64_C(0x100000000),"MooFS length");
    /* @einstieg ohne Moo-Handler verwendet laut Produktvertrag llvm.trap. */
    static const uint64_t invalid[4][2]={{4097,0},{4095,2},{UINT64_MAX,2},{1,UINT64_MAX}};
    struct rlimit no_core={0,0};
    check(!setrlimit(RLIMIT_CORE,&no_core),"disable core dumps");
    for (unsigned bad=0;bad<4;++bad) {
        pid_t child=fork();
        check(child>=0,"fork");
        if (!child) {
            (void)crc_bereich(data,4096,0,invalid[bad][0],invalid[bad][1]);
            _exit(99);
        }
        int status=0;
        check(waitpid(child,&status,0)==child,"waitpid");
        check(WIFSIGNALED(status) && WTERMSIG(status)==SIGILL,"range errors including overflow");
    }
    check(!munmap(map,span+2*page),"munmap");
    printf("CRC32C OK %" PRIu64 " lengths/seeds, split, guardpages, empty, MooFS and 4 bounds errors\n",cases);
    return 0;
}
