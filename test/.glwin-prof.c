/*
 * Throwaway: where glwin's geometry milliseconds go.
 *
 * Same scene as the window, drawn offscreen with present mode on so the
 * whole-surface mirror stands down exactly as it does on screen.  Nothing
 * is presented -- the present blit is measured separately and is not what
 * this asks about.
 *
 * The system's profiling runtime lives in libsys (monstartup/monoutput/
 * profil/mcount); only gcrt0.o is missing, and calling the two entry points
 * by hand is all gcrt0 ever did.  Verified end to end against /usr/ucb/gprof
 * before this file was written.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <mach-o/loader.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"
#include "../mesa/OpenStepMGAMesaBuffer.h"
#include "teapot-geometry.h"

extern const struct section *getsectbyname(const char *seg, const char *sect);
extern void monstartup(char *lowpc, char *highpc);
extern void monoutput(const char *filename);

#define W 640
#define H 480

/* knobs, all default to the window's own scene */
static int optLight = 1, optAutoNormal = 1, optNormalize = 1;
static int optTexMap = 1, optEval = 1;
static double optScale = 1.0;
static int optScissor = 0, optDepth = 1, optGrid = 4;

static double nowMs(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static double userMs(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return (double)ru.ru_utime.tv_sec * 1000.0 + (double)ru.ru_utime.tv_usec / 1000.0;
}

static double sysMs(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return (double)ru.ru_stime.tv_sec * 1000.0 + (double)ru.ru_stime.tv_usec / 1000.0;
}

/*
 * The teapot, with the knobs the ablation needs.  Same data and same order
 * as the copy in teapot-geometry.h; only the enables and the two optional
 * steps differ, so a knob at its default draws the identical picture.
 */
static void teapotFlex(int grid, double scale)
{
    float p[4][4][3], q[4][4][3], r[4][4][3], s[4][4][3];
    long i, j, k, l;

    glPushAttrib(GL_ENABLE_BIT | GL_EVAL_BIT);
    if (optAutoNormal) glEnable(GL_AUTO_NORMAL);
    if (optNormalize)  glEnable(GL_NORMALIZE); else glDisable(GL_NORMALIZE);
    glEnable(GL_MAP2_VERTEX_3);
    if (optTexMap) glEnable(GL_MAP2_TEXTURE_COORD_2);
    glPushMatrix();
    glRotatef(90.0f, 0.0f, 1.0f, 0.0f);
    glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
    glScalef((float)(0.5 * scale), (float)(0.5 * scale), (float)(0.5 * scale));
    glTranslatef(0.0f, 0.0f, -1.5f);
    for (i = 0; i < 10; i++) {
        for (j = 0; j < 4; j++)
            for (k = 0; k < 4; k++)
                for (l = 0; l < 3; l++) {
                    p[j][k][l] = cpdata[patchdata[i][j * 4 + k]][l];
                    q[j][k][l] = cpdata[patchdata[i][j * 4 + (3 - k)]][l];
                    if (l == 1) q[j][k][l] *= -1.0f;
                    if (i < 6) {
                        r[j][k][l] = cpdata[patchdata[i][j * 4 + (3 - k)]][l];
                        if (l == 0) r[j][k][l] *= -1.0f;
                        s[j][k][l] = cpdata[patchdata[i][j * 4 + k]][l];
                        if (l == 0) s[j][k][l] *= -1.0f;
                        if (l == 1) s[j][k][l] *= -1.0f;
                    }
                }
        if (optTexMap)
            glMap2f(GL_MAP2_TEXTURE_COORD_2, 0, 1, 2, 2, 0, 1, 4, 2, &tex[0][0][0]);
        glMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 4, 0, 1, 12, 4, &p[0][0][0]);
        glMapGrid2f(grid, 0.0, 1.0, grid, 0.0, 1.0);
        if (optEval) glEvalMesh2(GL_FILL, 0, grid, 0, grid);
        glMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 4, 0, 1, 12, 4, &q[0][0][0]);
        if (optEval) glEvalMesh2(GL_FILL, 0, grid, 0, grid);
        if (i < 6) {
            glMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 4, 0, 1, 12, 4, &r[0][0][0]);
            if (optEval) glEvalMesh2(GL_FILL, 0, grid, 0, grid);
            glMap2f(GL_MAP2_VERTEX_3, 0, 1, 3, 4, 0, 1, 12, 4, &s[0][0][0]);
            if (optEval) glEvalMesh2(GL_FILL, 0, grid, 0, grid);
        }
    }
    glPopMatrix();
    glPopAttrib();
}

