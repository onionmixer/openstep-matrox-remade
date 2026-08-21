/*
 * openstep-mga-mesa-binding-test.c -- does a refusal actually refuse?
 *
 * The back end substitutes a video-memory surface for the buffer the
 * application gave OSMesa, and declines to do so for a second context, a
 * colour order it cannot produce, a size it cannot fit, and a row length the
 * engine cannot walk.  Declining is supposed to leave OSMesa drawing into the
 * application's own buffer exactly as it would without this back end.
 *
 * These are the cases where it may not.  Each one is run in its own process,
 * because the surface is process-global and handed out once.
 *
 * Written BEFORE the repair, and expected to fail.  A test that has never
 * failed is not evidence that anything was fixed.
 *
 *   cc -O -Wall -o /tmp/bind openstep-mga-mesa-binding-test.c \
 *      -I<mesa>/include -L<built> -lGL_mga
 *   /tmp/bind <case>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>

extern unsigned long OSMGAMesaHookDrawn(void);
extern unsigned long OSMGAMesaHookDeclined(void);
extern unsigned long OSMGAMesaBufferOrigin(void);
extern void OpenStepMesaAccelReleaseBuffer(void *ctx);

#define W       320             /* a width the engine can walk: 320 = 32 x 10 */
#define H       240
#define WIDE    352             /* another it can walk, for the row-length case */
#define BAD     333             /* one it cannot */
#define MAXROW  (WIDE > BAD ? WIDE : BAD)

#define CLEARC  0xFF102030UL    /* what glClear leaves */
#define TRIC    0xFF00FF00UL    /* what the triangle leaves */

static unsigned long drewMark, declMark;
static void mark(void) { drewMark = OSMGAMesaHookDrawn();
                         declMark = OSMGAMesaHookDeclined(); }
static unsigned long drewSince(void) { return OSMGAMesaHookDrawn() - drewMark; }
static unsigned long declSince(void) { return OSMGAMesaHookDeclined() - declMark; }

static void
scene(void)
{
    glClearColor(0x10 / 255.0f, 0x20 / 255.0f, 0x30 / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
      glColor3ub(0x00, 0xFF, 0x00);
      glVertex3f( 4.5f,  4.5f, 0.0f);
      glVertex3f(60.5f,  4.5f, 0.0f);
      glVertex3f( 4.5f, 60.5f, 0.0f);
    glEnd();
    glFinish();
}

static void
project(int w, int h)
{
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)w, 0.0, (double)h, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
}

/*
 * Read the application's buffer at the row length in force, and say how many
 * of the sampled places hold what the scene put there.  Reading it at the
 * WRONG row length is exactly the failure being looked for, so the stride is
 * a parameter and never assumed.
 */
static int
picture(const unsigned long *app, int rowLen, const char *what)
{
    unsigned long inside  = app[10 * rowLen + 10];   /* inside the triangle */
    unsigned long outside = app[10 * rowLen + 200];  /* clear, right of it */
    unsigned long far     = app[100 * rowLen + 100]; /* clear, well below */
    int ok = (inside == TRIC) && (outside == CLEARC) && (far == CLEARC);

    printf("   %-34s at stride %3d: (10,10)=%08lx (200,10)=%08lx "
           "(100,100)=%08lx  %s\n", what, rowLen, inside, outside, far,
           ok ? "as drawn" : "NOT as drawn");
    return ok;
}

