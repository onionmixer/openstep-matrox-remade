/*
 * Does the hardware depth buffer survive the size the application asked for?
 *
 * The depth surface is mapped at an offset past the colour surface, and that
 * offset used to be rounded to 4096 on a machine whose page is 8192.  The
 * device refuses an offset that is not a whole page -- EINVAL rather than a
 * silent rounding -- so the mapping failed, the accessor returned nothing,
 * and Mesa went back to its own depth buffer without a word.
 *
 * Whether it lands right depends on h*w being a multiple of 2048, which
 * 64x64 and 1024x768 happen to satisfy and 800x600 and 320x240 do not.  Every
 * test in this tree had used one of the first two.
 *
 * One size per run, because the accessor keeps the first mapping it makes.
 *
 *   cc -O -Wall -o /tmp/zsize openstep-mga-mesa-depth-size-test.c \
 *      -I<mesa>/include -L<built> -lGL_mga
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>

extern unsigned long OSMGAMesaBufferDepthOrigin(void);
extern unsigned long OSMGAMesaBufferOrigin(void);

/* Enough of the window to tell "nowhere to put depth" from "nowhere it could
 * map".  The driver logs this figure; repeating it here keeps the test
 * needing nothing but the library. */
#define VRAM_LEN    3145728UL

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    void *appbuf;
    int w = (argc > 1) ? atoi(argv[1]) : 64;
    int h = (argc > 2) ? atoi(argv[2]) : 64;
    unsigned long colour, depth;

    if (w <= 0 || h <= 0) { printf("bad size\n"); return 2; }
    appbuf = malloc((unsigned)(w * h * 4));
    if (appbuf == 0) { printf("no room for the application buffer\n"); return 2; }
    memset(appbuf, 0, (unsigned)(w * h * 4));

    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, appbuf, GL_UNSIGNED_BYTE, w, h)) {
        printf("no context at %dx%d\n", w, h);
        return 2;
    }

    colour = OSMGAMesaBufferOrigin();
    depth  = OSMGAMesaBufferDepthOrigin();

    /* h*w must be a multiple of 2048 for the old rounding to have landed on a
     * page by accident; printed so the result can be read against the reason
     * rather than only against a pass or a fail. */
    {
        /*
         * A depth origin of zero means two quite different things and they
         * must not print the same.  If the colour surface has already filled
         * the window there is simply nowhere left to put depth -- a capacity
         * limit with nothing to do with alignment.  1024x768 at four bytes a
         * pixel is exactly the whole window, which is why it reports zero
         * both before this fix and after it.
         */
        unsigned long colourBytes = (unsigned long)w * (unsigned long)h * 4UL;
        const char *what;

        if (colour == 0UL)
            what = "(software: no accelerated surface)";
        else if (depth != 0UL)
            what = "hardware depth";
        else if (colourBytes >= VRAM_LEN)
            what = "no room for depth (capacity, not alignment)";
        else
            what = "SOFTWARE DEPTH";

        printf("%4dx%-4d  h*w=%-9d  h*w%%2048=%-5d  colour=%-9lu  "
               "depth=%-9lu  %s\n",
               w, h, w * h, (w * h) % 2048, colour, depth, what);
        if (colour != 0UL && depth == 0UL && colourBytes < VRAM_LEN) {
            OSMesaDestroyContext(ctx);
            free(appbuf);
            return 1;
        }
    }

    OSMesaDestroyContext(ctx);
    free(appbuf);
    return 0;
}
