#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
struct Value {uint64_t tag,data;};
struct String {unsigned refs;std::string text;};
extern "C" {
Value moo_atom_neu(Value);Value moo_atom_laden(Value);
Value moo_atom_speichern(Value,Value);Value moo_atom_addieren(Value,Value);
Value moo_atom_tauschen(Value,Value);Value moo_atom_vergleichen_und_tauschen(Value,Value,Value);
void moo_atom_free(void*);
}
static unsigned checks,strings;
static Value error{3,0};
static void check(bool ok,const char* label){++checks;if(!ok){std::cerr<<"FAIL Atom-API "<<label<<"\n";std::exit(1);}}
static uint64_t bits(double n){uint64_t r;std::memcpy(&r,&n,8);return r;}
static Value number(double n){return {0,bits(n)};}
static String* str(Value v){return reinterpret_cast<String*>(v.data);}
extern "C" void* moo_alloc(uint64_t n){void* p=std::malloc(n);if(!p)std::abort();return p;}
extern "C" const char* moo_type_name(Value v){switch(v.tag){case 0:return "Zahl";case 1:return "Text";case 3:return "Nichts";default:return "Testwert";}}
extern "C" Value moo_string_new(const char* text){++strings;return {1,reinterpret_cast<uint64_t>(new String{1,text})};}
extern "C" Value moo_add(Value a,Value b){return moo_string_new((str(a)->text+str(b)->text).c_str());}
extern "C" void moo_release(Value v){if(v.tag==1&&--str(v)->refs==0){delete str(v);--strings;}}
extern "C" void moo_throw(Value v){check(error.tag==3,"kein ueberschriebener owning Fehler");error=v;}
static void expect_error(Value r,const std::string& expected){check(r.tag==3&&error.tag==1&&str(error)->text==expected,"genaue fangbare Diagnose");moo_release(error);error={3,0};check(strings==0,"Fehlertext-Zwischenwerte freigegeben");}
int main(){
    Value a=moo_atom_neu(number(5));check(a.tag==32&&a.data!=0,"echtes Atom-Tag/Heap");
    check(*reinterpret_cast<int32_t*>(a.data)==1,"Header-Refcount");
    check(moo_atom_laden(a).data==bits(5),"Laden");
    check(moo_atom_addieren(a,number(2.5)).data==bits(5)&&moo_atom_laden(a).data==bits(7.5),"Add alter Wert");
    check(moo_atom_tauschen(a,number(-0.0)).data==bits(7.5),"Tausch alter Wert");
    check(moo_atom_vergleichen_und_tauschen(a,number(0),number(2)).data==1,"+0/-0 numerisch");
    check(moo_atom_speichern(a,{0,UINT64_C(0x7FF8000012345678)}).tag==3,"Speichern nichts");
    check(moo_atom_laden(a).data==UINT64_C(0x7FF8000012345678),"NaN Payload");
    check(moo_atom_vergleichen_und_tauschen(a,{0,UINT64_C(0x7FF8000012345678)},number(1)).data==0,"NaN ist ungleich");
    expect_error(moo_atom_laden(number(5)),"atom_laden erwartet ein Atom, erhalten: Zahl");
    expect_error(moo_atom_laden({32,0}),"atom_laden erwartet ein Atom, erhalten: Testwert");
    expect_error(moo_atom_neu({3,0}),"atom erwartet eine Zahl, erhalten: Nichts");
    expect_error(moo_atom_speichern(a,{3,0}),"atom_speichern erwartet eine Zahl, erhalten: Nichts");
    expect_error(moo_atom_addieren(a,{3,0}),"atom_addieren erwartet eine Zahl, erhalten: Nichts");
    expect_error(moo_atom_tauschen(a,{3,0}),"atom_tauschen erwartet eine Zahl, erhalten: Nichts");
    expect_error(moo_atom_vergleichen_und_tauschen(a,{3,0},number(1)),"atom_vergleichen_und_tauschen erwartet eine Zahl, erhalten: Nichts");
    expect_error(moo_atom_vergleichen_und_tauschen(a,number(1),{3,0}),"atom_vergleichen_und_tauschen erwartet eine Zahl, erhalten: Nichts");
    moo_atom_free(reinterpret_cast<void*>(a.data));
    check(strings==0&&error.tag==3,"kein Fehler-/Textbesitz uebrig");
    std::cout<<"PASS T14 Atom-API "<<checks<<" Pruefungen\n";
}
