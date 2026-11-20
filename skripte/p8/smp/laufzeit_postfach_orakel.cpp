#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
struct Value {uint64_t tag,data;};
struct Post {int32_t refs;uint32_t lock;bool alive;Post* heir;void* list;};
extern "C" {
Post* create(int32_t) asm("__moo_native.laufzeit_post_neu");
void drop(Post*) asm("__moo_native.laufzeit_post_freigeben");
void drain(Post*) asm("__moo_native.laufzeit_post_leeren");
void close_post(Post*) asm("__moo_native.laufzeit_post_schliessen");
void inherit(Post*,Post*) asm("__moo_native.laufzeit_post_uebernehmen");
void give(Post*,Post*,Value*) asm("__moo_native.laufzeit_post_abgeben");
}
static std::array<std::atomic<unsigned>,8010> released{};
static thread_local bool owner=false;
static unsigned checks;
static void check(bool ok,const char* label){++checks;if(!ok){std::cerr<<"FAIL Postfach "<<label<<"\n";std::exit(1);}}
extern "C" void* kern_c_moo_alloc(uint64_t n){void* p=std::malloc(n);if(!p)std::abort();return p;}
extern "C" void kern_c_moo_release(Value v){
    if(!owner||v.tag!=4||v.data>=released.size()||released[v.data].fetch_add(1)!=0)std::abort();
}
static void put(Post* p,Post* me,uint64_t id){Value v{4,id};give(p,me,&v);}
int main(){
    static_assert(sizeof(Post)==32);
    owner=true;
    Post* main=create(1);Post* worker=create(1);
    check(main->refs==1&&main->alive&&main->heir==nullptr&&main->list==nullptr,"Layout/Initialzustand");
    put(main,main,1);check(released[1]==1,"direkte eigene Freigabe");
    put(worker,main,2);check(released[2]==0,"keine fremde Refcount-Aenderung");
    drain(worker);check(released[2]==1&&worker->list==nullptr,"Besitzer leert");
    put(worker,main,3);close_post(worker);check(!worker->alive&&released[3]==1,"Schliessen leert Restliste");
    put(worker,main,4);check(released[4]==0,"nach Ende vor Join behalten");
    inherit(worker,main);check(released[4]==1&&worker->heir==main&&main->refs==2,"Join uebernimmt Liste und Erbkante");
    put(worker,main,5);check(released[5]==1,"spaete Freigabe an Joiner");
    Post* middle=create(1);Post* leaf=create(1);
    close_post(leaf);inherit(leaf,middle);close_post(middle);inherit(middle,main);
    put(leaf,main,6);check(released[6]==1,"transitive Erbkette");
    drop(leaf);drop(middle);drop(worker);check(main->refs==1,"Erbkanten geben exakt +1 ab");
    std::array<std::thread,4> senders;
    std::atomic<unsigned> finished{0};
    for(size_t t=0;t<senders.size();++t)senders[t]=std::thread([&,t]{
        for(unsigned n=0;n<2000;++n)put(main,nullptr,10+t*2000+n);
        finished.fetch_add(1);
    });
    while(finished.load()!=4){drain(main);std::this_thread::yield();}
    for(auto& t:senders)t.join();
    drain(main);
    bool all=true;for(size_t i=10;i<8010;++i)all=all&&released[i]==1;
    check(all,"8000 konkurrierende Uebergaben, genau einmal im Besitzer");
    check(main->refs==1&&main->list==nullptr,"keine Restwerte oder Postfachreferenzen");
    close_post(main);drop(main);
    std::cout<<"PASS T14 Postfach "<<checks<<" Pruefungen\n";
}
