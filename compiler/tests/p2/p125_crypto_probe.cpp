// P2-T125: unabhaengige Randwert-/Vektororakel gegen bestehende C-Provider.
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <regex>
#include <set>
#if defined(__x86_64__)
#include <cpuid.h>
#endif
extern "C" {
void wide(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t*) asm("__moo_native.p125_wide");
bool equal_bytes(uint8_t*,uint64_t,uint8_t*,uint64_t) asm("__moo_native.p125_bytes");
void wipe(uint8_t*,uint64_t) asm("__moo_native.p125_wipe");
uint64_t deadwipe() asm("__moo_native.p125_deadwipe");
void chacha(uint32_t*,uint32_t*) asm("__moo_native.p125_chacha");
void sha_block(uint32_t*,uint32_t*) asm("__moo_native.p125_sha_block");
bool random_word(uint64_t*) asm("__moo_native.p125_rdrand");
bool seed_word(uint64_t*) asm("__moo_native.p125_rdseed");
bool has_random() asm("__moo_native.p125_has_rdrand");
bool has_seed() asm("__moo_native.p125_has_rdseed");
void moo_krypto_chacha20_block(const uint8_t*,uint32_t,const uint8_t*,uint8_t*);
void moo_krypto_sha256(const uint8_t*,size_t,uint8_t*);
int crypto_verify16(const uint8_t*,const uint8_t*);
int crypto_verify32(const uint8_t*,const uint8_t*);
int crypto_verify64(const uint8_t*,const uint8_t*);
void crypto_wipe(void*,size_t);
}
static size_t checks;
static void check(bool ok, const char* name) {
    ++checks;
    if(!ok) { std::cerr << "FAIL P125 " << name << "\n"; std::exit(1); }
}
static uint64_t high64(uint64_t a,uint64_t b) {
    const uint64_t mask=UINT32_MAX, a0=a&mask,a1=a>>32,b0=b&mask,b1=b>>32;
    const uint64_t t=a1*b0+((a0*b0)>>32);
    const uint64_t u=(t&mask)+a0*b1;
    return a1*b1+(t>>32)+(u>>32);
}
static uint64_t next(uint64_t& s) { s=s*UINT64_C(6364136223846793005)+1; return s; }
static uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
static std::vector<uint8_t> hex(const char* p) {
    std::vector<uint8_t> r;
    while(*p) {
        const auto n=[](char c){return c<='9'?c-'0':c-'a'+10;};
        r.push_back(uint8_t((n(p[0])<<4)|n(p[1]))); p+=2;
    }
    return r;
}
static void multiply_tests() {
    const uint64_t values[]={0,1,2,UINT32_MAX,UINT64_C(1)<<32,UINT64_C(1)<<63,UINT64_MAX-1,UINT64_MAX};
    auto test=[](uint64_t a,uint64_t b,uint64_t c,uint64_t d) {
        uint64_t out[2]={}, lo=a*b, hi=high64(a,b);
        const uint64_t first=lo+c; hi+=(first<lo);
        const uint64_t second=first+d; hi+=(second<first);
        wide(a,b,c,d,out);
        check(out[0]==second && out[1]==hi,"breites Mul-Add");
    };
    for(auto a:values)for(auto b:values)for(auto c:values)for(auto d:values)test(a,b,c,d);
    uint64_t s=91;
    for(size_t i=0;i<10000;++i) {
        const auto a=next(s),b=next(s),c=next(s),d=next(s); test(a,b,c,d);
    }
}
static void bytes_tests() {
    check(equal_bytes(nullptr,0,nullptr,0),"leerer Vergleich");
    uint8_t singleton=1;
    check(!equal_bytes(nullptr,0,&singleton,1),"ungleiche Laengen");
    wipe(nullptr,0);
    for(size_t n=1;n<=257;++n) {
        std::vector<uint8_t>a(n+2,0xA5),b=a,c=a;
        check(equal_bytes(a.data()+1,n,b.data()+1,n),"gleiche Bytes");
        for(size_t i=0;i<n;++i) {
            b[i+1]^=uint8_t(1u<<(i%8));
            check(!equal_bytes(a.data()+1,n,b.data()+1,n),"Unterschied an jeder Position");
            if(n==16)check((crypto_verify16(a.data()+1,b.data()+1)==0)==equal_bytes(a.data()+1,n,b.data()+1,n),"Monocypher16");
            if(n==32)check((crypto_verify32(a.data()+1,b.data()+1)==0)==equal_bytes(a.data()+1,n,b.data()+1,n),"Monocypher32");
            if(n==64)check((crypto_verify64(a.data()+1,b.data()+1)==0)==equal_bytes(a.data()+1,n,b.data()+1,n),"Monocypher64");
            b[i+1]=a[i+1];
        }
        wipe(a.data()+1,n); crypto_wipe(c.data()+1,n);
        check(a==c,"Wipe gleich C, Nachbarbytes unveraendert");
    }
    check(deadwipe()==7,"unbeobachtetes lokales Wipe");
}
static void chacha_test(uint64_t& s, bool vector) {
    uint8_t key[32],nonce[12],provider[64];
    for(size_t i=0;i<32;++i)key[i]=vector?uint8_t(i):uint8_t(next(s)>>56);
    for(auto& v:nonce)v=0;
    if(vector){nonce[3]=9;nonce[7]=0x4a;}
    else for(auto& v:nonce)v=uint8_t(next(s)>>56);
    uint32_t input[16]={0x61707865,0x3320646e,0x79622d32,0x6b206574},out[16];
    for(size_t i=0;i<8;++i)input[4+i]=le32(key+4*i);
    input[12]=vector?1:uint32_t(next(s));
    for(size_t i=0;i<3;++i)input[13+i]=le32(nonce+4*i);
    chacha(input,out); moo_krypto_chacha20_block(key,input[12],nonce,provider);
    for(size_t i=0;i<16;++i)check(out[i]==le32(provider+4*i),"ChaCha C-Provider");
    if(vector) {
        const auto expected=hex("10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4ed2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e");
        check(std::memcmp(provider,expected.data(),64)==0,"RFC8439 2.3.2");
    }
}
static void sha_test(const std::vector<uint8_t>& msg,const char* vector) {
    check(msg.size()<56,"ein SHA-Block");
    uint8_t padded[64]={},provider[32],actual[32];
    std::copy(msg.begin(),msg.end(),padded); padded[msg.size()]=0x80;
    const uint64_t bits=msg.size()*8;
    for(size_t i=0;i<8;++i)padded[63-i]=uint8_t(bits>>(8*i));
    uint32_t words[16],out[8];
    for(size_t i=0;i<16;++i)words[i]=(uint32_t(padded[4*i])<<24)|(uint32_t(padded[4*i+1])<<16)|(uint32_t(padded[4*i+2])<<8)|padded[4*i+3];
    sha_block(words,out);
    for(size_t i=0;i<32;++i)actual[i]=uint8_t(out[i/4]>>(24-8*(i%4)));
    moo_krypto_sha256(msg.data(),msg.size(),provider);
    check(std::memcmp(actual,provider,32)==0,"SHA256 C-Provider");
    if(vector){const auto expected=hex(vector);check(std::memcmp(actual,expected.data(),32)==0,"NIST SHA256");}
}
static void rng_tests() {
#if defined(__x86_64__)
    unsigned a,b,c,d; const unsigned max=__get_cpuid_max(0,nullptr);
    bool r=false,s=false;
    if(max>=1){__cpuid_count(1,0,a,b,c,d); r=(c&(1u<<30))!=0;}
    if(max>=7){__cpuid_count(7,0,a,b,c,d); s=(b&(1u<<18))!=0;}
    check(has_random()==r && has_seed()==s,"CPUID C-Vergleich");
    size_t rs=0,ss=0;
    for(size_t i=0;i<1024;++i){
        uint64_t x=UINT64_C(0xfeed12345678abcd),y=x;
        const bool rx=random_word(&x),sy=seed_word(&y);
        check(rx||x==UINT64_C(0xfeed12345678abcd),"CF=false RDRAND bewahrt Ziel");
        check(sy||y==UINT64_C(0xfeed12345678abcd),"CF=false RDSEED bewahrt Ziel");
        check((!rx||r)&&(!sy||s),"fehlendes CPU-Merkmal liefert false");
        rs+=rx;ss+=sy;
    }
    check(!r||rs>0,"RDRAND liefert CF-Erfolg");
    check(!s||ss>0,"RDSEED liefert CF-Erfolg");
    std::cout<<"RNG C-Merkmale rdrand="<<r<<" rdseed="<<s<<" Erfolge="<<rs<<"/"<<ss<<"\n";
#endif
}

