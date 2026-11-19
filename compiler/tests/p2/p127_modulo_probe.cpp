#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
extern "C" {
uint8_t p127_case_0(uint8_t) asm("p127_case_0");
uint8_t p127_case_1(uint8_t) asm("p127_case_1");
uint8_t p127_case_2(uint8_t) asm("p127_case_2");
uint8_t p127_case_3(uint8_t) asm("p127_case_3");
uint8_t p127_case_4(uint8_t) asm("p127_case_4");
uint8_t p127_case_5(uint8_t) asm("p127_case_5");
uint16_t p127_case_6(uint16_t) asm("p127_case_6");
uint16_t p127_case_7(uint16_t) asm("p127_case_7");
uint16_t p127_case_8(uint16_t) asm("p127_case_8");
uint32_t p127_case_9(uint32_t) asm("p127_case_9");
uint32_t p127_case_10(uint32_t) asm("p127_case_10");
uint32_t p127_case_11(uint32_t) asm("p127_case_11");
uint32_t p127_case_12(uint32_t) asm("p127_case_12");
uint64_t p127_pair64(uint64_t) asm("p127_pair64");
uint64_t p127_case_14(uint64_t) asm("p127_case_14");
uint64_t p127_case_15(uint64_t) asm("p127_case_15");
uint64_t p127_case_16(uint64_t) asm("p127_case_16");
int64_t p127_signed64(int64_t) asm("p127_signed64");
uint64_t p127_variable_mask(uint64_t,uint64_t) asm("p127_variable_mask");
uint64_t p127_variable_divisor(uint64_t,uint64_t) asm("p127_variable_divisor");
void p127_fuellen(float*,uint64_t) asm("p127_fuellen");
}
static size_t checks;
static void check(bool ok,const char* name) {
    ++checks;
    if(!ok){std::cerr<<"FAIL P127 "<<name<<"\n";std::exit(1);}
}
static uint64_t next(uint64_t& s){s=s*UINT64_C(6364136223846793005)+1;return s;}
template<class T> static void test(T(*fn)(T),uint64_t div,uint64_t mask) {
    const T d=T(div),c=T(mask);
    auto one=[&](T x){
        const T expected=T(T(x%d)+T((x|c)%d));
        check(fn(x)==expected,"unsigned Restpaar");
    };
    if constexpr(sizeof(T)==1) { for(unsigned x=0;x<256;++x)one(T(x)); }
    else {
        for(uint64_t x:{UINT64_C(0),UINT64_C(1),UINT64_C(2),UINT64_C(98),UINT64_C(99),UINT64_C(100),UINT64_C(101),UINT64_C(255),UINT64_C(65535),UINT64_C(0x100000000),UINT64_C(1)<<63,UINT64_MAX-1,UINT64_MAX})one(T(x));
        uint64_t s=73;for(unsigned i=0;i<8192;++i)one(T(next(s)));
    }
}
int main(){
    test<uint8_t>(p127_case_0,UINT64_C(6),UINT64_C(1));
    test<uint8_t>(p127_case_1,UINT64_C(12),UINT64_C(3));
    test<uint8_t>(p127_case_2,UINT64_C(100),UINT64_C(1));
    test<uint8_t>(p127_case_3,UINT64_C(192),UINT64_C(63));
    test<uint8_t>(p127_case_4,UINT64_C(100),UINT64_C(4));
    test<uint8_t>(p127_case_5,UINT64_C(99),UINT64_C(1));
    test<uint16_t>(p127_case_6,UINT64_C(100),UINT64_C(3));
    test<uint16_t>(p127_case_7,UINT64_C(768),UINT64_C(255));
    test<uint16_t>(p127_case_8,UINT64_C(1024),UINT64_C(511));
    test<uint32_t>(p127_case_9,UINT64_C(120),UINT64_C(7));
    test<uint32_t>(p127_case_10,UINT64_C(1000),UINT64_C(7));
    test<uint32_t>(p127_case_11,UINT64_C(4294967280),UINT64_C(15));
    test<uint32_t>(p127_case_12,UINT64_C(100),UINT64_C(7));
    test<uint64_t>(p127_pair64,UINT64_C(100),UINT64_C(1));
    test<uint64_t>(p127_case_14,UINT64_C(100),UINT64_C(3));
    test<uint64_t>(p127_case_15,UINT64_C(9223372036854775808),UINT64_C(4611686018427387905));
    test<uint64_t>(p127_case_16,UINT64_C(99),UINT64_C(1));
    for(int64_t x:{INT64_MIN,INT64_MIN+1,INT64_C(-1001),INT64_C(-100),INT64_C(-1),INT64_C(0),INT64_C(1),INT64_C(100),INT64_MAX}){
        check(p127_signed64(x)==(x%100)+((x|INT64_C(1))%100),"signed Gegenbeispiel");
    }
    uint64_t s=127;
    for(unsigned i=0;i<8192;++i){
        const uint64_t x=next(s),mask=next(s),d=next(s)|1;
        check(p127_variable_mask(x,mask)==(x%100)+((x|mask)%100),"variable Maske");
        check(p127_variable_divisor(x,d)==(x%d)+((x|1)%d),"variabler Divisor");
    }
    p127_fuellen(nullptr,0);
    for(size_t n:{size_t(0),size_t(1),size_t(2),size_t(3),size_t(99),size_t(100),size_t(101),size_t(199),size_t(256),size_t(1025),size_t(100003)}){
        std::vector<uint8_t> bytes(n*sizeof(float)+2,0xa5),expected=bytes;
        // Rohzeiger bleiben byte-aligned zulaessig. Das C-Orakel schreibt
        // per memcpy, damit es selbst keine staerkere Alignmentannahme macht.
        p127_fuellen(reinterpret_cast<float*>(bytes.data()+1),n);
        for(size_t i=0;i<n;++i){const float f=float(i%100);std::memcpy(expected.data()+1+i*sizeof(float),&f,sizeof f);}
        check(bytes==expected,"unaligned Ausgabe und Canary");
    }
    std::cout<<"PASS P127 Modulo: "<<checks<<" C++-Orakelpruefungen, signed/ungerade/variable Gegenbeispiele und unaligned Fuellen\n";
}
