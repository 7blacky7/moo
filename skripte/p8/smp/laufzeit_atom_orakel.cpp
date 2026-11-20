#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
struct Atom {int32_t refcount,reserve;alignas(8) uint64_t bits;};
extern "C" {
void init(Atom*,uint64_t) asm("__moo_native.laufzeit_atom_init");
uint64_t load(Atom*) asm("__moo_native.laufzeit_atom_laden");
void store(Atom*,uint64_t) asm("__moo_native.laufzeit_atom_speichern");
uint64_t swap(Atom*,uint64_t) asm("__moo_native.laufzeit_atom_tauschen");
uint64_t add(Atom*,uint64_t) asm("__moo_native.laufzeit_atom_addieren");
bool cas(Atom*,uint64_t,uint64_t) asm("__moo_native.laufzeit_atom_vergleichen");
}
static unsigned checks;
static void check(bool b,const char* name) {++checks;if(!b){std::cerr<<"FAIL T14 Atom "<<name<<"\n";std::exit(1);}}
static uint64_t bits(double x) {uint64_t y;std::memcpy(&y,&x,8);return y;}
int main(){
    static_assert(sizeof(Atom)==16);
    Atom a{};
    init(&a,bits(5.0));check(a.refcount==1&&a.reserve==0&&load(&a)==bits(5.0),"Layout/Initialisierung");
    check(add(&a,bits(2.5))==bits(5.0)&&load(&a)==bits(7.5),"add liefert alten Wert");
    check(swap(&a,bits(-2.0))==bits(7.5)&&load(&a)==bits(-2.0),"Tausch");
    check(!cas(&a,bits(2.0),bits(7.0))&&load(&a)==bits(-2.0),"CAS Fehlschlag");
    store(&a,bits(-0.0));check(cas(&a,bits(0.0),bits(3.0))&&load(&a)==bits(3.0),"numerisches CAS plus/minus Null");
    constexpr uint64_t nan=UINT64_C(0x7FF8000012345678);
    store(&a,nan);check(load(&a)==nan&&!cas(&a,nan,bits(1.0))&&load(&a)==nan,"NaN-Bits und numerisches CAS");
    check(swap(&a,bits(0.0))==nan,"NaN-Tausch bewahrt alte Bits");
    std::array<std::thread,4> workers;
    for(size_t i=0;i<workers.size();++i)workers[i]=std::thread([&,i]{for(unsigned n=0;n<50000;++n)add(&a,bits(double(i+1)));});
    for(auto& t:workers)t.join();
    check(load(&a)==bits(500000.0),"vier echte Threads, 200000 konkurrierende CAS-Additionen");
    std::cout<<"PASS T14 Atom "<<checks<<" Pruefungen\n";
}