static size_t replace_all(std::string& s,const std::string& from,const std::string& to) {
    size_t count=0,pos=0;
    while((pos=s.find(from,pos))!=std::string::npos){s.replace(pos,from.size(),to);pos+=to.size();++count;}
    return count;
}
// Modell am erzeugten IR: nur CPU/CF ersetzt, Guards/Zielpfad bleiben erhalten.
static void rng_model_ir(const char* input,const char* output,bool success) {
    std::ifstream in(input); check(bool(in),"Modell-IR lesbar");
    std::ostringstream data;data<<in.rdbuf();std::string ir=data.str();
    check(replace_all(ir,R"("cpuid")",R"("movl $$7,$0\0Amovl $$262144,$1\0Amovl $$1073741824,$2\0Axorl $3,$3")")>=4,"CPUID-Modell greift");
    const auto code=success?R"("movq $$0,$0\0Amovb $$1,$1")":R"("movq $$12345,$0\0Amovb $$0,$1")";
    check(replace_all(ir,R"("rdrand $0\0Asetc $1")",code)==1,"RDRAND-CF-Modell greift");
    check(replace_all(ir,R"("rdseed $0\0Asetc $1")",code)==1,"RDSEED-CF-Modell greift");
    std::ofstream out(output);out<<ir;check(bool(out),"Modell-IR geschrieben");
}
static void rng_model_run(bool success) {
    check(has_random()&&has_seed(),"Modell-Merkmale");
    for(auto fn:{random_word,seed_word}) {
        uint64_t value=UINT64_C(0xfeed12345678abcd);
        const bool cf=fn(&value);
        check(cf==success,"exakter CF");
        check(value==(success?0:UINT64_C(0xfeed12345678abcd)),"CF=1 schreibt auch Null; CF=0 verwirft Instruktionswert");
    }
    std::cout<<"PASS P125 deterministisches CF-Modell "<<success<<"\n";
}
// Konservative SSA-Datenflusspruefung an optimierten CT-Bytefunktionen.
// Laengen/Zeiger sind oeffentlich, alle Byte-Loads geheim.
static void ct_ir(const char* path) {
    std::ifstream input(path);check(bool(input),"CT-IR lesbar");
    const std::regex variable(R"(%[-a-zA-Z0-9._]+)");
    std::vector<std::string> body;std::string line,name;
    size_t functions=0,branches=0,loads=0,dead_stores=0;
    auto audit=[&](){
        if(name.find("p125_deadwipe")!=std::string::npos){
            for(const auto& l:body)if(l.find("store volatile i8 0")!=std::string::npos)++dead_stores;
        }
        const bool critical=name.find("p125_bytes(")!=std::string::npos
            ||name.find("p125_geliehen(")!=std::string::npos||name.find("@__moo_ct_bytes(")!=std::string::npos;
        if(!critical)return;
        ++functions;std::set<std::string> secret;bool changed;
        do{
            changed=false;
            for(const auto& l:body) {
                const auto eq=l.find(" = ");if(eq==std::string::npos)continue;
                std::smatch match;
                const auto left=l.substr(0,eq),right=l.substr(eq+3);
                if(!std::regex_search(left,match,variable))continue;
                const auto dest=match.str();
                bool tainted=right.find("load volatile i8")!=std::string::npos
                    ||right.find("load i8")!=std::string::npos||right.find("call i1 @__moo_ct_bytes")!=std::string::npos;
                for(std::sregex_iterator i(right.begin(),right.end(),variable),end;i!=end;++i)
                    tainted=tainted||secret.count(i->str())!=0;
                if(tainted&&secret.insert(dest).second)changed=true;
            }
        }while(changed);
        for(const auto& l:body) {
            check(l.find("@memcmp")==std::string::npos&&l.find("@bcmp")==std::string::npos,"kein frueh abbrechender Bibliotheksvergleich");
            if(l.find("load volatile i8")!=std::string::npos)++loads;
            const bool branch=l.find("br i1 ")!=std::string::npos||l.find("switch ")!=std::string::npos;
            const bool address=l.find("getelementptr ")!=std::string::npos;
            if(branch||address) {
                const auto rhs=address?l.substr(l.find(" = ")+3):l.substr(0,l.find(", label"));
                for(std::sregex_iterator i(rhs.begin(),rhs.end(),variable),end;i!=end;++i)
                    check(secret.count(i->str())==0,branch?"geheime Sprungbedingung":"geheime Adresse");
                if(branch)++branches;
            }
        }
    };
    while(std::getline(input,line)){
        if(line.rfind("define ",0)==0){name=line;body.clear();}
        else if(line=="}"){audit();name.clear();body.clear();}
        else if(!name.empty())body.push_back(line);
    }
    check(functions>=2&&loads>=4&&branches>=4,"CT-Pruefung erfasst Funktionen/Loads/Guards");
    check(dead_stores>=8,"unbeobachtetes lokales Wipe bleibt erhalten");
    std::cout<<"PASS P125 CT-IR "<<path<<": "<<functions<<" Funktionen, "<<loads<<" geheime Loads, "<<branches<<" oeffentliche Spruenge, "<<dead_stores<<" tote volatile Stores\n";
}

int main(int argc,char** argv){
    if(argc==5&&std::string(argv[1])=="--rng-model-ir"){rng_model_ir(argv[2],argv[3],std::string(argv[4])=="1");return 0;}
    if(argc==3&&std::string(argv[1])=="--rng-model"){rng_model_run(std::string(argv[2])=="1");return 0;}
    if(argc==3&&std::string(argv[1])=="--ct-ir"){ct_ir(argv[2]);return 0;}

    multiply_tests();bytes_tests();
    uint64_t s=101;chacha_test(s,true);
    sha_test({},"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    sha_test({'a','b','c'},"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    for(size_t i=0;i<128;++i) {
        chacha_test(s,false);
        std::vector<uint8_t>m(i%56);for(auto&v:m)v=uint8_t(next(s)>>56);sha_test(m,nullptr);
    }
    rng_tests();
    std::cout<<"PASS P125 Krypto: "<<checks<<" Orakelpruefungen, RFC/NIST, drei C-Provider\n";
}
