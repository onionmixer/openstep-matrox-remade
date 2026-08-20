/*
 * openstep-mga-mesa-gl-test.c -- M1-3c: one surface, two paths.
 *
 * An ordinary OSMesa program.  It never maps anything, never opens a device,
 * and never mentions the card; it asks OSMesa where the picture is and looks
 * there.  What is being checked is that a triangle the hardware drew and a
 * triangle the software rasteriser drew end up in the SAME place -- which is
 * the whole reason for pointing Mesa at video memory, because a frame split
 * between two buffers is worse than a slow one.
 *
 *   cc -O -Wall -o /tmp/gltest openstep-mga-mesa-gl-test.c \
 *      -I<mesa>/include -L<built> -lGL_mga
 */

#include <stdio.h>
#include <string.h>
#include <GL/osmesa.h>

/* Declared here rather than included: an ordinary program would not have
 * these headers, and using them would weaken what this demonstrates. */
extern unsigned long OSMGAMesaHookDrawn(void);
extern unsigned long OSMGAMesaHookDeclined(void);
extern unsigned long OSMGAMesaBufferOrigin(void);
extern unsigned long OSMGAMesaBufferStride(void);

#define W 64
#define H 64

static int failures;

static void
expect(const char *what, int ok)
{
    printf("   %-46s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

int
main(void)
{
    OSMesaContext ctx;
    static unsigned char appbuf[W * H * 4];
    unsigned long *px;
    unsigned long stride, drewHW, drewSW;
    GLint bw = 0, bh = 0, bf = 0;
    void *surface = 0;

    /*
     * ARGB, not RGBA.  The engine lays a pixel out as 0x00RRGGBB and cannot
     * be told otherwise, so this is the layout that lets both paths share a
     * surface; with any other the back end declines and this renders in
     * software, which is correct but is not what this test is for.
     */
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx) { printf("OSMesaCreateContext failed\n"); return 1; }
    memset(appbuf, 0, sizeof appbuf);
    if (!OSMesaMakeCurrent(ctx, appbuf, GL_UNSIGNED_BYTE, W, H)) {
        printf("OSMesaMakeCurrent failed\n");
        return 1;
    }

    printf("M1-3c: one surface, two paths\n");
    if (OSMGAMesaBufferOrigin() == 0UL) {
        printf("   no hardware surface -- software rendering is the correct "
               "answer and there is nothing here to check\n");
        return 0;
    }
    stride = OSMGAMesaBufferStride();
    printf("   surface at video-memory offset %lu, stride %lu px\n",
           OSMGAMesaBufferOrigin(), stride);

    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glShadeModel(GL_SMOOTH);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* One the hardware takes. */
    drewHW = OSMGAMesaHookDrawn();
    glBegin(GL_TRIANGLES);
      glColor3ub(255, 0, 0); glVertex2f( 0.0f,  0.0f);
      glColor3ub(0, 255, 0); glVertex2f( 0.0f, 20.0f);
      glColor3ub(0, 0, 255); glVertex2f(40.0f, 20.0f);
    glEnd();
    glFinish();
    expect("the plain triangle went to the hardware",
           OSMGAMesaHookDrawn() > drewHW);

    /*
     * And one it refuses.  The scissor test is a state the chooser declines,
     * so this goes through the software rasteriser -- into the same surface
     * if the substitution worked, and into a different one if it did not.
     */
    drewSW = OSMGAMesaHookDrawn();
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, W, H);
    glBegin(GL_TRIANGLES);
      glColor3ub(0, 255, 255); glVertex2f(30.0f, 30.0f);
      glColor3ub(0, 255, 255); glVertex2f(30.0f, 50.0f);
      glColor3ub(0, 255, 255); glVertex2f(50.0f, 50.0f);
    glEnd();
    glFinish();
    glDisable(GL_SCISSOR_TEST);
    expect("the scissored triangle was left to software",
           OSMGAMesaHookDrawn() == drewSW);

    if (!OSMesaGetColorBuffer(ctx, &bw, &bh, &bf, &surface) || !surface) {
        printf("   FAIL -- OSMesa has no colour buffer\n");
        return 1;
    }
    px = (unsigned long *)surface;

    /* A pixel from each, read out of the one surface OSMesa points at. */
    {
        unsigned long hw = px[10UL * stride + 10UL] & 0xffffffUL;
        unsigned long sw = px[40UL * stride + 35UL] & 0xffffffUL;

        /*
         * Exact values, not merely "something was drawn".  The first version
         * of this asked only whether the pixels were non-zero, and it passed
         * while the two paths were writing OPPOSITE byte orders into the one
         * surface -- the software half in RGBA and the hardware half in
         * ARGB.  A test that cannot see that is not testing sharing.
         *
         * The hardware pixel is the colour plane at (10,10): red falls 12.75
         * a row, green falls 6.375 a column and rises 12.75 a row, blue rises
         * 6.375 a column, giving 127, 63, 63.  The software pixel is the cyan
         * the second triangle was given.
         */
        printf("   hardware pixel %06lx (want 7f3f3f)\n", hw);
        printf("   software pixel %06lx (want 00ffff)\n", sw);
        expect("the accelerated triangle has the colour it should",
               hw == 0x7f3f3fUL);
        expect("the software triangle has that colour in the same order",
               sw == 0x00ffffUL);
    }

    printf("   hook drew %lu, declined %lu\n",
           OSMGAMesaHookDrawn(), OSMGAMesaHookDeclined());
    printf("%s\n", failures ? "FAIL"
                            : "PASS -- both paths drew into one surface");
    OSMesaDestroyContext(ctx);
    return failures ? 1 : 0;
}
