/* What the submit-timing instrumentation costs per submission. */
#include <stdio.h>
#include <sys/time.h>
#define N 200000
int main(void)
{
    struct timeval a, b; int i; double t0, t1;
    gettimeofday(&a,(struct timezone*)0);
    for (i = 0; i < N; i++) gettimeofday(&b,(struct timezone*)0);
    gettimeofday(&b,(struct timezone*)0);
    t0 = a.tv_sec + a.tv_usec/1e6; t1 = b.tv_sec + b.tv_usec/1e6;
    printf("gettimeofday: %.0f ns a call\n", (t1-t0)/N*1e9);
    printf("two a submission, 33.5 submissions a frame: %.3f ms a frame\n",
           (t1-t0)/N*2*33.5*1000.0);
    return 0;
}
