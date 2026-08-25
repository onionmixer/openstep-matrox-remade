/*
 * The Utah teapot, drawn by this driver.
 *
 * WHY THIS PROGRAM EXISTS.  Everything else in this directory asks one narrow
 * question and answers it with a number.  This one asks whether the whole
 * stack draws a picture somebody would recognise: evaluators producing the
 * geometry, Mesa's lighting colouring the vertices, our back end taking the
 * triangles, the engine rasterising them with depth, and the surface coming
 * back as an image.
 *
 * IT IS NOT THE GITHUB TEAPOT.  That one wants GLEW, freeglut and framebuffer
 * objects; this Mesa is from 2001 and has none of the three -- no FBO at all,
 * and the accelerated path here is OSMesa, which renders offscreen and has no
 * window system binding.  What is the same is the teapot: the control points
 * and the evaluator loop come from this Mesa tree's own tea.c, which is where
 * glutSolidTeapot's implementation lives.
 *
 * The geometry is INCLUDED FROM A GENERATED FILE rather than copied into this
 * repository.  tea.c is GPL as a whole -- the teapot inside it is Mark
 * Kilgard's, under GLUT's own terms -- and rather than decide which licence
 * a copied fragment would carry, the build cuts the fragment out of the Mesa
 * tree at build time and nothing of it is committed here.
 *
 * The image is written as a TIFF because that is what this system's Workspace
 * opens without help.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
/*
 * BUILDS BOTH WAYS.
 *
 * Linked against this project's libGL_mga.a the program reports what the
 * card did and can force the software path for comparison; linked against
 * the stock Mesa libGL.a -- which has no hook, because the hook itself is
 * compiled only under OPENSTEP_MESA_ACCEL_HOOK -- it draws exactly the same
 * teapot with Mesa's own rasteriser and simply has no counters to show.
 *
 * The switch is a shim rather than an #ifdef at each of eighteen call
 * sites: the body below stays the one program, and only these definitions
 * differ.  Define OSMGA_TEAPOT_PLAIN to build the stock form.
 */
#ifndef OSMGA_TEAPOT_PLAIN
#include "OpenStepMGAMesaHook.h"
#include "OpenStepMGAMesaBuffer.h"
#define OSMGA_TEAPOT_ACCELERATED 1
#else
#define OSMGA_TEAPOT_ACCELERATED 0
/* Every one of these is a counter or a switch.  None of them draws, so the
 * picture does not depend on any of them -- which is why the stock build can
 * answer nought and still be the same program. */
#define OSMGAMesaHookBatchLimit(n)     ((void)(n))
#define OSMGAMesaHookInjectRefusal(n)  ((void)(n))
#define OSMGAMesaHookForceSoftware(n)  ((void)(n))
#define OSMGAMesaBufferOrigin()        0UL
#define OSMGAMesaHookBatches()         0UL
#define OSMGAMesaHookMirrors()         0UL
#define OSMGAMesaHookDrawn()           0UL
#define OSMGAMesaHookSoftware()        0UL
#define OSMGAMesaHookUnsupported()     0UL
#define OSMGAMesaHookReplayed()        0UL
#endif

/* patchdata, cpdata, tex and teapot(), cut from the Mesa tree at build time */
#include "teapot-geometry.h"

#define W 640
#define H 480

static void
lights(void)
{
    GLfloat ambient[4];
    GLfloat diffuse[4];
    GLfloat position[4];
    GLfloat lmodel_ambient[4];

    ambient[0] = 0.0f; ambient[1] = 0.0f; ambient[2] = 0.0f; ambient[3] = 1.0f;
    diffuse[0] = 1.0f; diffuse[1] = 1.0f; diffuse[2] = 1.0f; diffuse[3] = 1.0f;
    position[0] = 0.0f; position[1] = 3.0f; position[2] = 3.0f;
    position[3] = 0.0f;
    lmodel_ambient[0] = 0.2f; lmodel_ambient[1] = 0.2f;
    lmodel_ambient[2] = 0.2f; lmodel_ambient[3] = 1.0f;

    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_POSITION, position);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, lmodel_ambient);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_AUTO_NORMAL);
    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);
}

static void
material(float r, float g, float b, float shine)
{
    GLfloat amb[4], dif[4], spec[4];

    amb[0] = r * 0.2f; amb[1] = g * 0.2f; amb[2] = b * 0.2f; amb[3] = 1.0f;
    dif[0] = r; dif[1] = g; dif[2] = b; dif[3] = 1.0f;
    spec[0] = 0.9f; spec[1] = 0.9f; spec[2] = 0.9f; spec[3] = 1.0f;
    glMaterialfv(GL_FRONT, GL_AMBIENT, amb);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, dif);
    glMaterialfv(GL_FRONT, GL_SPECULAR, spec);
    glMaterialf(GL_FRONT, GL_SHININESS, shine);
}

