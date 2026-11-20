// Testgegenstelle: echte native ICR-Kodierung, keine Kopie der Startlogik.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>
extern "C" {
bool icr(uint64_t,uint64_t,uint64_t,uint32_t,uint32_t) asm("__moo_native.t12_icr");
bool map_ok(uint64_t,uint64_t,uint64_t) asm("__moo_native.t12_karte");
bool overlap(uint64_t,uint64_t) asm("__moo_native.ap_lowmem_belegt");
}
static std::vector<std::pair<uint64_t,uint64_t>> writes;
static unsigned reads=0,busy=0;
extern "C" uint64_t read_reg(uint64_t reg) {
    if(reg!=0x300)std::abort();
    ++reads;return reads<=busy?0x1000:0;
}
extern "C" void write_reg(uint64_t reg,uint64_t value) {writes.emplace_back(reg,value);}
static unsigned checks;
static void check(bool value,const char* name) {
    ++checks;if(!value){std::cerr<<"FAIL T12 "<<name<<"\n";std::exit(1);}
}
static bool send(uint64_t mode,uint32_t id,uint32_t cmd,unsigned pending=0) {
    writes.clear();reads=0;busy=pending;
    return icr(mode,reinterpret_cast<uint64_t>(&read_reg),reinterpret_cast<uint64_t>(&write_reg),id,cmd);
}
static bool descriptor(uint32_t type,uint64_t base,uint64_t pages,uint64_t step=40) {
    uint64_t words[5]={type,base,0,pages,0};
    return map_ok(reinterpret_cast<uint64_t>(words),sizeof(words),step);
}
int main() {
    check(send(0,0x45,0xC500),"xAPIC INIT");
    check(writes==std::vector<std::pair<uint64_t,uint64_t>>{{0x310,0x45000000},{0x300,0xC500}},"xAPIC high vor low");
    check(send(0,255,0x608),"xAPIC letzte ID");
    check(writes.at(0).second==0xFF000000&&writes.at(1).second==0x608,"SIPI-Vektor8");
    check(!send(0,256,0x608)&&writes.empty(),"xAPIC-ID nicht abschneiden");
    check(send(1,0x12345678,0x608),"x2APIC breite ID");
    check(reads==0&&writes==std::vector<std::pair<uint64_t,uint64_t>>{{0x300,UINT64_C(0x1234567800000608)}},"x2APIC einzelnes64Bit-ICR");
    check(send(0,1,0x8500,1),"ICR wartet auf delivery");
    check(reads>=2,"Busy tatsächlich gelesen");
    check(!send(0,1,0x608,1000000)&&writes.empty(),"ICR-Timeout schreibt nichts");
    for(auto type:{1u,2u,3u,4u,7u})check(descriptor(type,0x8000,1),"Lowmem erlaubter UEFI-Typ");
    for(auto type:{0u,5u,6u,8u,9u,10u,11u,12u})check(!descriptor(type,0x8000,1),"Lowmem reserviert/runtime/ACPI/MMIO");
    check(descriptor(7,0,9),"Seite im größeren Deskriptor");
    check(!descriptor(7,0,8),"Deskriptor endet vor Trampolin");
    check(!descriptor(7,0x9000,1),"Deskriptor beginnt danach");
    check(!descriptor(7,0x8000,0),"Null Seiten");
    check(!descriptor(7,0x8000,UINT64_MAX),"Seitenzahlüberlauf");
    check(!descriptor(7,0x8000,1,39),"Kurzer Deskriptorschritt");
    check(!map_ok(0,0,40),"Leere Karte");
    check(!overlap(0x8000,0),"Leerer Livebereich");
    check(!overlap(0x7000,0x1000),"Livebereich endet exakt davor");
    check(overlap(0x7000,0x1001),"Livebereich überlappt Anfang");
    check(overlap(0x8000,1)&&overlap(0x8FFF,1),"Livebereich im Trampolin");
    check(!overlap(0x9000,1),"Livebereich beginnt exakt danach");
    std::cout<<"PASS T12 ICR/Lowmem "<<checks<<" Prüfungen\n";
}
