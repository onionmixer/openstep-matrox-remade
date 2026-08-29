/*
 * M20 -- is the mirror's cost linear in PIXELS, or does a row cost something
 * of its own?
 *
 * M20 measured that a mirror narrowed to what each bracket drew would move
 * 113 times fewer pixels on the teapot, and that only becomes a speed-up if
 * pixels are what the mirror is paying for.  The evidence so far is two
 * FULL-SURFACE sizes agreeing (146.722 ms at 512x384 predicts 229.3 ms at
 * 640x480; 229.9 measured).  Neither is a small box, and a row-by-row copy
 * has a per-row cost: 129 boxes of 52x52 are 6708 rows, where one full
 * surface is 480.
 *
 * So this times the same copy over shapes chosen to separate the two costs:
 *
 *   full         480 rows x 640     the mirror itself
 *   52x52        the teapot's measured average bracket box
 *   1 x 307200-ish  one enormous row     -- pixels with almost no rows
 *   307200-ish x 1  one column           -- rows with almost no pixels
 *
 * If cost is pixels alone, the wide row and the tall column cost the same.
 * If a row costs something, the column costs far more.  The answer is a
 * number, not an argument.
 *
 * Nothing here draws and nothing here touches the card: the box copy reads
 * the mapped surface and writes this program's own array, which is what the
 * mirror does, and it leaves the dirty flag alone.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaBuffer.h"

#define W 640
#define H 480

static double
now(void)
{
    struct timeval t;
    gettimeofday(&t, (struct timezone *)0);
    return (double)t.tv_sec + (double)t.tv_usec / 1e6;
}

static double
timeBox(unsigned long x0, unsigned long y0, unsigned long x1, unsigned long y1,
        int reps)
{
    double t0;
    int i;

    OSMGAMesaBufferMirrorBox(x0, y0, x1, y1);   /* warm, not counted */
    t0 = now();
    for (i = 0; i < reps; i++)
        OSMGAMesaBufferMirrorBox(x0, y0, x1, y1);
    return (now() - t0) / (double)reps;
}

static void
row(const char *what, unsigned long x0, unsigned long y0,
    unsigned long x1, unsigned long y1, int reps)
{
    double s = timeBox(x0, y0, x1, y1, reps);
    unsigned long px = (x1 - x0 + 1UL) * (y1 - y0 + 1UL);
    unsigned long rows = y1 - y0 + 1UL;

    printf("   %-14s %7lu px %6lu rows  %9.3f ms   %7.1f ns/px  %8.1f us/row\n",
           what, px, rows, s * 1e3, s * 1e9 / (double)px,
           s * 1e6 / (double)rows);
}

int
main(void)
{
    OSMesaContext ctx;
    unsigned long *app;

    app = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    if (!app) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    /* One frame, so the surface exists and is the engine's. */
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    if (OSMGAMesaBufferOrigin() == 0UL) {
        printf("NOT TESTED -- the surface is not the engine's, so this would\n"
               "be timing a copy between two malloc'd arrays\n");
        return 1;
    }

    printf("the mirror's cost, by shape (%dx%d surface)\n\n", W, H);
    row("full",      0UL, 0UL, (unsigned long)W - 1UL, (unsigned long)H - 1UL, 5);
    row("52x52",     0UL, 0UL, 51UL, 51UL, 200);
    row("one row",   0UL, 0UL, (unsigned long)W - 1UL, 0UL, 200);
    row("one column",0UL, 0UL, 0UL, (unsigned long)H - 1UL, 200);
    row("half rows", 0UL, 0UL, (unsigned long)W - 1UL, (unsigned long)H / 2UL - 1UL, 10);
    row("half cols", 0UL, 0UL, (unsigned long)W / 2UL - 1UL, (unsigned long)H - 1UL, 10);

    printf("\n   if the two halves cost the same, the price is pixels;\n"
           "   if 'half cols' costs more, a row costs something of its own\n");
    OSMesaDestroyContext(ctx);
    free(app);
    return 0;
}
