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
extern unsigned long OSMGAMesaBufferDepthOrigin(void);

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

    /*
     * And the buffer the application actually handed to MakeCurrent.  It is
     * not where the picture is drawn any more -- video memory is -- so
     * unless it is put back, a program that reads its own array sees nothing
     * at all, which is the whole difficulty with substituting the buffer.
     *
     * Read at the application's own row width, not the surface's stride: the
     * array is exactly width by height.
     */
    {
        unsigned long *ap = (unsigned long *)appbuf;
        unsigned long hw = ap[10UL * W + 10UL] & 0xffffffUL;
        unsigned long sw = ap[40UL * W + 35UL] & 0xffffffUL;

        printf("   application's own buffer: %06lx and %06lx\n", hw, sw);
        expect("the accelerated triangle reached the caller's buffer",
               hw == 0x7f3f3fUL);
        expect("so did the software one", sw == 0x00ffffUL);
    }

    /*
     * Depth.  The buffer Mesa tests against is in video memory too now, and
     * the first thing to establish is that the software rasteriser still
     * gets the right answer out of it -- this back end refuses depth-tested
     * states for the moment, so both triangles below go through software and
     * what is being checked is the substitution, not the acceleration.
     *
     * Two triangles over the same pixels at different depths.  Which one
     * wins is worked out rather than guessed: this projection is
     * glOrtho(0,W,0,H,-1,1), whose z maps to normalised -z, so a vertex at
     * z = -0.5 ends up at window depth 0.75 and one at z = +0.5 at 0.25.
     * GL_LESS keeps the smaller, so the SECOND triangle wins -- which is the
     * opposite of what the naming would suggest, and is why it is computed.
     */
    printf("   depth buffer at video-memory offset %lu\n",
           OSMGAMesaBufferDepthOrigin());
    if (OSMGAMesaBufferDepthOrigin() == 0UL) {
        printf("   FAIL -- no shared depth buffer\n");
        failures++;
    } else {
        unsigned long drewZ;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        drewZ = OSMGAMesaHookDrawn();

        /*
         * The nearer one FIRST, the further one second.  Drawn the other way
         * round -- as this was at first -- the second triangle wins whether
         * or not any depth testing happens, so the test could not tell a
         * working depth buffer from none at all.  This order can: without
         * the comparison the second would simply overwrite the first.
         */
        glBegin(GL_TRIANGLES);            /* window depth 0.25 -- must win */
          glColor3ub(0, 0, 255);
          glVertex3f( 0.0f,  0.0f,  0.5f);
          glVertex3f(40.0f,  0.0f,  0.5f);
          glVertex3f( 0.0f, 40.0f,  0.5f);
        glEnd();
        glBegin(GL_TRIANGLES);            /* window depth 0.75 -- must lose */
          glColor3ub(255, 0, 0);
          glVertex3f( 0.0f,  0.0f, -0.5f);
          glVertex3f(40.0f,  0.0f, -0.5f);
          glVertex3f( 0.0f, 40.0f, -0.5f);
        glEnd();
        glFinish();
        glDisable(GL_DEPTH_TEST);

        {
            unsigned long p2 = ((unsigned long *)appbuf)[5UL * W + 5UL]
                               & 0xffffffUL;

            printf("   depth-tested pixel %06lx (want 0000ff -- red would "
                   "mean no comparison happened)\n", p2);
            expect("the deeper triangle, drawn second, was rejected",
                   p2 == 0x0000ffUL);
            /*
             * And that the engine did it.  Without this the test passes
             * just as well when depth quietly falls back to software --
             * the answer is the same either way, which is exactly why the
             * answer alone cannot tell us the hardware was used.
             */
            expect("both depth triangles went to the hardware",
                   OSMGAMesaHookDrawn() == drewZ + 2UL);
        }
    }

    /*
     * The claim the whole design rests on: within ONE frame, a triangle the
     * engine drew and a triangle the software rasteriser drew must see each
     * other's depths.  Nothing so far tested that -- each half was checked
     * against itself.
     *
     * Both directions, because seeing is not symmetric by construction: the
     * engine must respect what software wrote, and software must respect
     * what the engine wrote, and either could fail alone.
     */
    if (OSMGAMesaBufferDepthOrigin() != 0UL) {
        unsigned long px1, px2;

        /* hardware lays down the near one; software tries to paint over it */
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glBegin(GL_TRIANGLES);              /* accelerated, depth 0.25 */
          glColor3ub(0, 0, 255);
          glVertex3f( 0.0f,  0.0f,  0.5f);
          glVertex3f(40.0f,  0.0f,  0.5f);
          glVertex3f( 0.0f, 40.0f,  0.5f);
        glEnd();
        glEnable(GL_SCISSOR_TEST);          /* refused -> software */
        glScissor(0, 0, W, H);
        glBegin(GL_TRIANGLES);              /* software, depth 0.75 */
          glColor3ub(255, 0, 0);
          glVertex3f( 0.0f,  0.0f, -0.5f);
          glVertex3f(40.0f,  0.0f, -0.5f);
          glVertex3f( 0.0f, 40.0f, -0.5f);
        glEnd();
        glDisable(GL_SCISSOR_TEST);
        glFinish();
        px1 = ((unsigned long *)appbuf)[5UL * W + 5UL] & 0xffffffUL;

        /* and the other way round */
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, W, H);
        glBegin(GL_TRIANGLES);              /* software, depth 0.25 */
          glColor3ub(0, 255, 0);
          glVertex3f( 0.0f,  0.0f,  0.5f);
          glVertex3f(40.0f,  0.0f,  0.5f);
          glVertex3f( 0.0f, 40.0f,  0.5f);
        glEnd();
        glDisable(GL_SCISSOR_TEST);
        glBegin(GL_TRIANGLES);              /* accelerated, depth 0.75 */
          glColor3ub(255, 0, 0);
          glVertex3f( 0.0f,  0.0f, -0.5f);
          glVertex3f(40.0f,  0.0f, -0.5f);
          glVertex3f( 0.0f, 40.0f, -0.5f);
        glEnd();
        glFinish();
        glDisable(GL_DEPTH_TEST);
        px2 = ((unsigned long *)appbuf)[5UL * W + 5UL] & 0xffffffUL;

        printf("   software over hardware: %06lx (want 0000ff)\n", px1);
        printf("   hardware over software: %06lx (want 00ff00)\n", px2);
        expect("software respected the depth the engine wrote",
               px1 == 0x0000ffUL);
        expect("the engine respected the depth software wrote",
               px2 == 0x00ff00UL);
    }

    /*
     * Blending.  An opaque red ground, then blue at half alpha over it: the
     * engine computes (a*src + (255-a)*dst)/255 with a true divide, so the
     * answer is worked out rather than eyeballed.
     *
     * That it reads the destination at all is the point -- and the
     * destination alpha has to be the fourth byte of the surface, not a
     * buffer beside it, or the engine would blend against alpha it never
     * wrote.
     */
    {
        unsigned long before3 = OSMGAMesaHookDrawn();
        unsigned long px3;

        glDisable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);              /* opaque red ground */
          glColor4ub(255, 0, 0, 255);
          glVertex2f( 0.0f,  0.0f);
          glVertex2f(40.0f,  0.0f);
          glVertex2f( 0.0f, 40.0f);
        glEnd();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBegin(GL_TRIANGLES);              /* blue at half alpha over it */
          glColor4ub(0, 0, 255, 128);
          glVertex2f( 0.0f,  0.0f);
          glVertex2f(40.0f,  0.0f);
          glVertex2f( 0.0f, 40.0f);
        glEnd();
        glDisable(GL_BLEND);
        glFinish();

        px3 = ((unsigned long *)appbuf)[5UL * W + 5UL] & 0xffffffUL;
        printf("   blended pixel %06lx (want 7f0080)\n", px3);
        expect("the blend is what the formula says", px3 == 0x7f0080UL);
        expect("both blended triangles went to the hardware",
               OSMGAMesaHookDrawn() == before3 + 2UL);
    }

    {
        /*
     * The alpha each path leaves behind, from an OPAQUE triangle.  The
     * engine writes whatever its interpolator holds, and the software
     * path writes the vertex's alpha, so if the interpolator is not set
     * for unblended triangles the two disagree about the fourth byte --
     * which nothing blends with here, but glReadPixels would return.
     */
    {
        unsigned long ah = ((unsigned long *)appbuf)[5UL * W + 5UL] >> 24;

        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);          /* accelerated, opaque, alpha 255 */
          glColor4ub(0, 255, 0, 255);
          glVertex2f( 0.0f,  0.0f);
          glVertex2f(40.0f,  0.0f);
          glVertex2f( 0.0f, 40.0f);
        glEnd();
        glFinish();
        ah = ((unsigned long *)appbuf)[5UL * W + 5UL] >> 24;

        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST); glScissor(0, 0, W, H);
        glBegin(GL_TRIANGLES);          /* software, same triangle */
          glColor4ub(0, 255, 0, 255);
          glVertex2f( 0.0f,  0.0f);
          glVertex2f(40.0f,  0.0f);
          glVertex2f( 0.0f, 40.0f);
        glEnd();
        glDisable(GL_SCISSOR_TEST);
        glFinish();
        printf("   opaque alpha: hardware %02lx, software %02lx\n",
               ah, ((unsigned long *)appbuf)[5UL * W + 5UL] >> 24);
        expect("the two paths leave the same alpha behind",
               ah == (((unsigned long *)appbuf)[5UL * W + 5UL] >> 24));
    }

    }

    printf("   hook drew %lu, declined %lu\n",
           OSMGAMesaHookDrawn(), OSMGAMesaHookDeclined());
    printf("%s\n", failures ? "FAIL"
                            : "PASS -- both paths drew into one surface");
    OSMesaDestroyContext(ctx);
    return failures ? 1 : 0;
}
