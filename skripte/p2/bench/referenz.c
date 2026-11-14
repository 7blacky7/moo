#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "moo_noise.h"
typedef struct { double x,y; } Punkt;
__attribute__((noinline)) static double builtin(double x) { return fabs(x); }
int main(int argc, char **argv) {
 if(argc!=2) return 2;
 const char *k=argv[1]; double s=0; int i;
 if(!strcmp(k,"summe")) { for(i=0;i<10000000;i++) s+=i; }
 else if(!strcmp(k,"vektor")) {
   float *a=malloc(10000000*sizeof(float)),*b=malloc(10000000*sizeof(float)),*c=malloc(10000000*sizeof(float));
   if(!a||!b||!c) return 3;
   for(i=0;i<10000000;i++) { a[i]=i%100; b[i]=i%100; }
   for(i=0;i<10000000;i++) c[i]=a[i]+b[i];
   for(i=0;i<10000000;i++) s+=c[i]; free(a); free(b); free(c);
 } else if(!strcmp(k,"fuellen")) {
   float *a=malloc(10000000*sizeof(float)); if(!a)return 3;
   for(i=0;i<10000000;i++) a[i]=i%100;
   for(i=0;i<10000000;i++) s+=a[i]; free(a);
 } else if(!strcmp(k,"matrix")) {
   float m[16]={1,2,3,4,2,3,4,5,3,4,5,6,4,5,6,7};
   for(i=0;i<1000000;i++) { float x=i%100;
      float a=m[0]*x+m[1]*2+m[2]*3+m[3]*4, b=m[4]*x+m[5]*2+m[6]*3+m[7]*4;
      float c=m[8]*x+m[9]*2+m[10]*3+m[11]*4, d=m[12]*x+m[13]*2+m[14]*3+m[15]*4;
      s+=a+b+c+d;
   }
 } else if(!strcmp(k,"objekte")) {
   Punkt *p=malloc(100000*sizeof(Punkt)); if(!p)return 3;
   for(i=0;i<100000;i++) p[i]=(Punkt){i%100,2};
   for(i=0;i<100000;i++) s+=p[i].x+p[i].y; free(p);
 } else if(!strcmp(k,"builtin")) {
   for(i=0;i<10000000;i++) s+=builtin(i%100-50);
 } else if(!strcmp(k,"rauschen")) {
   for(int y=0;y<512;y++) for(int x=0;x<512;x++)
     s+=moo_noise_perlin2(moo_noise_scramble_seed(17),(float)(x/32.0),(float)(y/32.0));
 } else return 2;
 printf("%.9g\n",s); return 0;
}
