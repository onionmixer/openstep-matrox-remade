/*
 * Getting a real GL texture into video memory.
 *
 * The chooser does not accept textured state yet, so nothing here draws
 * through GL: the residency layer is called directly and video memory is read
 * back.  What that can still prove is everything about the copy -- that the
 * bytes arrive in the engine's order, that redefining an image gives its
 * block back, that changing one texel makes the copy stale, and that deleting
 * the texture returns the arena.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>
#include <GL/gl.h>

extern int OSMGAMesaTexResidentCurrent(void *ctx, unsigned long *origin,
                                       unsigned long *w, unsigned long *h,
                                       unsigned long *pitch);
extern void *OSMGAMesaBufferTextureMap(const void *ctx, unsigned long *origin,
                                       unsigned long *bytes);
extern void OSMGAMesaTexArenaStat(unsigned long *count, unsigned long *used);
extern unsigned long OSMGAMesaTexUploads(void);
extern unsigned long OSMGAMesaTexRefused(void);

#define W 320
#define H 240
#define TW 16
#define TH 8

static int failures;

static void
say(const char *what, int ok, unsigned long got, unsigned long want)
{
    printf("  %-50s %s", what, ok ? "ok" : "FAIL");
    if (!ok) printf("  got %lu wanted %lu", got, want);
    printf("\n");
    if (!ok) failures++;
}

/* every texel different in all four channels, so a swizzle cannot hide */
static void
fill(GLubyte *p, int w, int h, int bump)
{
    int x, y;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            GLubyte *t = p + (y * w + x) * 4;

            t[0] = (GLubyte)(x * 4 + 1 + bump);      /* R */
            t[1] = (GLubyte)(y * 8 + 2 + bump);      /* G */
            t[2] = (GLubyte)(x + y + 3 + bump);      /* B */
            t[3] = (GLubyte)(200 + bump);            /* A, which the engine
                                                      * does not keep */
        }
}

int
main(void)
{
    OSMesaContext ctx;
    unsigned long *app;
    GLubyte *img;
    GLuint tex;
    unsigned long org, w, h, pitch, aOrg, aLen, n0, u0, n1, u1;
    volatile unsigned long *vram;
    int x, y, wrong;

    app = (unsigned long *)malloc((unsigned)(W * H) * sizeof(unsigned long));
    img = (GLubyte *)malloc(TW * TH * 4);
    if (!app || !img) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    glViewport(0, 0, W, H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    vram = (volatile unsigned long *)OSMGAMesaBufferTextureMap(ctx, &aOrg, &aLen);
    if (vram == 0) { printf("no arena\n"); return 2; }
    printf("arena at %lu, %lu bytes\n\n", aOrg, aLen);

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    fill(img, TW, TH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TW, TH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img);

    OSMGAMesaTexArenaStat(&n0, &u0);
    say("nothing is resident before it is asked for", n0 == 0UL, n0, 0UL);

    say("the texture becomes resident",
        OSMGAMesaTexResidentCurrent(ctx, &org, &w, &h, &pitch), 0, 1);
    say("at the size it was given", w == (unsigned long)TW && h == (unsigned long)TH,
        w, (unsigned long)TW);
    OSMGAMesaTexArenaStat(&n1, &u1);
    say("and takes one block", n1 == 1UL, n1, 1UL);

    /* the bytes, in the engine's order */
    wrong = 0;
    for (y = 0; y < TH; y++)
        for (x = 0; x < TW; x++) {
            GLubyte *t = img + (y * TW + x) * 4;
            unsigned long want = ((unsigned long)t[0] << 16)
                               | ((unsigned long)t[1] << 8)
                               |  (unsigned long)t[2];

            if (vram[(org - aOrg) / 4UL + y * pitch + x] != want) wrong++;
        }
    say("every texel arrives as 0x00RRGGBB", wrong == 0, (unsigned long)wrong, 0UL);

    /* one texel changed under an unchanged pointer */
    {
        GLubyte one[4];
        unsigned long before = OSMGAMesaTexUploads();

        one[0] = 9; one[1] = 8; one[2] = 7; one[3] = 6;
        glTexSubImage2D(GL_TEXTURE_2D, 0, 3, 2, 1, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, one);
        (void)OSMGAMesaTexResidentCurrent(ctx, &org, &w, &h, &pitch);
        say("a sub-image makes the copy stale",
            OSMGAMesaTexUploads() == before + 1UL,
            OSMGAMesaTexUploads(), before + 1UL);
        say("and the new texel is in video memory",
            vram[(org - aOrg) / 4UL + 2 * pitch + 3] == 0x090807UL,
            vram[(org - aOrg) / 4UL + 2 * pitch + 3], 0x090807UL);
    }

    /* redefinition at another size gives the block back */
    {
        GLubyte *big = (GLubyte *)malloc(32 * 16 * 4);

        fill(big, 32, 16, 5);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 16, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, big);
        OSMGAMesaTexArenaStat(&n1, &u1);
        say("redefining hands the old block back", n1 == 0UL, n1, 0UL);
        say("and the new size becomes resident",
            OSMGAMesaTexResidentCurrent(ctx, &org, &w, &h, &pitch) &&
            w == 32UL && h == 16UL, w, 32UL);
        free(big);
    }

    /* deletion returns the arena */
    glDeleteTextures(1, &tex);
    OSMGAMesaTexArenaStat(&n1, &u1);
    say("deleting returns the arena", n1 == 0UL && u1 == 0UL, n1, 0UL);

    printf("\nuploads %lu, refusals %lu\n",
           OSMGAMesaTexUploads(), OSMGAMesaTexRefused());
    printf("%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===", failures);
    return failures ? 1 : 0;
}
