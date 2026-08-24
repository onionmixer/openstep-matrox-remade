/*
 * A spinning triangle, live on the OPENSTEP screen.
 *
 * This is the first program in this tree whose output is the DISPLAY rather
 * than a number or a file.  Every frame is drawn into the offscreen VRAM
 * surface and presented by the engine -- a VRAM-to-VRAM blit into the
 * visible framebuffer -- so the picture never crosses the bus and the
 * caller's array is never brought up to date (present mode declares it
 * stale).
 *
 * It exists to answer one question honestly: with the delivery structure
 * fixed, is the hardware path actually FASTER than software on the very same
 * screen path?  So it has two arms.  "hw" lets the engine rasterise; "soft"
 * forces every triangle and clear through Mesa's software rasteriser, which
 * then writes the same VRAM surface through the CPU mapping -- and both arms
 * end each frame with the same Present.  Same scene, same surface, same
 * screen; only who rasterises differs.
 *
 * What each frame reports is what codex review asked for: median, p95 and
 * minimum rather than a single average, the GL half and the Present half
 * separately (render-bound or present-bound is the first question the
 * numbers must answer), and the engine counters so each arm can PROVE which
 * arm it was.
 *
 * Deliberate, recorded limits of the sample: the blit is not synchronised to
 * scanout (it may tear), the window server does not know these pixels (a
 * passing cursor or menu scribbles on them; the next frame heals it), and
 * the projection is flipped (glOrtho top/bottom swapped) because memory row
 * nought lands on the top screen row and a blit cannot flip.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"
#include "../mesa/OpenStepMGAMesaBuffer.h"
#include "OpenStepMGAHW3D.h"

#define W 640
#define H 480
#define DSTX 192L               /* centred on a 1024x768 screen */
#define DSTY 144L

static double
now(void)
{
    struct timeval t;
    gettimeofday(&t, (struct timezone *)0);
    return (double)t.tv_sec + (double)t.tv_usec / 1e6;
}

static int
cmpd(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static const char *
presentWhy(unsigned long v)
{
    switch (v) {
    case OSMGA_PRESENT_OK:      return "ok";
    case OSMGA_PRESENT_E_MAGIC: return "magic";
    case OSMGA_PRESENT_E_SRC:   return "source rect";
    case OSMGA_PRESENT_E_DST:   return "dest rect";
    case OSMGA_PRESENT_E_GEOM:  return "geometry";
    case OSMGA_PRESENT_E_BUSY:  return "busy";
    case OSMGA_PRESENT_E_LATCH: return "acceleration latched off";
    case OSMGA_PRESENT_E_MODE:  return "mode/window";
    default:                    return "unknown";
    }
}

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    unsigned long *buf;
    int soft = (argc > 1 && strcmp(argv[1], "soft") == 0);
    int frames = (argc > 2) ? atoi(argv[2]) : 240;
    double *glMs, *prMs, *totMs;
    double a = 0.0, t0, t1, t2;
    unsigned long verdict = 0UL;
    unsigned long drawn0, drawn1, clears0, clears1;
    int i;

    if (frames < 10) frames = 10;
    glMs = (double *)malloc((unsigned)frames * sizeof *glMs);
    prMs = (double *)malloc((unsigned)frames * sizeof *prMs);
    totMs = (double *)malloc((unsigned)frames * sizeof *totMs);
    buf = (unsigned long *)malloc((unsigned)(W * H) * sizeof *buf);
    if (!glMs || !prMs || !totMs || !buf) { printf("no room\n"); return 2; }

    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }
    if (OSMGAMesaBufferOrigin() == 0UL) {
        printf("the surface is not the engine's -- present cannot work\n");
        return 2;
    }
    OSMGAMesaBufferPresentMode(1);

    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)W, (double)H, 0.0, -1.0, 1.0);   /* flipped: upright */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);
    glClearColor(0.06f, 0.08f, 0.14f, 1.0f);

    printf("a spinning triangle on the screen -- %s arm, %d frames\n\n",
           soft ? "SOFTWARE" : "HARDWARE", frames);

    if (soft)
        OSMGAMesaHookForceSoftware(1);
    drawn0 = OSMGAMesaHookDrawn();
    clears0 = OSMGAMesaHookClears();

    for (i = 0; i < frames; i++) {
        double cx = 320.0, cy = 240.0, r = 180.0;

        t0 = now();
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
          glColor3f(1.0f, 0.25f, 0.2f);
          glVertex2d(cx + r * cos(a),                 cy + r * sin(a));
          glColor3f(0.2f, 1.0f, 0.35f);
          glVertex2d(cx + r * cos(a + 2.0944),        cy + r * sin(a + 2.0944));
          glColor3f(0.25f, 0.4f, 1.0f);
          glVertex2d(cx + r * cos(a + 4.1888),        cy + r * sin(a + 4.1888));
        glEnd();
        glFinish();
        t1 = now();

        if (OSMGAMesaBufferPresent(DSTX, DSTY, &verdict) != 0) {
            printf("present REFUSED at frame %d: %s\n", i, presentWhy(verdict));
            if (soft) OSMGAMesaHookForceSoftware(0);
            return 1;
        }
        t2 = now();

        glMs[i] = (t1 - t0) * 1000.0;
        prMs[i] = (t2 - t1) * 1000.0;
        totMs[i] = (t2 - t0) * 1000.0;
        a += 0.0523598;                       /* three degrees */
    }

    drawn1 = OSMGAMesaHookDrawn();
    clears1 = OSMGAMesaHookClears();
    if (soft)
        OSMGAMesaHookForceSoftware(0);

    /* The arm proves which arm it was. */
    printf("   engine batches         : %lu   (want %s)\n",
           drawn1 - drawn0, soft ? "0" : "> 0");
    printf("   engine clears          : %lu   (want %s)\n\n",
           clears1 - clears0, soft ? "0" : "> 0");

    qsort(totMs, (unsigned)frames, sizeof(double), cmpd);
    qsort(glMs, (unsigned)frames, sizeof(double), cmpd);
    qsort(prMs, (unsigned)frames, sizeof(double), cmpd);
    printf("   frame   median %8.2f ms   p95 %8.2f   min %8.2f   -> %.1f fps"
           " at the median\n",
           totMs[frames / 2], totMs[(frames * 95) / 100], totMs[0],
           1000.0 / totMs[frames / 2]);
    printf("   GL      median %8.2f ms   (clear + triangle + finish)\n",
           glMs[frames / 2]);
    printf("   present median %8.2f ms   (engine blit to the screen)\n",
           prMs[frames / 2]);

    OSMesaDestroyContext(ctx);
    free(buf); free(glMs); free(prMs); free(totMs);
    return 0;
}
