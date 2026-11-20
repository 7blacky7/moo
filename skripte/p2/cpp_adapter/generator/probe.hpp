#pragma once
inline int adapter_lebende = 0;
class Zaehler {
    int zahl;
public:
    Zaehler() : zahl(0) { ++adapter_lebende; }
    explicit Zaehler(int n) : zahl(n) { if(n < 0) throw 7; ++adapter_lebende; }
    ~Zaehler() { --adapter_lebende; }
    int wert() const { return zahl; }
    void addiere(int n) { if(n < 0) throw 9; zahl += n; }
    int mal(int n) const { if(n < 0) throw 11; return zahl*n; }
    double mal(double n) const { return zahl*n; }
    unsigned long long gross() const { return 18446744073709551615ULL; }
};
struct Fremd {
    Fremd() {}
    int wert() const { return 42; }
};