int
main(int argc, char **argv)
{
    int which = (argc > 1) ? atoi(argv[1]) : 0;
    unsigned long *app;
    OSMesaContext a;
    int bad = 0;

    /* Big enough for any row length used below, so that a wrong stride
     * misplaces pixels rather than running off the end. */
    app = (unsigned long *)malloc((unsigned)(MAXROW * H) * sizeof(unsigned long));
    if (!app) { printf("no room\n"); return 2; }
    memset(app, 0, (unsigned)(MAXROW * H) * sizeof(unsigned long));

    a = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!a || !OSMesaMakeCurrent(a, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    project(W, H);

    /* Every case starts by proving the hardware is there and drawing.  A
     * later delta of zero would otherwise mean "no acceleration on this
     * machine" just as well as "correctly declined". */
    mark(); scene();
    printf("case %d: baseline accelerated draw: drew=%lu declined=%lu "
           "surface=%lu\n", which, drewSince(), declSince(),
           OSMGAMesaBufferOrigin());
    if (drewSince() == 0UL) {
        printf("   the baseline did not accelerate; nothing below means "
               "anything\n");
        return 2;
    }
    if (!picture(app, W, "baseline")) bad++;

    switch (which) {
    case 1:
        /*
         * The row length changed after the context is current.  The surface
         * is laid out at 320; OSMesa now addresses rows 333 apart.  Whatever
         * happens, the picture must end up in the application's buffer at
         * 333, because that is what the application asked for.
         */
        OSMesaPixelStore(OSMESA_ROW_LENGTH, BAD);
        mark(); scene();
        printf("   after row length %d: drew=%lu declined=%lu\n",
               BAD, drewSince(), declSince());
        if (drewSince() != 0UL) { printf("   STILL ACCELERATED\n"); bad++; }
        if (declSince() != 0UL) { printf("   and a batch was declined\n"); bad++; }
        if (!picture(app, BAD, "after the row length changed")) bad++;
        break;

    case 2:
        /*
         * The same context bound again at a different size.  The allocator
         * refuses -- there is only one surface -- so OSMesa goes back to the
         * application's buffer, and nothing should be drawn by the engine.
         */
        /*
         * The SAME width, so the row length still matches the surface's
         * stride, and only the height differs.  Written at 352 first, which
         * proved nothing: a different width also changes the row length, and
         * the state update refuses a mismatched one -- so the surface was
         * left alone for a reason that has nothing to do with ownership.
         * The hole only shows when the stride agrees and the binding does
         * not.
         */
        if (!OSMesaMakeCurrent(a, app, GL_UNSIGNED_BYTE, W, H / 2)) {
            printf("   the rebind failed outright\n"); return 2;
        }
        project(W, H / 2);
        printf("   after rebinding at %dx%d: surface=%lu\n", W, H / 2,
               OSMGAMesaBufferOrigin());
        mark(); scene();
        printf("   drew=%lu declined=%lu\n", drewSince(), declSince());
        if (drewSince() != 0UL) { printf("   STILL ACCELERATED\n"); bad++; }
        if (!picture(app, W, "after rebinding at another size")) bad++;
        break;

    case 3: {
        /*
         * A second context.  It is refused the surface, so it must render in
         * software; and when it goes away it must not take the first
         * context's surface with it.
         */
        OSMesaContext b = OSMesaCreateContext(OSMESA_ARGB, NULL);
        unsigned long *appB = (unsigned long *)
            malloc((unsigned)(MAXROW * H) * sizeof(unsigned long));

        if (!b || !appB) { printf("   no second context\n"); return 2; }
        memset(appB, 0, (unsigned)(MAXROW * H) * sizeof(unsigned long));
        if (!OSMesaMakeCurrent(b, appB, GL_UNSIGNED_BYTE, W, H)) {
            printf("   the second context would not bind\n"); return 2;
        }
        project(W, H);
        mark(); scene();
        printf("   second context: drew=%lu declined=%lu surface=%lu\n",
               drewSince(), declSince(), OSMGAMesaBufferOrigin());
        if (drewSince() != 0UL) { printf("   B WAS ACCELERATED\n"); bad++; }
        if (!picture(appB, W, "the second context's own buffer")) bad++;

        OSMesaDestroyContext(b);
        printf("   after destroying the second: surface=%lu\n",
               OSMGAMesaBufferOrigin());
        if (OSMGAMesaBufferOrigin() == 0UL) {
            printf("   IT TOOK THE FIRST CONTEXT'S SURFACE\n"); bad++;
        }
        /* And the first context must still be able to draw with it. */
        if (!OSMesaMakeCurrent(a, app, GL_UNSIGNED_BYTE, W, H)) {
            printf("   the first context would not come back\n"); bad++;
            break;
        }
        project(W, H);
        mark(); scene();
        printf("   the first context again: drew=%lu declined=%lu\n",
               drewSince(), declSince());
        if (drewSince() == 0UL) { printf("   IT LOST ACCELERATION\n"); bad++; }
        if (!picture(app, W, "the first context after B went away")) bad++;
        break;
    }

    case 4:
        /*
         * The forced cleanup the fork path uses: a null context means "let go
         * of everything", and the child depends on it.  Any ownership test
         * added to the release has to leave this working.
         */
        OpenStepMesaAccelReleaseBuffer(0);
        printf("   after ReleaseBuffer(NULL): surface=%lu\n",
               OSMGAMesaBufferOrigin());
        if (OSMGAMesaBufferOrigin() != 0UL) {
            printf("   THE FORCED CLEANUP DID NOTHING\n"); bad++;
        }
        break;

    case 5:
        /*
         * The orientation turned over after the context is current.  Same
         * trigger as the row length, different consequence: the back end
         * draws bottom-up and says so, and a surface it shares cannot simply
         * be read the other way round.
         */
        OSMesaPixelStore(OSMESA_Y_UP, 0);
        mark(); scene();
        printf("   after Y_UP off: drew=%lu declined=%lu\n",
               drewSince(), declSince());
        if (drewSince() != 0UL) { printf("   STILL ACCELERATED\n"); bad++; }
        break;

    default:
        printf("   usage: 1 row length, 2 rebind, 3 second context, "
               "4 forced release, 5 y-up\n");
        return 2;
    }

    printf("case %d: %d thing%s wrong\n", which, bad, (bad == 1) ? "" : "s");
    return bad ? 1 : 0;
}
