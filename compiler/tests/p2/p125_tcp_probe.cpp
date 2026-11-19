#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
extern "C" uint64_t abstand(uint64_t,uint64_t) asm("__moo_native.tcp_abstand");
extern "C" uint64_t addieren(uint64_t,uint64_t) asm("__moo_native.tcp_addieren");
int main() {
    size_t checks=0;
    auto test=[&](uint64_t a,uint64_t b) {
        // C++-Orakel rechnet direkt in unsigned 32 Bit, ohne Handmasken.
        const auto add=uint32_t(uint32_t(a)+uint32_t(b));
        const auto diff=uint32_t(uint32_t(b)-uint32_t(a));
        if(addieren(a,b)!=add||abstand(a,b)!=diff) {
            std::cerr<<"FAIL TCP Modulo "<<a<<" "<<b<<"\n";std::exit(1);
        }
        checks+=2;
    };
    const std::array<uint64_t,8> values={0,1,2,UINT32_MAX-1,UINT32_MAX,UINT64_C(0x100000000),UINT64_C(0x100000001),UINT64_MAX};
    for(auto a:values)for(auto b:values)test(a,b);
    uint64_t s=17;
    for(size_t i=0;i<100000;++i){
        s=s*UINT64_C(6364136223846793005)+1;const auto a=s;
        s=s*UINT64_C(6364136223846793005)+1;test(a,s);
    }
    std::cout<<"PASS P125 TCP: "<<checks<<" Orakelpruefungen an echten Kernel-Funktionen\n";
}
