/*
 * The cost of the leaf itself, three ways, with the argument already in
 * memory as a 64-bit double -- which is the shape the real call sites have.
 *
 * The earlier run reported 1.4 ns for the inlined form, which is less than a
 * single divide takes; the loop had been hoisted.  Here the input comes from
 * an array the compiler cannot fold, the result goes to a volatile sink so
 * nothing is dead, and the divide is done up front so what is timed is the
 * leaf and not the arithmetic feeding it.
 */
#include <stdio.h>
#include <sys/time.h>

#define M 1024
static double in[M];
static volatile unsigned long sink;
static volatile double dsink;

static unsigned long fixCall(double v)
{ double s = v * 32768.0;
  return (unsigned long)(long)((s >= 0.0) ? (s + 0.5) : (s - 0.5)); }
static __inline__ unsigned long fixInl(double v)
{ double s = v * 32768.0;
  return (unsigned long)(long)((s >= 0.0) ? (s + 0.5) : (s - 0.5)); }
static __inline__ unsigned long fixInlVol(double v)
{ volatile double vv = v; double s = vv * 32768.0;
  return (unsigned long)(long)((s >= 0.0) ? (s + 0.5) : (s - 0.5)); }

static double clampCall(double v)
{ if (v > 255.0) return 255.0; if (v < -255.0) return -255.0; return v; }
static __inline__ double clampInl(double v)
{ if (v > 255.0) return 255.0; if (v < -255.0) return -255.0; return v; }
static __inline__ double clampInlVol(double v)
{ volatile double vv = v;
  if (vv > 255.0) return 255.0; if (vv < -255.0) return -255.0; return vv; }

static double now(void)
{ struct timeval t; gettimeofday(&t,(struct timezone*)0);
  return t.tv_sec + t.tv_usec/1000000.0; }

#define R 4000

int
main(void)
{
    int i, r;
    double t0, t1, a, b, c;
    unsigned long s;
    double d;

    for (i = 0; i < M; i++)
        in[i] = ((double)(i * 37 % 511) - 255.0) / 7.0;

    printf("leaf cost, argument already 64-bit in memory\n");
    printf("%d x %d = %d applications each\n\n", R, M, R * M);

    t0 = now();
    for (r = 0; r < R; r++) { s = 0;
        for (i = 0; i < M; i++) s += fixCall(in[i]); sink = s; }
    t1 = now(); a = t1 - t0;
    t0 = now();
    for (r = 0; r < R; r++) { s = 0;
        for (i = 0; i < M; i++) s += fixInl(in[i]); sink = s; }
    t1 = now(); b = t1 - t0;
    t0 = now();
    for (r = 0; r < R; r++) { s = 0;
        for (i = 0; i < M; i++) s += fixInlVol(in[i]); sink = s; }
    t1 = now(); c = t1 - t0;
    printf("  fixed   call %6.1f ns   inline %6.1f ns   forced %6.1f ns\n",
           a/(R*M)*1e9, b/(R*M)*1e9, c/(R*M)*1e9);
    printf("          inline saves %.1f ns (%.0f%%), forced saves %.1f ns (%.0f%%)\n",
           (a-b)/(R*M)*1e9, 100.0*(a-b)/a, (a-c)/(R*M)*1e9, 100.0*(a-c)/a);

    t0 = now();
    for (r = 0; r < R; r++) { d = 0.0;
        for (i = 0; i < M; i++) d += clampCall(in[i]); dsink = d; }
    t1 = now(); a = t1 - t0;
    t0 = now();
    for (r = 0; r < R; r++) { d = 0.0;
        for (i = 0; i < M; i++) d += clampInl(in[i]); dsink = d; }
    t1 = now(); b = t1 - t0;
    t0 = now();
    for (r = 0; r < R; r++) { d = 0.0;
        for (i = 0; i < M; i++) d += clampInlVol(in[i]); dsink = d; }
    t1 = now(); c = t1 - t0;
    printf("  clamp   call %6.1f ns   inline %6.1f ns   forced %6.1f ns\n",
           a/(R*M)*1e9, b/(R*M)*1e9, c/(R*M)*1e9);
    printf("          inline saves %.1f ns (%.0f%%), forced saves %.1f ns (%.0f%%)\n",
           (a-b)/(R*M)*1e9, 100.0*(a-b)/a, (a-c)/(R*M)*1e9, 100.0*(a-c)/a);
    return 0;
}
