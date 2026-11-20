#include <cstdint>
#include <iostream>
extern "C" uint64_t null_probe();
extern "C" uint64_t null_rein();
extern "C" uint64_t null_kopie();
int main(){if(null_probe()!=0||null_rein()!=0||null_kopie()!=7)return 1;std::cout<<"PASS Nullstruktur: verschachtelt/Array/Atom/positive0bits, 3 Wiederinitialisierungen, Konstruktor/Kopie\n";}