/* A baseline uncompressed RGB TIFF, laid out header, pixels, then the tags. */
static void
writeTiff(const char *path, const unsigned long *argb, int w, int h)
{
    FILE *f = fopen(path, "w");
    unsigned char *row;
    long pixels = (long)w * (long)h * 3L;
    long bpsOff = 8L + pixels;
    long ifdOff = bpsOff + 6L;
    int x, y, i;
    static const unsigned short tags[10] = {
        256, 257, 258, 259, 262, 273, 277, 278, 279, 284
    };
    unsigned long vals[10];
    unsigned short types[10];
    unsigned long counts[10];

    if (!f) { printf("   cannot write %s\n", path); return; }
    row = (unsigned char *)malloc((unsigned)(w * 3));
    if (!row) { fclose(f); return; }

#define PUT16(v) do { int v_ = (int)(v); putc(v_ & 0xff, f); \
                      putc((v_ >> 8) & 0xff, f); } while (0)
#define PUT32(v) do { unsigned long V_ = (unsigned long)(v); \
                      putc((int)(V_ & 0xff), f); \
                      putc((int)((V_ >> 8) & 0xff), f); \
                      putc((int)((V_ >> 16) & 0xff), f); \
                      putc((int)((V_ >> 24) & 0xff), f); } while (0)

    putc('I', f); putc('I', f); PUT16(42); PUT32(ifdOff);

    /*
     * OSMesa's row nought is the BOTTOM of the picture and TIFF's is the top,
     * so the rows go out in reverse.  Getting this wrong gives an upside-down
     * teapot, which is the kind of thing that looks like a driver fault and
     * is not one.
     */
    for (y = h - 1; y >= 0; y--) {
        for (x = 0; x < w; x++) {
            unsigned long p = argb[(long)y * w + x];

            row[x * 3 + 0] = (unsigned char)((p >> 16) & 0xff);
            row[x * 3 + 1] = (unsigned char)((p >> 8) & 0xff);
            row[x * 3 + 2] = (unsigned char)(p & 0xff);
        }
        fwrite(row, 1, (unsigned)(w * 3), f);
    }
    PUT16(8); PUT16(8); PUT16(8);           /* BitsPerSample array */

    vals[0] = (unsigned long)w;    types[0] = 3; counts[0] = 1;  /* width */
    vals[1] = (unsigned long)h;    types[1] = 3; counts[1] = 1;  /* length */
    vals[2] = (unsigned long)bpsOff; types[2] = 3; counts[2] = 3;/* bits */
    vals[3] = 1UL;                 types[3] = 3; counts[3] = 1;  /* none */
    vals[4] = 2UL;                 types[4] = 3; counts[4] = 1;  /* RGB */
    vals[5] = 8UL;                 types[5] = 4; counts[5] = 1;  /* strip */
    vals[6] = 3UL;                 types[6] = 3; counts[6] = 1;  /* samples */
    vals[7] = (unsigned long)h;    types[7] = 3; counts[7] = 1;  /* rows */
    vals[8] = (unsigned long)pixels; types[8] = 4; counts[8] = 1;/* bytes */
    vals[9] = 1UL;                 types[9] = 3; counts[9] = 1;  /* chunky */

    PUT16(10);
    for (i = 0; i < 10; i++) {
        PUT16(tags[i]);
        PUT16(types[i]);
        PUT32(counts[i]);
        if (types[i] == 3 && counts[i] == 1) { PUT16(vals[i]); PUT16(0); }
        else                                  { PUT32(vals[i]); }
    }
    PUT32(0);
    free(row);
    fclose(f);
    printf("   wrote %s (%dx%d, %ld bytes of pixels)\n", path, w, h, pixels);
}

static double
now(void)
{
    struct timeval t;
    gettimeofday(&t, (struct timezone *)0);
    return (double)t.tv_sec + (double)t.tv_usec / 1e6;
}

