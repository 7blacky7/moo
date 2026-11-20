// Testanschluss für den tatsächlichen Moo-Träger, keine Thread-Runtime in C++.
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
struct Value {uint64_t tag,data;};
struct Context {
    int32_t flag=0,depth=0;
    Value error{3,0};
    uint64_t location=0;
    int32_t reported=0,native=1;
    uint8_t worker=0;
    uint64_t post=0;
    bool ready=true;
};
static_assert(sizeof(Context)==64&&offsetof(Context,post)==48&&offsetof(Context,worker)==40);
static thread_local Context context;
struct HostMutex {std::mutex value;std::atomic<Context*> owner{nullptr};};
struct Backend {
    std::mutex gate;
    std::unordered_map<void*,std::unique_ptr<HostMutex>> mutexes;
    std::unordered_map<void*,std::unique_ptr<std::condition_variable>> conditions;
    std::vector<std::thread> threads;
    unsigned starts=0;
};
static Backend& backend(){static Backend* b=new Backend;return *b;}
static HostMutex& mutex_for(void* key){
    auto& b=backend();std::lock_guard<std::mutex> l(b.gate);
    auto& m=b.mutexes[key];if(!m)m=std::make_unique<HostMutex>();return *m;
}
static std::condition_variable& condition_for(void* key){
    auto& b=backend();std::lock_guard<std::mutex> l(b.gate);
    auto& c=b.conditions[key];if(!c)c=std::make_unique<std::condition_variable>();return *c;
}
extern "C" {
void* native_context() asm("moo_backend_kontext");
uint64_t native_spawn(uint64_t,uint64_t) asm("moo_backend_starten");
void native_lock(void*) asm("moo_backend_mutex_nimm");
bool native_unlock(void*) asm("moo_backend_mutex_gib");
void native_wait(void*,void*) asm("moo_backend_bedingung_warten");
void native_signal(void*) asm("moo_backend_bedingung_wecken");
void native_broadcast(void*) asm("moo_backend_alle_wecken");
void post_drain(void*) asm("__moo_native.laufzeit_post_leeren");
void post_close(void*) asm("__moo_native.laufzeit_post_schliessen");
void post_drop(void*) asm("__moo_native.laufzeit_post_freigeben");
int32_t native_status() asm("__moo_native.laufzeit_status");
}
extern "C" void* native_context(){return &context;}
extern "C" uint64_t native_spawn(uint64_t entry,uint64_t data){
    if(!entry)std::abort();
    auto& b=backend();std::lock_guard<std::mutex> l(b.gate);
    b.threads.emplace_back([entry,data]{
        context.worker=1;
        reinterpret_cast<void(*)(uint64_t)>(entry)(data);
        if(context.flag||context.depth||context.error.tag!=3||context.post)std::abort();
    });
    ++b.starts;return 1;
}
extern "C" void native_lock(void* key){
    auto& m=mutex_for(key);m.value.lock();
    if(m.owner.exchange(&context)!=nullptr)std::abort();
}
extern "C" bool native_unlock(void* key){
    auto& m=mutex_for(key);
    if(m.owner.exchange(nullptr)!=&context)std::abort();
    m.value.unlock();return true;
}
extern "C" void native_wait(void* cond,void* mutex){
    auto& cv=condition_for(cond);
    auto& m=mutex_for(mutex);
    if(m.owner.exchange(nullptr)!=&context)std::abort();
    std::unique_lock<std::mutex> lock(m.value,std::adopt_lock);
    cv.wait(lock);
    if(m.owner.exchange(&context)!=nullptr)std::abort();
    lock.release();
}
extern "C" void native_signal(void* cond){condition_for(cond).notify_one();}
extern "C" void native_broadcast(void* cond){condition_for(cond).notify_all();}
extern "C" void moo_backend_abgeben(){std::this_thread::yield();}
extern "C" void moo_backend_warten(uint64_t){/* Kernelvertrag: Abschluss in finish(). */}
extern "C" void moo_backend_ende(){}
extern "C" void moo_backend_ausgabe(const char* text,bool zeile){
    std::fputs(text,stdout);if(zeile)std::fputc('\n',stdout);
}
extern "C" [[noreturn]] void moo_backend_anhalten(){std::abort();}
extern "C" void moo_uefi_print(const char* text){std::puts(text);}
extern "C" [[noreturn]] void kern_panic(const char* text){
    std::fprintf(stderr,"FAIL Native-Backend Panik: %s\n",text);std::abort();
}
static void finish(){
    auto& b=backend();
    for(auto& t:b.threads)if(t.joinable())t.join();
    if(context.post){
        void* p=reinterpret_cast<void*>(context.post);
        post_drain(p);post_close(p);context.post=0;post_drop(p);
    }
    if(context.flag||context.depth||context.error.tag!=3||native_status()){
        std::fputs("FAIL Native-Backend Fehlerzustand\n",stderr);std::_Exit(1);
    }
    for(const auto& m:b.mutexes)if(m.second->owner.load()!=nullptr)std::abort();
    unsigned starts=b.starts;delete &b;
    std::fprintf(stderr,"PASS nativer Moo-Traeger, %u echte Hostthreads\n",starts);
}
__attribute__((constructor)) static void setup(){std::atexit(finish);}
