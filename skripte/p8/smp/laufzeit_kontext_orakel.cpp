#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
extern "C" {
uint64_t choose(uint32_t,uint32_t) asm("__moo_native.t14_kontext_slot");
uint64_t context_size() asm("__moo_native.t14_kontext_groesse");
bool start(void*,uint8_t) asm("__moo_native.t14_kontext_start");
struct Value {uint64_t tag,data;};
}
struct Context {
    int32_t flag,depth;
    Value last;
    uint64_t location;
    int32_t reported,native;
    uint8_t worker;
    uint64_t mailbox;
    bool ready;
};
static unsigned checks;
static void check(bool ok,const char* name) {
    ++checks;if(!ok){std::cerr<<"FAIL T14 Kontext "<<name<<"\n";std::exit(1);}
}
static Context* select(uint32_t cpu,uint32_t slot) {
    return reinterpret_cast<Context*>(choose(cpu,slot));
}
int main() {
    check(context_size()==sizeof(Context),"C/Moo Layout");
    check(select(0,24)==nullptr,"ungueltiger Slot ohne Speicherzugriff");
    check(select(0,16)!=nullptr,"erweiterter Planerslot");
    check(select(0,23)!=nullptr,"letzter Planerslot");
    check(!start(nullptr,1),"Null-Kontext abgewiesen");
    auto* main=select(0,0);
    check(main->ready&&main->last.tag==3&&main->worker==0,"BSP-Hauptkontext");
    auto* a=select(0,2);
    check(a!=main&&a->worker==1,"Worker eigener Kontext");
    check(start(a,1),"frischer Start");
    a->flag=17;a->depth=3;
    a->last.tag=31;a->last.data=1234;
    auto* b=select(0,3);
    check(b!=a&&b->flag==0&&b->depth==0&&b->last.tag==3,"anderer Faden ohne Fehler");
    b->flag=29;
    check(select(0,2)==a&&a->flag==17&&a->depth==3,"Rueckkehr restauriert Kontext");
    check(a->last.tag==31&&a->last.data==1234,"Fehlerwert bleibt beim Faden");
    check(!start(a,1),"kein owning Fehler still verworfen");
    a->flag=0;a->depth=0;a->last={3,0};a->mailbox=1;
    check(!start(a,1),"kein Postfach still verworfen");
    a->mailbox=0;a->location=99;a->reported=1;
    check(start(a,1)&&a->location==0&&a->reported==0&&a->native==1,"sauberer Slot wiederverwendbar");
    uint64_t other=0;bool other_ok=false;
    std::thread cpu([&]{
        auto* c=select(1,2);other=reinterpret_cast<uint64_t>(c);
        other_ok=c->flag==0&&c->depth==0&&c->last.tag==3&&c->worker==1;
        c->flag=55;
    });
    cpu.join();
    check(other!=reinterpret_cast<uint64_t>(a)&&other_ok,"CPU-lokales Bild trennt gleiche Fadennummern");
    check(a->flag==0&&b->flag==29,"fremde CPU veraendert lokale Kontexte nicht");
    check(select(0,0)==main&&main->worker==0,"Hauptthreadmarkierung bleibt lokal");
    std::cout<<"PASS T14 Kontext "<<checks<<" Pruefungen\n";
}
