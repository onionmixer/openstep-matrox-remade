/*
 * openstep-mga-mesa-gl-test.c -- M1-3b-4: does the hook actually fire?
 *
 * An ordinary OSMesa program: make a context, draw a triangle, no knowledge
 * of any of this.  What is being checked is that the triangle reached the
 * hardware -- a hook that is installed but never called looks exactly like
 * one that works, so the count matters as much as the pixels.
 *
 * Built against the accelerated library, and against nothing else:
 *   cc -O -Wall -o /tmp/gltest openstep-mga-mesa-gl-test.c \
 *      -I<mesa>/include -L/tmp/OpenStepMesaMGA -lGL_mga
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <mach/mach.h>
#include <GL/osmesa.h>

/* Declared here rather than included: an ordinary program would not have
 * these headers, and using them would weaken what this test demonstrates. */
extern unsigned long OSMGAMesaHookDrawn(void);
extern unsigned long OSMGAMesaHookDeclined(void);
extern int OSMGAMesaProbeDeviceFd(void);

extern caddr_t mmap(caddr_t, int, int, int, int, long);

#define PROT_READ   0x01
#define PROT_WRITE  0x02
#define MAP_SHARED  0x0001
#define VRAM_BLOCK  (4UL * 1024UL * 1024UL)
#define STRIDE_DW   1024UL
#define ROWS        40UL
#define COLS        64UL
#define SENTINEL    0x5A5A5A5AUL

#define W 64
#define H 64

static int failures;

int
main(void)
{
    OSMesaContext ctx;
    static unsigned char buffer[W * H * 4];
    volatile unsigned long *vram = 0;
    unsigned long before, after, row, col, drawn;

    ctx = OSMesaCreateContext(OSMESA_RGBA, NULL);
    if (!ctx) {
        printf("OSMesaCreateContext failed\n");
        return 1;
    }
    if (!OSMesaMakeCurrent(ctx, buffer, GL_UNSIGNED_BYTE, W, H)) {
        printf("OSMesaMakeCurrent failed\n");
        return 1;
    }

    printf("M1-3b-4: an ordinary OSMesa program draws one triangle\n");

    /* The hook draws into video memory, not into this program's buffer --
     * that substitution is a later step -- so the result is read from there.
     * Mapping is possible only once the probe has opened the device, which
     * making the context has now done. */
    if (OSMGAMesaProbeDeviceFd() >= 0) {
        vm_address_t addr = 0;

        if (vm_allocate(task_self(), &addr,
                        (vm_size_t)(ROWS * STRIDE_DW * 4UL), TRUE)
                == KERN_SUCCESS &&
            (int)mmap((caddr_t)addr, (int)(ROWS * STRIDE_DW * 4UL),
                      PROT_READ | PROT_WRITE, MAP_SHARED,
                      OSMGAMesaProbeDeviceFd(), (long)VRAM_BLOCK) != -1)
            vram = (volatile unsigned long *)addr;
    }
    if (vram == 0) {
        printf("   no hardware here -- software rendering is the right "
               "answer and there is nothing to check\n");
        return 0;
    }
    for (row = 0UL; row < ROWS; row++)
        for (col = 0UL; col < COLS; col++)
            vram[row * STRIDE_DW + col] = SENTINEL;

    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)W, 0.0, (double)H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glShadeModel(GL_SMOOTH);

    before = OSMGAMesaHookDrawn();
    glBegin(GL_TRIANGLES);
      glColor3ub(255, 0, 0);   glVertex2f( 0.0f,  0.0f);
      glColor3ub(0, 255, 0);   glVertex2f( 0.0f, 20.0f);
      glColor3ub(0, 0, 255);   glVertex2f(40.0f, 20.0f);
    glEnd();
    glFinish();
    after = OSMGAMesaHookDrawn();

    printf("   hook drew %lu triangle(s), declined %lu\n",
           after - before, OSMGAMesaHookDeclined());
    if (after == before) {
        printf("   FAIL -- the hook never fired; this is software rendering "
               "wearing the hardware's name\n");
        failures++;
    }

    drawn = 0UL;
    for (row = 0UL; row < ROWS; row++)
        for (col = 0UL; col < COLS; col++)
            if (vram[row * STRIDE_DW + col] != SENTINEL)
                drawn++;
    printf("   video memory holds %lu pixels, wanted 400\n", drawn);
    if (drawn != 400UL)
        failures++;

    printf("%s\n", failures ? "FAIL"
                            : "PASS -- glVertex reached the engine");
    OSMesaDestroyContext(ctx);
    return failures ? 1 : 0;
}
