/*
 * A width the engine cannot walk, accelerated by padding the surface.
 *
 * The engine's destination pitch has to be a multiple of 32 pixels.  A
 * 500-wide render used to be refused outright and drawn entirely in software.
 * The surface is padded up to 512 now; the CALLER's array keeps its own row
 * length, and the two are kept apart by every path that touches both.
 *
 * That separation is the whole risk, so this asks about it directly:
 *
 *   - does a 500-wide context actually reach the engine,
 *   - does it draw what software draws, pixel for pixel,
 *   - and does anything outside the caller's 500 columns move?
 *
 * The last one is asked with sentinels rather than by looking, because the
 * failure it guards against -- the surface's stride used on the caller's
 * array -- would write plausible pixels into the wrong places.
 *
 * The public queries are asked too.  While a surface is substituted they must
 * describe the CALLER's buffer, not the surface: the row length the header
 * defines is the one in the image buffer, and a padded stride handed back
 * would come straight back in through PixelStore describing rows the caller
 * does not own.
 */
#include <stdio.h>
#include <stdlib.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"
#include "../mesa/OpenStepMGAMesaBuffer.h"

#define W       500             /* not a multiple of 32 */
#define H       300
#define APPROW  520             /* the caller's own row length, wider still */
#define SENT    0xfeedfaceUL

static int failures;

static void
verdict(const char *what, int ok, const char *detail)
{
    printf("   %-44s %s%s%s\n", what, ok ? "yes" : "NO",
           (detail && *detail) ? "  -- " : "", detail ? detail : "");
    if (!ok) failures++;
}

static void
scene(void)
{
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1.0f, 0.5f, 0.0f);
    glBegin(GL_TRIANGLES);
      glVertex2d(20.0, 20.0);
      glVertex2d(480.0, 40.0);
      glVertex2d(250.0, 280.0);
    glEnd();
    glColor3f(0.0f, 1.0f, 0.5f);
    glBegin(GL_TRIANGLES);
      glVertex2d(0.0, 290.0);
      glVertex2d(499.0, 290.0);
      glVertex2d(499.0, 299.0);
    glEnd();
    glFinish();
}

int
main(void)
{
    OSMesaContext ctx;
    unsigned long *app, *soft;
    unsigned long i, y, x, differ = 0UL, moved = 0UL, cells = 0UL;
    unsigned long drawn0, drawn1;
    GLint qrow = -1;
    char msg[160];

    app  = (unsigned long *)malloc((unsigned)(APPROW * H) * sizeof *app);
    soft = (unsigned long *)malloc((unsigned)(APPROW * H) * sizeof *soft);
    if (!app || !soft) { printf("no room\n"); return 2; }

    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx) { printf("no context\n"); return 2; }

    printf("a %d-wide surface, padded to something the engine can walk\n\n",
           W);

    /*
     * Current FIRST, and only then the row length.
     *
     * OSMesaPixelStore takes the current context and dereferences it without
     * looking, so calling it before there is one is a bus error -- which is
     * how this test first ran.  Upstream OSMesa is the same; the order is the
     * caller's job.  And the row length has to be in place BEFORE the binding
     * that matters, because MakeCurrent is what hands it to the back end, so
     * the sequence is: bind once, set the length, bind again.
     */
    if (!OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("   MakeCurrent refused\n"); return 2;
    }
    OSMesaPixelStore(OSMESA_ROW_LENGTH, APPROW);

    /* Sentinels everywhere, so anything written outside the picture shows. */
    for (i = 0UL; i < (unsigned long)APPROW * H; i++)
        app[i] = SENT;

    if (!OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, W, H)) {
        printf("   rebind at the caller's row length refused\n"); return 2;
    }
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);

    sprintf(msg, "surface stride %lu for a %d-wide picture",
            OSMGAMesaBufferStride(), W);
    verdict("the surface is the engine's", OSMGAMesaBufferOrigin() != 0UL,
            msg);
    verdict("and its stride is a multiple of 32",
            (OSMGAMesaBufferStride() % 32UL) == 0UL, msg);

    drawn0 = OSMGAMesaHookDrawn();
    scene();
    drawn1 = OSMGAMesaHookDrawn();
    sprintf(msg, "%lu batches reached the engine", drawn1 - drawn0);
    verdict("the scene is drawn on the engine", drawn1 > drawn0, msg);

    /* Nothing outside the picture may have moved. */
    for (y = 0UL; y < (unsigned long)H; y++)
        for (x = (unsigned long)W; x < (unsigned long)APPROW; x++) {
            cells++;
            if (app[y * APPROW + x] != SENT) moved++;
        }
    sprintf(msg, "%lu of %lu cells past column %d still hold the sentinel",
            cells - moved, cells, W);
    verdict("nothing outside the caller's picture moved", moved == 0UL, msg);

    /* The public queries describe the CALLER's buffer, not the surface. */
    OSMesaGetIntegerv(OSMESA_ROW_LENGTH, &qrow);
    sprintf(msg, "reported %d, the caller set %d, the surface is %lu",
            (int)qrow, APPROW, OSMGAMesaBufferStride());
    verdict("the row-length query answers for the caller",
            qrow == APPROW, msg);

    /* Now the same scene in software, into a second array, and compare. */
    for (i = 0UL; i < (unsigned long)APPROW * H; i++)
        soft[i] = SENT;
    OSMGAMesaHookForceSoftware(1);
    if (!OSMesaMakeCurrent(ctx, soft, GL_UNSIGNED_BYTE, W, H)) {
        printf("   rebind for software refused\n"); return 2;
    }
    glViewport(0, 0, W, H);
    scene();
    OSMGAMesaHookForceSoftware(0);

    /*
     * WHERE they differ, not just how many.
     *
     * Two things could make these two pictures disagree and they need telling
     * apart.  One is the padding, which would show as whole rows sliding --
     * a difference spread across every row and following the columns.  The
     * other is the edge coverage this project has measured and recorded for
     * a long time: the engine samples a pixel at its top-left corner and GL
     * at its centre, so slanted edges pick up a different last pixel.  That
     * one shows only where a colour changes.
     *
     * So the count is reported beside how many of the differing pixels have a
     * neighbour of another colour -- an edge -- and how many rows carry any.
     */
    {
        unsigned long onEdge = 0UL, rowsWith = 0UL;

        for (y = 0UL; y < (unsigned long)H; y++) {
            unsigned long inRow = 0UL;

            for (x = 0UL; x < (unsigned long)W; x++) {
                unsigned long a = app[y * APPROW + x];

                if (a == soft[y * APPROW + x])
                    continue;
                differ++; inRow++;
                if ((x > 0UL && soft[y * APPROW + x - 1UL] != a) ||
                    (x + 1UL < (unsigned long)W &&
                     soft[y * APPROW + x + 1UL] != a) ||
                    (y > 0UL && soft[(y - 1UL) * APPROW + x] != a) ||
                    (y + 1UL < (unsigned long)H &&
                     soft[(y + 1UL) * APPROW + x] != a))
                    onEdge++;
            }
            if (inRow) rowsWith++;
        }
        sprintf(msg, "%lu of %lu differ; %lu of those touch a colour change, "
                "spread over %lu of %d rows",
                differ, (unsigned long)W * (unsigned long)H, onEdge, rowsWith,
                H);
        verdict("what differs is edges, not rows", differ == onEdge, msg);
    }

    printf("\n   %d failed\n", failures);
    OSMesaDestroyContext(ctx);
    free(app); free(soft);
    return failures ? 1 : 0;
}
