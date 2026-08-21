/*
 * The VRAM layout: colour, then the space depth would take, then textures.
 *
 * What this has to be able to catch is a texture arena that overlaps the
 * depth buffer.  The obvious test -- "ask for the arena, then allocate depth,
 * and see that the depth origin did not move" -- CANNOT catch it: the depth
 * origin is computed from the colour surface and never looks at the arena, so
 * it does not move whether the arena overlaps it or not.  That check passes
 * on a broken layout.
 *
 * So the test asserts the thing itself: the two half-open ranges are
 * disjoint, at an origin worked out independently of the code under test.
 * And it proves that assertion can fire, by running it once against a
 * poisoned arena that starts exactly where depth starts.  A judge that never
 * fails is not a judge.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <mach/mach.h>
#include <GL/osmesa.h>

extern int  OSMGAMesaBufferTextureArena(const void *ctx, unsigned long *o,
                                        unsigned long *b);
extern unsigned long OSMGAMesaBufferOrigin(void);
extern unsigned long OSMGAMesaBufferDepthOrigin(void);
extern unsigned long OSMGAMesaBufferWidth(void);
extern unsigned long OSMGAMesaBufferHeight(void);
extern unsigned long OSMGAMesaBufferStride(void);
extern int  OSMGAMesaProbeDeviceFd(void);
extern unsigned long OSMGAMesaHookDrawn(void);

extern caddr_t mmap(caddr_t, int, int, int, int, long);

#define CLEARC 0xFF102030UL
#define PATTERN 0xA5A5C3C3UL

static int failures;

static void
say(const char *what, int ok, const char *detail)
{
    printf("%-46s %s%s%s\n", what, ok ? "ok" : "FAIL",
           detail ? "  " : "", detail ? detail : "");
    if (!ok) failures++;
}

/*
 * The judge, kept apart from what it judges so it can be aimed at a value
 * that is deliberately wrong.
 */
static int
disjoint(unsigned long aStart, unsigned long aLen,
         unsigned long bStart, unsigned long bLen)
{
    return (aStart + aLen <= bStart) || (bStart + bLen <= aStart);
}

