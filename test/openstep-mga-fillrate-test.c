/*
 * What it costs to write a whole surface's worth of system memory.
 *
 * The frame-cost measurement says a clear-and-draw frame walks the video
 * memory surface twice and that one of the two walks exists only because the
 * clear gets its own render bracket.  A whole-surface clear leaves the surface
 * holding one constant, so that walk could be replaced by writing the same
 * constant into the caller's array -- but only if writing system memory is
 * much cheaper than reading video memory, which is 5.36 MB/s here.
 *
 * This measures the replacement before anything is built around it.  Two
 * shapes are timed: the row-by-row loop the mirror uses, so the comparison is
 * like for like, and a straight run over the whole block, to show what the
 * row structure costs when the strides are equal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

static double
now(void)
{
    struct timeval t;
    gettimeofday(&t, (struct timezone *)0);
    return (double)t.tv_sec + (double)t.tv_usec / 1e6;
}

static void
run(int w, int h)
{
    unsigned long *buf;
    double t0, t1, rows, flat;
    unsigned long px = (unsigned long)w * (unsigned long)h;
    unsigned long bytes = px * sizeof(unsigned long);
    int i;

    buf = (unsigned long *)malloc((unsigned)bytes);
    if (!buf) { printf("   %dx%d: no room\n", w, h); return; }

    /* touch it once so the pages exist before either timing */
    for (i = 0; i < (int)px; i++) buf[i] = 0UL;

    t0 = now();
    for (i = 0; i < 20; i++) {
        unsigned long y;
        for (y = 0UL; y < (unsigned long)h; y++) {
            unsigned long *d = buf + y * (unsigned long)w;
            unsigned long x;
            for (x = 0UL; x < (unsigned long)w; x++)
                d[x] = 0xff123456UL;
        }
    }
    t1 = now();
    rows = (t1 - t0) / 20.0 * 1000.0;

    t0 = now();
    for (i = 0; i < 20; i++) {
        unsigned long k;
        for (k = 0UL; k < px; k++)
            buf[k] = 0xff123456UL;
    }
    t1 = now();
    flat = (t1 - t0) / 20.0 * 1000.0;

    printf("   %-9s %9lu B   row loop %8.3f ms (%6.1f MB/s)   flat %8.3f ms"
           " (%6.1f MB/s)\n",
           (w == 128) ? "128x96" : (w == 512 ? "512x384" : "1024x768"),
           bytes, rows, bytes / rows / 1000.0, flat, bytes / flat / 1000.0);
    free(buf);
}

int
main(void)
{
    printf("what writing a constant over a whole surface costs, in system"
           " memory\n\n");
    printf("   compare against the mirror, which READS video memory at"
           " 5.36 MB/s:\n");
    printf("      128x96 %.3f ms   512x384 %.3f ms   1024x768 %.3f ms\n\n",
           128.0 * 96 * 4 / 5.36e6 * 1000.0,
           512.0 * 384 * 4 / 5.36e6 * 1000.0,
           1024.0 * 768 * 4 / 5.36e6 * 1000.0);
    run(128, 96);
    run(512, 384);
    run(1024, 768);
    return 0;
}
