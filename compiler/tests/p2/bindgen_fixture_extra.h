#ifndef P2_EXTRA_H
#define P2_EXTRA_H
#define P2_OCTAL 012
#define P2_EXPR (1 + 2)
#define P2_HUGE 1e999
#define P2_TINY 1e-999
int *p2_echo(int *value);
_Bool p2_bool(_Bool value);
int p2_fnptr(int (*callback)(int));
enum { P2_ANON = 17 };
enum OtherMode { P2_FROM_NAMED = 23 };
typedef int P2Alias;
int p2_attr(int *p __attribute__((annotate("owned"))));
int p2_oldstyle();
int guard(void);
extern int p2_global;
struct P2Array { int values[2][3]; };
struct P2Aligned { int x __attribute__((aligned(16))); };
#endif
