// Echte C-ABI-Aufrufe an beide Seiten der erzeugten Runtime-Adapter.
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
struct Value {uint64_t tag,data;};
extern "C" Value moo_thread_done(Value);
extern "C" Value moo_thread_spawn(Value,Value);
extern "C" Value kern_c_moo_number(double);
static unsigned checks,number_calls,call8_calls;
static void check(bool ok,const char* name) {
    ++checks;if(!ok){std::cerr<<"FAIL Runtime-ABI "<<name<<"\n";std::exit(1);}
}
static uint64_t bits(double n) {uint64_t v;std::memcpy(&v,&n,8);return v;}
extern "C" Value moo_number(double n) {++number_calls;return {0,bits(n)};}
static Value expected_func,expected_arg;
extern "C" Value moo_func_call_8(Value f,Value a,Value b,Value c,Value d,Value e,Value g,Value h,Value i) {
    ++call8_calls;
    const std::array<Value,9> got={f,a,b,c,d,e,g,h,i};
    const std::array<Value,9> want={expected_func,expected_arg,Value{31,UINT64_C(0xFEDCBA9876543210)},Value{1,2},Value{2,3},Value{3,4},Value{4,5},Value{5,6},Value{6,7}};
    for(size_t n=0;n<got.size();++n)check(got[n].tag==want[n].tag&&got[n].data==want[n].data,"9 Strukturargumente Register/Stack");
    return {UINT64_C(0xFEDCBA9876543210),UINT64_C(0x8ABCDEF012345678)};
}
int main() {
    for(double n:{0.0,-0.0,17.5,-123.25,std::numeric_limits<double>::infinity()}) {
        auto r=kern_c_moo_number(n);check(r.tag==0&&r.data==bits(n),"C-Helper->RawRuntime->C-Rueckgabe");
    }
    auto r=moo_thread_done({31,17});
    check(r.tag==0&&r.data==bits(17.0),"RawRuntime->Native->C-Helper");
    for(uint64_t n:{UINT64_C(0),UINT64_C(1),UINT64_C(0xFEDCBA9876543210)}) {
        expected_func={6,n};expected_arg={31,n^UINT64_C(0xA55A55AA55AA55A5)};
        r=moo_thread_spawn(expected_func,expected_arg);
        check(r.tag==UINT64_C(0xFEDCBA9876543210)&&r.data==UINT64_C(0x8ABCDEF012345678),"16-Byte-Rueckgabebits");
    }
    check(number_calls==6&&call8_calls==3,"alle Bruecken wirklich ausgefuehrt");
    std::cout<<"PASS Runtime-Provider-ABI "<<checks<<" Pruefungen\n";
}
