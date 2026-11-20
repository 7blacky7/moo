#pragma once
template<class T> struct Vorlage { Vorlage(); T wert(); };
struct Basis {};
struct Erbe: Basis { Erbe(); int wert(); };
struct Referenz { Referenz(); int wert(int& x); };
struct Zeiger { Zeiger(); int* wert(); };
struct BoolTyp { BoolTyp(); bool wert(); };
struct Variadisch { Variadisch(); int wert(int, ...); };
struct Virtuell { Virtuell(); virtual int wert(); };
struct Standardarg { Standardarg(); int wert(int n = 3); };
struct Statisch { Statisch(); static int wert(); };
struct OhneKonstruktor { int wert(); };
struct Privat { private: Privat(); };
