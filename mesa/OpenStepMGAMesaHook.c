/*
 * OpenStepMGAMesaHook.c - see the header.
 *
 * The shape of this file follows osmesa.c's own: a chooser that returns a
 * function or NULL, and a triangle function that reads the vertex buffer.
 * Deviating from that would mean guessing at conventions the software driver
 * already demonstrates.
 */

#include "glheader.h"
#include "context.h"
#include "types.h"
#include "vb.h"

#include "OpenStepMGAMesaProbe.h"
#include "OpenStepMGAMesaTriangle.h"

/*
 * Where the hardware draws.  Still the offscreen block the tests have always
 * used: pointing Mesa's own buffer at video memory is a separate step, and
 * doing both at once would leave a wrong picture with two possible causes.
 */
#define OSMGA_HOOK_DSTORG (4UL * 1024UL * 1024UL)

static unsigned long hookDrawn;
static unsigned long hookDeclined;

/*
 * One triangle, straight to the engine.
 *
 * A refusal here is not fatal and must not be silent: it means the geometry
 * was outside what the driver accepts, and the honest response is to leave
 * the triangle undrawn and say so, rather than to give up acceleration for
 * the whole program on account of one triangle.
 */
static void
osmgaMesaTriangle(GLcontext *ctx, GLuint v0, GLuint v1, GLuint v2, GLuint pv)
{
    struct vertex_buffer *VB = ctx->VB;
    OSMGAHW3DBatch *batch = OSMGAMesaProbeBatch();
    OSMGAHW3DSubmitBlock res;
    OSMGAMesaVertex a, b, c, prov;
    int n;

    if (batch == 0) {
        /* The chooser already established this; if it has changed under us
         * the triangle is lost, so say so rather than lose it quietly. */
        hookDeclined++;
        OSMGAMesaProbeRevoke("the command window went away mid-frame");
        return;
    }

#define OSMGA_LOAD(dst, idx)                                             \
    do {                                                                 \
        (dst).x = (long)VB->Win.data[idx][0];                            \
        (dst).y = (long)VB->Win.data[idx][1];                            \
        (dst).r = (unsigned long)VB->ColorPtr->data[idx][0];             \
        (dst).g = (unsigned long)VB->ColorPtr->data[idx][1];             \
        (dst).b = (unsigned long)VB->ColorPtr->data[idx][2];             \
    } while (0)

    OSMGA_LOAD(a, v0);
    OSMGA_LOAD(b, v1);
    OSMGA_LOAD(c, v2);
#undef OSMGA_LOAD

    /*
     * Flat shading takes its colour from the provoking vertex, which is what
     * pv names; smooth shading interpolates and does not use it.  Reading pv
     * in both cases would have looked harmless and been wrong in one.
     */
    if (ctx->Light.ShadeModel == GL_FLAT) {
        prov.x = (long)VB->Win.data[pv][0];
        prov.y = (long)VB->Win.data[pv][1];
        prov.r = (unsigned long)VB->ColorPtr->data[pv][0];
        prov.g = (unsigned long)VB->ColorPtr->data[pv][1];
        prov.b = (unsigned long)VB->ColorPtr->data[pv][2];
        n = OSMGAMesaBuildTriangle(&a, &b, &c, &prov, batch->tri);
    } else {
        n = OSMGAMesaBuildTriangle(&a, &b, &c, (const OSMGAMesaVertex *)0,
                                   batch->tri);
    }
    if (n == 0)
        return;                 /* no area; nothing to draw and no error */

    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = (unsigned long)n;
    batch->state.dstorg = OSMGA_HOOK_DSTORG;

    if (OSMGAMesaProbeSubmit(&res) != 0) {
        /*
         * This triangle is already lost -- Mesa dispatched it here and there
         * is no returning it to the software path.  Giving up acceleration
         * now at least stops the next one being lost the same way, and the
         * loss is recorded rather than absorbed.
         */
        hookDeclined++;
        OSMGAMesaProbeRevoke("the driver refused a batch");
        return;
    }
    hookDrawn++;
}

/*
 * Whether this state is one we can draw.  Modelled on the software driver's
 * own chooser: say no by returning NULL and the software path takes it.
 *
 * Everything not yet built is refused rather than approximated.  Depth,
 * texture and blending all exist in the driver and have all been measured,
 * but none of them is wired through this file yet, and drawing them wrong
 * would be worse than drawing them slowly.
 */
static triangle_func
osmgaMesaChooseTriangle(GLcontext *ctx)
{
    OSMGAMesaProbe probe;

    OSMGAMesaProbeRun(&probe);
    if (probe.verdict != OSMGA_PROBE_HARDWARE) return NULL;

    /*
     * Refuse everything that is not ordinary rendering.  In feedback and
     * selection Mesa installs triangle functions that record rather than
     * draw, and glRenderMode would silently return nothing if we replaced
     * one of them; NoRaster asks for the drawing to be discarded, which is
     * not something to answer by drawing.
     */
    if (ctx->RenderMode != GL_RENDER) return NULL;
    if (ctx->NoRaster)                return NULL;

    /*
     * Colour-index mode keeps its colours in IndexPtr, and reading RGB out
     * of ColorPtr there would draw whatever happened to be in an array this
     * mode does not maintain.
     */
    if (!ctx->Visual->RGBAflag)       return NULL;

    /*
     * Ask for the batch here rather than per triangle.  Once the function is
     * installed Mesa dispatches every triangle to it, and returning without
     * drawing loses that triangle for good -- there is no way back to the
     * software function from inside the call.  Anything that can be found
     * out in advance has to be found out here.
     */
    if (OSMGAMesaProbeBatch() == 0)   return NULL;

    /* Neither of these reaches RasterMask, so they are asked about here. */
    if (ctx->Polygon.SmoothFlag)      return NULL;
    if (ctx->Polygon.StippleFlag)     return NULL;

    /*
     * RasterMask is the union of everything that makes rasterisation more
     * than plain filling -- alpha test, blending, depth, fog, logic op,
     * scissor, stencil, colour masking, texture (state.c, where it is
     * composed).  Refusing any bit we do not expressly allow is what keeps a
     * state we have never seen from being drawn as though it were plain,
     * including states added to Mesa after this was written.
     *
     * ALPHABUF_BIT has to be allowed or nothing is ever accelerated: OSMesa
     * asks for a software alpha buffer whenever the visual has alpha bits,
     * which every 32-bit format here does, so the bit is set in even the
     * plainest context.  Letting it through is sound only because everything
     * that would READ alpha -- blending and the alpha test -- is refused by
     * its own bit.  What it leaves behind is recorded in REMAINING_WORK: the
     * software alpha buffer does not learn about pixels we drew.
     */
    if ((ctx->RasterMask & ~(GLuint)ALPHABUF_BIT) != 0)
        return NULL;

    return osmgaMesaTriangle;
}

void
OpenStepMesaAccelUpdateState(GLcontext *ctx)
{
    triangle_func f = osmgaMesaChooseTriangle(ctx);

    /*
     * Only ever replace with something; never write NULL.  The software
     * driver has just put its own choice here, and overwriting that with
     * NULL would take away an acceleration that has nothing to do with us.
     */
    if (f != 0)
        ctx->Driver.TriangleFunc = f;
}

unsigned long OSMGAMesaHookDrawn(void)    { return hookDrawn; }
unsigned long OSMGAMesaHookDeclined(void) { return hookDeclined; }