static void drawFrame(int n)
{
    glClear((GLbitfield)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    glPushMatrix();
    glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
    glRotatef((float)n * 1.5f, 0.0f, 1.0f, 0.0f);
    teapotFlex(optGrid, optScale);
    glPopMatrix();
    glFinish();
}

static void counters(const char *tag)
{
    unsigned long fc[4], sb[6], dl[4], db[7], dr[25];

    OSMGAMesaHookFlushCounts(fc);
    OSMGAMesaHookSubmitStats(sb);
    OSMGAMesaHookDeltaStats(dl);
    OSMGAMesaHookDeltaBlocks(db);
    OSMGAMesaHookDeltaRegs(dr);
    printf("%s: drawn %lu batches %lu traps %lu | software %lu declined %lu "
           "unsupported %lu | mirrors %lu fills %lu clears %lu | replayed %lu\n",
           tag, OSMGAMesaHookDrawn(), OSMGAMesaHookBatches(),
           OSMGAMesaHookTraps(), OSMGAMesaHookSoftware(),
           OSMGAMesaHookDeclined(), OSMGAMesaHookUnsupported(),
           OSMGAMesaHookMirrors(), OSMGAMesaHookUniformFills(),
           OSMGAMesaHookClears(), OSMGAMesaHookReplayed());
    {
        static const char *vn[24] = {
            "OK","MAGIC","VERSION","COUNT","DSTORG","ZORG","TEXORG","DWGCTL",
            "TRIROW","TRICOL","TRISLOPE","ALPHA","TEXSIZE","TEXCOORD",
            "DSTSIZE","EDGEDIV","DSTPITCH","TRICROSS","TRISGN","TRIEMPTY",
            "20","21","22","23"
        };
        const OSMGAMesaRefusal *r = OSMGAMesaHookLastRefusal();
        unsigned long v;
        int any = 0;

        for (v = 0UL; v < OSMGA_MESA_VERDICTS; v++)
            if (OSMGAMesaHookVerdictCount(v) != 0UL) {
                printf("%s: verdict %s x %lu\n", tag, vn[v],
                       OSMGAMesaHookVerdictCount(v));
                any = 1;
            }
        if (any && r != 0)
            printf("%s: last refusal status %lu verdict %lu, trapezoid %lu of "
                   "%lu, dst %lux%lu; y %ld h %ld ar0 %ld ar6 %ld sgn %ld "
                   "fxbndry %08lx dwgctl %08lx\n",
                   tag, r->status, r->verdict, r->triangle, r->triCount,
                   r->dstWidth, r->dstHeight,
                   r->tri.y, r->tri.h, r->tri.ar0, r->tri.ar6, r->tri.sgn,
                   r->tri.fxbndry, r->tri.dwgctl);
    }
    {
        unsigned long mk[64], mc[64], spill, tot = 0UL;
        int k;

        OSMGAMesaHookDeltaMasks(mk, mc, &spill);
        for (k = 0; k < 64; k++) tot += mc[k];
        if (tot != 0UL) {
            printf("%s: masks -- %lu recorded, %lu spilled\n", tag, tot, spill);
            for (k = 0; k < 64; k++)
                if (mc[k] != 0UL)
                    printf("%s: mask %07lx x %lu\n", tag, mk[k], mc[k]);
        }
    }
    printf("%s: flushes -- bracket %lu, key %lu, full %lu, other %lu\n",
           tag, fc[0], fc[1], fc[2], fc[3]);
    printf("%s: submits %lu, %lu us total (%.1f us each), %lu dwords "
           "(%.1f/submit), poll index sum %lu max %lu, %lu submits polled "
           "more than once\n",
           tag, sb[0], sb[1],
           sb[0] ? (double)sb[1] / (double)sb[0] : 0.0,
           sb[2], sb[0] ? (double)sb[2] / (double)sb[0] : 0.0,
           sb[3], sb[4], sb[5]);
    printf("%s: %lu trapezoids, %.1f of 25 register values differ from the "
           "one before; blocks %lu -> %lu (%.1f%% of the list)\n",
           tag, dl[0],
           dl[0] ? (double)dl[1] / (double)dl[0] : 0.0,
           dl[2], dl[3],
           dl[2] ? 100.0 * (double)dl[3] / (double)dl[2] : 0.0);
    {
        static const char *bn[7] = {
            "dwgctl+adx+Rdy+ady", "Rdx+Gdx+Bdy+Gdy", "ar6+ar5+ar4+ar1",
            "ar0+Zdx+ar2+Zdy",    "Gs+Bs+Rs+Bdx",    "alpha start+ctrl",
            "sgn+Zstart+fxbndry+exec"
        };
        int k;

        static const char *rn[25] = {
            "dwgctl","ar0","ar1","ar2","ar4","ar5","ar6","sgn",
            "DR4 Rstart","DR6 Rdx","DR7 Rdy","DR8 Gstart",
            "DR10 Gdx","DR11 Gdy","DR12 Bstart","DR14 Bdx",
            "DR15 Bdy","DR0 Zstart","DR2 Zdx","DR3 Zdy",
            "ALPHASTART","ALPHAXINC","ALPHAYINC","ALPHACTRL","fxbndry"
        };
        for (k = 0; k < 7; k++)
            printf("%s:   block %-16s changes %6.1f%% of trapezoids\n",
                   tag, bn[k],
                   dl[0] ? 100.0 * (double)db[k] / (double)dl[0] : 0.0);
        for (k = 0; k < 25; k++)
            printf("%s:   reg   %-16s changes %6.1f%%\n",
                   tag, rn[k],
                   dl[0] ? 100.0 * (double)dr[k] / (double)dl[0] : 0.0);
    }
    fflush(stdout);
}

int main(int argc, char **argv)
{
    OSMesaContext ctx;
    unsigned long *buf;
    const char *mode = (argc > 1) ? argv[1] : "frame";
    int reps = (argc > 2) ? atoi(argv[2]) : 120;
    GLfloat amb[4], dif[4], pos[4], lamb[4], mamb[4], mdif[4], mspec[4];
    double t0, t1, u0, u1, s0, s1;
    int n;

    if (strcmp(mode, "mapsonly") == 0)      optEval = 0;
    else if (strcmp(mode, "nolight") == 0)  optLight = 0;
    else if (strcmp(mode, "noautonorm") == 0) optAutoNormal = 0;
    else if (strcmp(mode, "nonormalize") == 0) optNormalize = 0;
    else if (strcmp(mode, "notexmap") == 0) optTexMap = 0;
    else if (strncmp(mode, "grid", 4) == 0) optGrid = atoi(mode + 4);
    else if (strcmp(mode, "scissor") == 0)  optScissor = 1;
    else if (strcmp(mode, "nodepth") == 0)  optDepth = 0;
    else if (strcmp(mode, "small") == 0)    optScale = 0.15;
    else if (strcmp(mode, "tiny") == 0)     optScale = 0.05;

    buf = (unsigned long *)malloc((unsigned)(W * H) * 4);
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
        printf("no ctx\n"); return 2;
    }
    OSMGAMesaBufferPresentMode(1);      /* exactly what the window declares */

    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glFrustum(-1.0, 1.0, 0.75, -0.75, 2.0, 20.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glTranslatef(0.0f, -0.2f, -6.0f);
    glRotatef(-20.0f, 1.0f, 0.0f, 0.0f);
    glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);
    if (optDepth) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); }
    else glDisable(GL_DEPTH_TEST);
    glClearDepth(1.0);
    /*
     * Same geometry, almost no pixels: every triangle is still built and
     * still submitted -- the counters must show it -- but the engine writes
     * only what falls in an 8x8 box.  Scaling the model down instead was not
     * this experiment: sub-pixel triangles get rejected and the trapezoid
     * count falls with the pixels, which confounds the two.
     */
    if (optScissor) { glScissor(0, 0, 8, 8); glEnable(GL_SCISSOR_TEST); }
    amb[0]=0;amb[1]=0;amb[2]=0;amb[3]=1;
    dif[0]=1;dif[1]=1;dif[2]=1;dif[3]=1;
    pos[0]=0;pos[1]=3;pos[2]=3;pos[3]=0;
    lamb[0]=0.2f;lamb[1]=0.2f;lamb[2]=0.2f;lamb[3]=1;
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, lamb);
    if (optLight) { glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); }
    mamb[0]=0.18f;mamb[1]=0.07f;mamb[2]=0.03f;mamb[3]=1;
    mdif[0]=0.9f;mdif[1]=0.35f;mdif[2]=0.15f;mdif[3]=1;
    mspec[0]=0.9f;mspec[1]=0.9f;mspec[2]=0.9f;mspec[3]=1;
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mamb);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mdif);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mspec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 50.0f);
    glClearColor(0.06f, 0.08f, 0.14f, 1.0f);

    /*
     * The submission instrumentation is off in the driver by default -- it
     * costs 0.76 ms a frame, which is more than several of the things it is
     * used to measure.  Turn it on only for the modes that report it, and
     * say so, because a frame time measured with it set is not the frame
     * time without it.
     */
    if (strcmp(mode, "submit") == 0 || strcmp(mode, "delta") == 0
        || (argc > 3 && strcmp(argv[3], "inst") == 0)) {
        OSMGAMesaHookInstrument(1);
        printf("instrumented: submit timing and delta counting are ON, "
               "which costs about 0.76 ms a frame\n");
    }
    if (strcmp(mode, "limit1") == 0) OSMGAMesaHookBatchLimit(1UL);
    if (strcmp(mode, "soft") == 0)   OSMGAMesaHookForceSoftware(1);

    drawFrame(0);                       /* warm: first frame pages in */
    counters("warm");

    if (strcmp(mode, "profile") == 0) {
        const struct section *sect = getsectbyname("__TEXT", "__text");

        if (!sect) { printf("no text section\n"); return 2; }
        printf("text %08lx + %08lx, %d frames\n", (unsigned long)sect->addr,
               (unsigned long)sect->size, reps);
        fflush(stdout);
        u0 = userMs(); s0 = sysMs(); t0 = nowMs();
        monstartup((char *)sect->addr, (char *)(sect->addr + sect->size));
        for (n = 1; n <= reps; n++) drawFrame(n);
        monoutput("/tmp/gmon.out");
        t1 = nowMs(); u1 = userMs(); s1 = sysMs();
        printf("profiled %d frames: wall %.2f ms/frame = user %.2f + sys %.2f "
               "+ idle %.2f\n", reps, (t1 - t0) / (double)reps,
               (u1 - u0) / (double)reps, (s1 - s0) / (double)reps,
               (t1 - t0 - (u1 - u0) - (s1 - s0)) / (double)reps);
        counters("after");
        return 0;
    }

    u0 = userMs(); s0 = sysMs(); t0 = nowMs();
    for (n = 1; n <= reps; n++) drawFrame(n);
    t1 = nowMs(); u1 = userMs(); s1 = sysMs();
    printf("%-12s wall %7.2f = user %6.2f + sys %6.2f + idle %6.2f  ms/frame "
           "(%d frames)\n", mode, (t1 - t0) / (double)reps,
           (u1 - u0) / (double)reps, (s1 - s0) / (double)reps,
           (t1 - t0 - (u1 - u0) - (s1 - s0)) / (double)reps, reps);
    counters("after");
    return 0;
}
