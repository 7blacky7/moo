#include "adapter.cpp"
#include <cassert>
#include <cstdint>
int main() {
    auto* z=tg_Zaehler_k_neu_i32_abi(4); assert(z && adapter_lebende==1);
    int32_t out=77;
    assert(tg_Zaehler_m_wert_leer_const_abi(z,&out)==0 && out==4);
    assert(tg_Zaehler_m_addiere_i32_abi(z,3)==0);
    assert(tg_Zaehler_m_mal_i32_const_abi(z,5,&out)==0 && out==35);
    double d=0; assert(tg_Zaehler_m_mal_f64_const_abi(z,0.5,&d)==0 && d==3.5);
    uint64_t u=0; assert(tg_Zaehler_m_gross_leer_const_abi(z,&u)==0 && u==UINT64_MAX);
    out=77; assert(tg_Zaehler_m_mal_i32_const_abi(z,-1,&out)==-2 && out==77);
    assert(tg_abi_fehler()==-2);
    assert(tg_Zaehler_m_wert_leer_const_abi(nullptr,&out)==-1 && out==77);
    assert(tg_Zaehler_m_wert_leer_const_abi(z,nullptr)==-1);
    assert(tg_Zaehler_frei_abi(z)==0 && adapter_lebende==0);
    assert(tg_Zaehler_frei_abi(nullptr)==-1);
    assert(tg_Zaehler_k_neu_i32_abi(-1)==nullptr && adapter_lebende==0 && tg_abi_fehler()==-2);
    z=tg_Zaehler_k_neu_leer_abi(); assert(z);
    assert(tg_Zaehler_frei_abi(z)==0 && adapter_lebende==0);
}
