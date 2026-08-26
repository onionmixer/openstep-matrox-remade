/* Does this cc inline a static leaf at -O, and what does a call cost? */
#include <stdio.h>
#include <sys/time.h>

static double clampCall(double v)
{ if (v > 255.0) return 255.0; if (v < -255.0) return -255.0; return v; }

static __inline__ double clampInl(double v)
{ if (v > 255.0) return 255.0; if (v < -255.0) return -255.0; return v; }

#define CLAMP_MAC(v) ((v) > 255.0 ? 255.0 : ((v) < -255.0 ? -255.0 : (v)))

static double now(void)
{ struct timeval t; gettimeofday(&t,(struct timezone*)0);
  return t.tv_sec + t.tv_usec/1000000.0; }

#define N 3000000

int main(void)
{
    int i; double s, t0, t1, a, b, c;
    double src[16];
    for (i = 0; i < 16; i++) src[i] = (double)(i * 40 - 300);

    t0 = now(); s = 0.0;
    for (i = 0; i < N; i++) s += clampCall(src[i & 15]);
    t1 = now(); a = t1 - t0;

    t0 = now(); s = 0.0;
    for (i = 0; i < N; i++) s += clampInl(src[i & 15]);
    t1 = now(); b = t1 - t0;

    t0 = now(); s = 0.0;
    for (i = 0; i < N; i++) s += CLAMP_MAC(src[i & 15]);
    t1 = now(); c = t1 - t0;

    printf("%d calls: static %.3f s, __inline__ %.3f s, macro %.3f s\n",
           N, a, b, c);
    printf("per call: static %.1f ns, __inline__ %.1f ns, macro %.1f ns\n",
           a/N*1e9, b/N*1e9, c/N*1e9);
    printf("(sum %g, kept so nothing is dead)\n", s);
    return 0;
}
