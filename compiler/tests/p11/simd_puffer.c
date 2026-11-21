/* Guardpages pruefen reale unaligned SIMD-I/O, nicht nur das IR. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
typedef void (*probe)(uint8_t*,uint64_t,uint64_t,uint8_t*,uint64_t,uint64_t);
extern void simd_puffer_16_skalar(uint8_t*,uint64_t,uint64_t,uint8_t*,uint64_t,uint64_t);
extern void simd_puffer_32_skalar(uint8_t*,uint64_t,uint64_t,uint8_t*,uint64_t,uint64_t);
extern void simd_puffer_16_ssse3(uint8_t*,uint64_t,uint64_t,uint8_t*,uint64_t,uint64_t);
extern void simd_puffer_32_ssse3(uint8_t*,uint64_t,uint64_t,uint8_t*,uint64_t,uint64_t);
extern void simd_puffer_16_avx2(uint8_t*,uint64_t,uint64_t,uint8_t*,uint64_t,uint64_t);
extern void simd_puffer_32_avx2(uint8_t*,uint64_t,uint64_t,uint8_t*,uint64_t,uint64_t);
extern void simd_puffer_16_gfni128(uint8_t*,uint64_t,uint64_t,uint8_t*,uint64_t,uint64_t);
extern void simd_puffer_32_gfni128(uint8_t*,uint64_t,uint64_t,uint8_t*,uint64_t,uint64_t);
extern void simd_puffer_16_gfni(uint8_t*,uint64_t,uint64_t,uint8_t*,uint64_t,uint64_t);
extern void simd_puffer_32_gfni(uint8_t*,uint64_t,uint64_t,uint8_t*,uint64_t,uint64_t);
_Noreturn void moo_native_fehler(const char *s) { (void)s; abort(); }
static void check(int ok,const char *s) { if(!ok) { fprintf(stderr,"FAIL %s\n",s); exit(1); } }
static void test(const char *name,unsigned width,probe f) {
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    uint8_t *map=mmap(NULL,6*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    check(map!=MAP_FAILED,"mmap");
    uint8_t *a=map+page,*b=map+4*page;
    check(!mprotect(a,page,PROT_READ|PROT_WRITE),"source");
    check(!mprotect(b,page,PROT_READ|PROT_WRITE),"destination");
    for(size_t i=0;i<page;++i) a[i]=(uint8_t)(i*37+(i>>4));
    for(unsigned skew=0;skew<64;++skew) {
        size_t start=page-width-skew;
        memset(b,0xcc,page);
        f(a,page,start,b,page,skew);
        check(!memcmp(a+start,b+skew,width),"offset bytes");
        for(size_t i=0;i<page;++i)
            if(i<skew || i>=skew+width) check(b[i]==0xcc,"store footprint");
        /* Ziel exakt bis zur Guardpage, Quelle teilweise ueberlappend. */
        f(a,page,skew,b,page,page-width);
        check(!memcmp(a+skew,b+page-width,width),"end guard store");
        uint8_t saved[32];
        memcpy(saved,b+skew,width);
        f(b,page,skew,b,page,skew+1);
        check(!memcmp(saved,b+skew+1,width),"overlap snapshot");
    }
    uint64_t bad[5][2]={{page-width+1,0},{0,page-width+1},{UINT64_MAX,0},{0,UINT64_MAX},{page,0}};
    for(unsigned i=0;i<5;++i) {
        pid_t child=fork();check(child>=0,"fork");
        if(!child) { f(a,page,bad[i][0],b,page,bad[i][1]);_exit(99); }
        int status=0;check(waitpid(child,&status,0)==child,"wait");
        check(WIFSIGNALED(status)&&WTERMSIG(status)==SIGILL,"bounds trap before guardpage");
    }
    check(!munmap(map,6*page),"munmap");
    printf("SIMD Puffer OK %s: 64 Offsets, Guardpages, Alias und 5 Boundsfehler\n",name);
}
int main(void) {
    struct rlimit core={0,0};check(!setrlimit(RLIMIT_CORE,&core),"core");
    test("16_skalar",16,simd_puffer_16_skalar);
    test("32_skalar",32,simd_puffer_32_skalar);
    test("16_ssse3",16,simd_puffer_16_ssse3);
    test("32_ssse3",32,simd_puffer_32_ssse3);
    test("16_avx2",16,simd_puffer_16_avx2);
    test("32_avx2",32,simd_puffer_32_avx2);
    test("16_gfni128",16,simd_puffer_16_gfni128);
    test("32_gfni128",32,simd_puffer_32_gfni128);
    test("16_gfni",16,simd_puffer_16_gfni);
    test("32_gfni",32,simd_puffer_32_gfni);
}