static int
run(int w, int h, int appRow, int wantDepthFirst)
{
    OSMesaContext ctx;
    unsigned long *app;
    unsigned long org, bytes, dorg, dexp, page, colourEnd;
    void *zb; GLint dw, dh, bpv;
    int got;
    char name[80];

    app = (unsigned long *)malloc((unsigned)(appRow * h) * sizeof(unsigned long));
    if (!app) return 0;
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx) { free(app); return 0; }
    /*
     * After MakeCurrent, not before: OSMesaPixelStore works on the current
     * context, and calling it with none current is what the bus error was.
     */
    if (!OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, w, h)) {
        OSMesaDestroyContext(ctx); free(app); return 0;
    }
    if (appRow != w) {
        OSMesaPixelStore(OSMESA_ROW_LENGTH, appRow);
        if (!OSMesaMakeCurrent(ctx, app, GL_UNSIGNED_BYTE, w, h)) {
            OSMesaDestroyContext(ctx); free(app); return 0;
        }
    }
    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    sprintf(name, "%dx%d row %d %s", w, h, appRow,
            wantDepthFirst ? "(depth first)" : "(arena first)");

    if (wantDepthFirst) {
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
        glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT); glFinish();
        (void)OSMesaGetDepthBuffer(ctx, &dw, &dh, &bpv, &zb);
    }

    got = OSMGAMesaBufferTextureArena(ctx, &org, &bytes);

    if (!wantDepthFirst) {
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
        glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT); glFinish();
        (void)OSMesaGetDepthBuffer(ctx, &dw, &dh, &bpv, &zb);
    }

    dorg = OSMGAMesaBufferDepthOrigin();
    page = (unsigned long)vm_page_size;

    printf("\n[%s] surface %lux%lu stride %lu colour origin %lu\n", name,
           OSMGAMesaBufferWidth(), OSMGAMesaBufferHeight(),
           OSMGAMesaBufferStride(), OSMGAMesaBufferOrigin());
    printf("   arena %s", got ? "" : "none");
    if (got) printf("origin %lu bytes %lu", org, bytes);
    printf("   depth origin %lu\n", dorg);

    if (OSMGAMesaBufferStride() == 0UL) {
        OSMesaDestroyContext(ctx); free(app); return 0;
    }

    /*
     * The expected depth origin, from the colour surface alone -- an oracle
     * that does not call the code under test.
     */
    colourEnd = OSMGAMesaBufferOrigin() +
                OSMGAMesaBufferHeight() * OSMGAMesaBufferStride() * 4UL;
    dexp = (colourEnd + (page - 1UL)) & ~(page - 1UL);

    if (dorg != 0UL) {
        say("  depth origin equals the independent one", dorg == dexp, 0);
        if (got) {
            unsigned long dlen = OSMGAMesaBufferHeight() *
                                 OSMGAMesaBufferStride() * 2UL;

            say("  arena and depth are disjoint",
                disjoint(org, bytes, dorg, dlen), 0);
            say("  arena starts after the colour surface",
                org >= colourEnd, 0);
            /* the judge itself, aimed at a value that IS wrong */
            say("  (self-check) the same judge rejects a poisoned value",
                !disjoint(dorg, bytes, dorg, dlen), 0);
        }
    } else {
        printf("   no depth here -- the overlap cannot be judged\n");
    }

    /* the pattern: the WHOLE arena, not one word at its start */
    if (got && dorg != 0UL) {
        int fd = OSMGAMesaProbeDeviceFd();
        vm_address_t addr = 0;
        unsigned long span = bytes;
        unsigned long i, kept = 0UL, n;

        if (span > 1024UL * 1024UL) span = 1024UL * 1024UL;   /* enough, and quick */
        if (fd >= 0 &&
            vm_allocate(task_self(), &addr, (vm_size_t)span, TRUE) == KERN_SUCCESS) {
            if ((int)mmap((caddr_t)addr, (int)span, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, (long)org) != -1) {
                unsigned long *p = (unsigned long *)addr;

                n = span / sizeof(unsigned long);
                for (i = 0UL; i < n; i++) p[i] = PATTERN ^ (unsigned long)i;
                /* a frame that writes depth across the whole surface */
                glDepthMask(GL_TRUE);
                glMatrixMode(GL_PROJECTION); glLoadIdentity();
                glOrtho(0.0, (double)w, 0.0, (double)h, -1.0, 1.0);
                glMatrixMode(GL_MODELVIEW); glLoadIdentity();
                glBegin(GL_TRIANGLES);
                  glColor4ub(200,200,200,255);
                  glVertex3d(-10.0, -10.0, 0.5);
                  glVertex3d((double)w*3.0, -10.0, 0.5);
                  glVertex3d(-10.0, (double)h*3.0, 0.5);
                glEnd();
                glFinish();
                for (i = 0UL; i < n; i++)
                    if (p[i] == (PATTERN ^ (unsigned long)i)) kept++;
                printf("   pattern kept %lu/%lu words\n", kept, n);
                say("  the whole arena survives the drawing", kept == n, 0);
                (void)vm_deallocate(task_self(), addr, (vm_size_t)span);
            } else {
                (void)vm_deallocate(task_self(), addr, (vm_size_t)span);
                printf("   could not map the arena\n");
            }
        }
    }

    OSMesaDestroyContext(ctx);
    free(app);
    return 1;
}

int
main(void)
{
    printf("VRAM layout -- colour, the space depth would take, then the texture arena\n");
    (void)run(320, 240, 320, 0);
    (void)run(320, 240, 320, 1);
    (void)run(320, 240, 352, 0);      /* padded row: stride != width */
    (void)run(640, 480, 640, 0);
    (void)run(1024, 768, 1024, 0);    /* the window is the colour surface */
    printf("\n%s (%d failures)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===", failures);
    return failures ? 1 : 0;
}