int
main(int argc, char **argv)
{
    double t0, t1, t2, t3, t4, t5;
    OSMesaContext ctx;
    unsigned long *buf;
    unsigned long drawn0, drawn1, soft0, soft1, unsup0, unsup1;
    unsigned long batches0;
    unsigned long mir0, mir1;
    const char *out = (argc > 1) ? argv[1] : "/tmp/teapot.tiff";
    int forceSoft = (argc > 2 && strcmp(argv[2], "soft") == 0);
    int grid = (argc > 3) ? atoi(argv[3]) : 12;
    /* argv[4]: the batch limit.  1 reproduces the pre-batching behaviour
     * exactly, so two runs (limit 1 and no argument) must write identical
     * files -- that is the batching identity gate, compared on the host. */
    if (argc > 4)
        OSMGAMesaHookBatchLimit((unsigned long)atoi(argv[4]));
    /* argv[5] "inject": refuse every flushed batch in the kernel so the
     * software replay draws the whole scene -- the resulting file must be
     * byte-identical to the forced-software file, and the replayed count
     * must equal the source count. */
    if (argc > 5 && strcmp(argv[5], "inject") == 0)
        OSMGAMesaHookInjectRefusal(1);

    t0 = now();
    buf = (unsigned long *)malloc((unsigned)(W * H) * sizeof *buf);
    if (!buf) { printf("no room\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
        printf("no context\n"); return 2;
    }

    t1 = now();
    printf("the Utah teapot, through this driver\n\n");
#if OSMGA_TEAPOT_ACCELERATED
    printf("   surface is the engine's : %s\n",
           (OSMGAMesaBufferOrigin() != 0UL) ? "yes" : "NO -- software only");
#else
    printf("   built against           : the stock Mesa library, so this is"
           " Mesa's own rasteriser\n");
#endif

    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -0.75, 0.75, 2.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, -0.3f, -6.0f);
    glRotatef(-20.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(25.0f, 0.0f, 1.0f, 0.0f);

    lights();
    glClearColor(0.08f, 0.10f, 0.16f, 1.0f);
    glClearDepth(1.0);
    t2 = now();
    glClear((GLbitfield)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    glFinish();
    t3 = now();

    if (forceSoft)
        OSMGAMesaHookForceSoftware(1);
    batches0 = OSMGAMesaHookBatches();
    mir0 = OSMGAMesaHookMirrors();
    drawn0 = OSMGAMesaHookDrawn();
    soft0  = OSMGAMesaHookSoftware();
    unsup0 = OSMGAMesaHookUnsupported();

    /*
     * A half turn about x before each pot.
     *
     * The teapot() lifted from tea.c carries its own two rotations, chosen
     * for that demo's coordinate system rather than for anybody else's, and
     * under this camera they leave the pot standing on its lid.  The
     * highlight was in the right place -- top of the body, from a light above
     * -- which is what says the picture is the right way up and the MODEL was
     * not; had the image been flipped the light would have come from below.
     */
    glPushMatrix();
    glTranslatef(-1.1f, 0.0f, 0.0f);
    glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
    material(0.9f, 0.35f, 0.15f, 40.0f);
    teapot(grid, 1.0, GL_FILL);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(1.1f, 0.0f, 0.0f);
    glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
    material(0.25f, 0.6f, 0.9f, 90.0f);
    teapot(grid, 1.0, GL_FILL);
    glPopMatrix();

    glFinish();
    t4 = now();
    if (forceSoft)
        OSMGAMesaHookForceSoftware(0);

    mir1 = OSMGAMesaHookMirrors();
    drawn1 = OSMGAMesaHookDrawn();
    soft1  = OSMGAMesaHookSoftware();
    unsup1 = OSMGAMesaHookUnsupported();

#if !OSMGA_TEAPOT_ACCELERATED
    /* Saying "0 triangles drawn by the card" would be a lie of the kind a
     * report is supposed to prevent: there is no card in this build, so
     * there is nothing to count.  Say that instead. */
    printf("   counters                : none -- the stock library has no"
           " hook to count with\n");
    (void)mir0; (void)mir1; (void)drawn0; (void)drawn1;
    (void)soft0; (void)soft1; (void)unsup0; (void)unsup1; (void)batches0;
#else
    printf("   surface walked back     : %lu times\n", mir1 - mir0);
    printf("   source triangles drawn  : %lu\n", drawn1 - drawn0);
    printf("   submissions             : %lu   (batching: %lu sources per"
           " submission)\n", OSMGAMesaHookBatches() - batches0,
           (OSMGAMesaHookBatches() - batches0) ? (drawn1 - drawn0) /
               (OSMGAMesaHookBatches() - batches0) : 0UL);
    printf("   triangles left to Mesa  : %lu\n", soft1 - soft0);
    printf("   refused as unsupported  : %lu\n", unsup1 - unsup0);
    printf("   replayed after refusal  : %lu\n", OSMGAMesaHookReplayed());
    if ((drawn1 - drawn0) + (soft1 - soft0) > 0UL)
        printf("   share drawn by the card : %lu%%\n",
               (drawn1 - drawn0) * 100UL /
               ((drawn1 - drawn0) + (soft1 - soft0)));
#endif
    printf("\n");
    writeTiff(out, buf, W, H);
    t5 = now();
    printf("\n   where the time went, in seconds\n");
    printf("      context + first bind : %8.3f\n", t1 - t0);
    printf("      state + projection   : %8.3f\n", t2 - t1);
    printf("      clear + finish       : %8.3f\n", t3 - t2);
    printf("      the two teapots      : %8.3f\n", t4 - t3);
    printf("      writing the file     : %8.3f\n", t5 - t4);
    printf("      total                : %8.3f\n", t5 - t0);

    OSMesaDestroyContext(ctx);
    free(buf);
    return 0;
}
