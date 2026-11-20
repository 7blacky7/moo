// Unabhängige ACPI-Bytefolgen gegen die native Moo-Implementierung.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
extern "C" {
bool parse(uint64_t,uint64_t) asm("__moo_native.smp_madt");
uint32_t count() asm("__moo_native.t11_anzahl");
bool valid() asm("__moo_native.t11_ok");
struct Cpu { uint32_t id,uid,flags; bool ready; uint64_t base,stack,ist1,ist2; };
Cpu* cpu(uint32_t) asm("__moo_native.smp_kern");
}
static size_t checks=0;
static void check(bool ok,const char* name) {
    ++checks;
    if(!ok){std::cerr<<"FAIL T11 "<<name<<"\n";std::exit(1);}
}
static void word(std::vector<uint8_t>& b,size_t p,uint32_t n) {
    for(unsigned i=0;i<4;++i)b.at(p+i)=uint8_t(n>>(i*8));
}
static std::vector<uint8_t> header() { return std::vector<uint8_t>(44); }
static void lapic(std::vector<uint8_t>& b,uint8_t id,uint8_t uid,uint32_t flags=1) {
    size_t p=b.size();b.resize(p+8);b[p]=0;b[p+1]=8;b[p+2]=uid;b[p+3]=id;word(b,p+4,flags);
}
static void x2apic(std::vector<uint8_t>& b,uint32_t id,uint32_t uid,uint32_t flags=1) {
    size_t p=b.size();b.resize(p+16);b[p]=9;b[p+1]=16;word(b,p+4,id);word(b,p+8,flags);word(b,p+12,uid);
}
// Ende exakt an einer unlesbaren Seite: ein einziger Überlauf wird sichtbar.
static bool run(std::vector<uint8_t> b,bool set_length=true) {
    if(set_length)word(b,4,uint32_t(b.size()));
    const size_t page=size_t(sysconf(_SC_PAGESIZE));
    check(b.size()<=page,"Test passt in Guard-Fenster");
    auto* map=static_cast<uint8_t*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    check(map!=MAP_FAILED,"mmap");
    check(mprotect(map+page,page,PROT_NONE)==0,"Guard");
    auto* start=map+page-b.size();std::memcpy(start,b.data(),b.size());
    const bool ok=parse(reinterpret_cast<uint64_t>(start),b.size());
    check(ok==valid(),"Status konsistent");
    check(munmap(map,page*2)==0,"munmap");
    return ok;
}
int main() {
    check(!parse(0,0)&&!valid(),"Null");
    auto b=header();lapic(b,2,5);lapic(b,7,9);lapic(b,8,10,0);lapic(b,9,11,2);
    x2apic(b,1025,0xABCDEF01);x2apic(b,2,99);
    b.insert(b.end(),{127,3,99});
    check(run(b)&&count()==3,"Typ0/9, deaktiviert, Online-Capable, Duplikat, unbekannt");
    check(cpu(0)->id==2&&cpu(0)->uid==5&&cpu(1)->id==7,"LAPIC-Felder");
    check(cpu(2)->id==1025&&cpu(2)->uid==0xABCDEF01&&cpu(2)->flags==1,"x2APIC volle Breite");
    check(!run(header()),"Keine aktivierte CPU");
    b=header();x2apic(b,1000,1,2);check(!run(b),"Nur Online-Capable");
    b=header();word(b,4,43);check(!run(b,false),"Headerlänge zu klein");
    b=header();word(b,4,100);check(!run(b,false),"Länge größer als Ansicht");
    b=header();b.push_back(0);check(!run(b),"Ein letztes Byte");
    for(auto n:{0,1,7}){b=header();b.resize(52);b[45]=uint8_t(n);check(!run(b),"Typ0 zu kurz");}
    b=header();b.resize(59);b[44]=9;b[45]=15;check(!run(b),"Typ9 zu kurz");
    b=header();b.insert(b.end(),{9,16});check(!run(b),"Eintrag über Ende");
    b=header();for(unsigned i=0;i<64;++i)x2apic(b,1000+i,i);
    check(run(b)&&count()==64,"64 CPUs");
    x2apic(b,2000,64);check(!run(b)&&!valid(),"Überlauf nicht still kürzen");
    b=header();lapic(b,1,1);check(run(b)&&count()==1,"Neuer Parse setzt Zähler zurück");
    std::cout<<"PASS T11 MADT "<<checks<<" Prüfungen\n";
}
