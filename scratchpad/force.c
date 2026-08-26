/* What forcing the argument to 64 bits costs, against the call it replaces. */
#include <stdio.h>
#include <sys/time.h>

static unsigned long fixCall(double v)
{ double s = v * 32768.0;
  return (unsigned long)(long)((s >= 0.0) ? (s + 0.5) : (s - 0.5)); }

static __inline__ unsigned long fixInl(double v)
{ double s = v * 32768.0;
  return (unsigned long)(long)((s >= 0.0) ? (s + 0.5) : (s - 0.5)); }

static __inline__ unsigned long fixInlVol(double v)
{ volatile double vv = v; double s = vv * 32768.0;
  return (unsigned long)(long)((s >= 0.0) ? (s + 0.5) : (s - 0.5)); }

static double now(void)
{ struct timeval t; gettimeofday(&t,(struct timezone*)0);
  return t.tv_sec + t.tv_usec/1000000.0; }

#define N 2000000

int main(void)
{
    int i; unsigned long s; double t0, t1, a, b, c;
    static double num[16], den[16];
    for (i = 0; i < 16; i++) { num[i] = (double)(i*7+1); den[i] = (double)(i+3); }

    t0=now(); s=0; for(i=0;i<N;i++) s += fixCall(num[i&15]/den[i&15]);
    t1=now(); a=t1-t0;
    t0=now(); s=0; for(i=0;i<N;i++) s += fixInl(num[i&15]/den[i&15]);
    t1=now(); b=t1-t0;
    t0=now(); s=0; for(i=0;i<N;i++) s += fixInlVol(num[i&15]/den[i&15]);
    t1=now(); c=t1-t0;

    printf("%d iterations\n", N);
    printf("  call            %.3f s  %6.1f ns\n", a, a/N*1e9);
    printf("  inline          %.3f s  %6.1f ns\n", b, b/N*1e9);
    printf("  inline + forced %.3f s  %6.1f ns\n", c, c/N*1e9);
    printf("  forced keeps %.0f%% of the win\n",
           100.0*(a-c)/(a-b));
    printf("(sum %lu)\n", s);
    return 0;
}
