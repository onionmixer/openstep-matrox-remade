/*
 * OpenStepMGAMesaBuffer.c - see the header.
 */

#include <stdio.h>
#include <sys/types.h>
#include <mach/mach.h>

#include "OpenStepMGAMesaBuffer.h"
#include "OpenStepMGAMesaProbe.h"
#include "OpenStepMGAHW3D.h"   /* OSMGA_HW3D_PITCH_ALIGN */

extern caddr_t mmap(caddr_t, int, int, int, int, long);

#define PROT_READ   0x01
#define PROT_WRITE  0x02
#define MAP_SHARED  0x0001

static unsigned long bufOrigin;
static unsigned long bufWidth, bufHeight, bufStride;
static void *bufMapped;
static void *bufCtx;      /* the context the surface belongs to */
/*
 * And the context that is DRAWING into it, which is not the same question.
 *
 * A rebind at another size is refused and deliberately leaves the description
 * alone, so the owner is still that context while Mesa has gone back to the
 * application's buffer.  Asking who owns the surface therefore says yes when
 * nobody is using it, and the hardware path went on being installed against
 * a surface nothing was drawing into.
 */
static void *bufBound;
static void *bufApp;      /* what the application gave us */
static unsigned long bufAppRow;  /* and how its rows are laid out */
static int   bufDirty;
static void *depthMapped;
static unsigned long depthOrigin, depthBytes;
/*
 * The texture arena, mapped.  Colour and depth each have a mapping of their
 * own and the arena had none, so nothing could be copied into it -- the only
 * textures drawn so far were written by a raw client that mapped the device
 * itself.
 */
static void *texMapped;
static unsigned long texMapOrigin, texMapBytes;
/*
 * Which surface the arena belongs to.
 *
 * Not the origin: a surface released and another bound can be handed the same
 * origin with unrelated contents, and a residency record that compared
 * origins would call that its own.  This counts bindings instead, so a record
 * from a previous surface is refused by a number that never repeats.
 */
static unsigned long texEpoch;
static unsigned long bufBytes;

unsigned long OSMGAMesaBufferOrigin(void) { return bufOrigin; }
unsigned long OSMGAMesaBufferWidth(void)  { return bufWidth;  }
unsigned long OSMGAMesaBufferHeight(void) { return bufHeight; }
unsigned long OSMGAMesaBufferStride(void) { return bufStride; }
unsigned long OSMGAMesaBufferDepthOrigin(void) { return depthOrigin; }

/*
 * Where a texture may live.
 *
 * The window holds three things and has no owner: the kernel gives colour,
 * depth and texture the WHOLE window as their permitted range and checks only
 * that each reach lands inside it -- there is no intersection check anywhere
 * in the validator, on purpose.  So the layout is entirely userland's to keep
 * straight, and this is where it is decided:
 *
 *   [ colour ][ pad ][ depth reservation ][ pad ][ texture arena ... ] end
 *
 * The depth extent is RESERVED whether or not Mesa ever asks for a depth
 * buffer.  Depth is allocated lazily, so a texture placed immediately after
 * the colour surface would be sitting exactly where a later depth request
 * lands -- and the kernel would allow it.  Reserving costs the depth extent
 * on a surface that never uses depth (about six percent of the arena at
 * 320x240) and buys a layout with one order instead of two.
 *
 * The reservation is not padded.  Only the two starts are rounded to a page,
 * because only an offset has to be page-aligned to be mapped; the depth
 * allocator leaves its own length exact for the same reason.
 *
 * Answers only the context currently bound.  A rebinding at a different size
 * is refused rather than resized, and that refusal clears bufBound while
 * deliberately leaving the old surface mapped -- so "a surface exists" is not
 * the same question as "this caller may use it".
 */
int
OSMGAMesaBufferTextureArena(const void *ctx, unsigned long *origin,
                            unsigned long *bytes)
{
    OSMGAMesaProbe probe;
    unsigned long page, colourEnd, depthStart, depthEnd, texStart, avail;

    if (origin != 0) *origin = 0UL;
    if (bytes != 0)  *bytes  = 0UL;
    if (origin == 0 || bytes == 0)
        return 0;
    if (bufMapped == 0 || ctx == 0 || bufBound != ctx)
        return 0;

    page = (unsigned long)vm_page_size;
    if (page == 0UL || (page & (page - 1UL)) != 0UL)
        return 0;               /* not a power of two: the mask below lies */

    OSMGAMesaProbeRun(&probe);
    if (probe.verdict != OSMGA_PROBE_HARDWARE)
        return 0;
    if (bufOrigin < probe.caps[OSMGA_HW3D_CAP_VRAMOFF])
        return 0;
    avail = probe.caps[OSMGA_HW3D_CAP_VRAMLEN];

    /*
     * Everything below is an offset from the window start, and no product is
     * formed without first proving it fits -- a check that overflows is not a
     * check, which is the rule the rest of this file already follows.
     */
    colourEnd = bufOrigin - probe.caps[OSMGA_HW3D_CAP_VRAMOFF];
    if (bufStride == 0UL || bufHeight == 0UL)
        return 0;
    if (bufHeight > (0xFFFFFFFFUL - colourEnd) / (bufStride * 4UL))
        return 0;
    colourEnd += bufHeight * bufStride * 4UL;

    if (colourEnd > 0xFFFFFFFFUL - (page - 1UL))
        return 0;
    depthStart = (colourEnd + (page - 1UL)) & ~(page - 1UL);

    if (bufHeight > (0xFFFFFFFFUL - depthStart) / (bufStride * 2UL))
        return 0;
    depthEnd = depthStart + bufHeight * bufStride * 2UL;

    if (depthEnd > 0xFFFFFFFFUL - (page - 1UL))
        return 0;
    texStart = (depthEnd + (page - 1UL)) & ~(page - 1UL);

    /*
     * At the display's own resolution the colour surface is the whole window,
     * and there is no arena.  Nothing is returned rather than something small
     * and overlapping: zero here means "no texture acceleration", which is a
     * refusal the caller can act on.
     */
    if (texStart >= avail)
        return 0;

    *origin = probe.caps[OSMGA_HW3D_CAP_VRAMOFF] + texStart;
    *bytes  = avail - texStart;
    return 1;
}

unsigned long OSMGAMesaBufferTexEpoch(void) { return texEpoch; }

/*
 * The texture arena with a pointer to it.
 *
 * Mapped once and kept: the arena moves only when the surface changes, and
 * then the epoch moves with it.
 */
void *
OSMGAMesaBufferTextureMap(const void *ctx, unsigned long *origin,
                          unsigned long *bytes)
{
    unsigned long org = 0UL, len = 0UL;
    vm_address_t addr = 0;

    if (origin != 0) *origin = 0UL;
    if (bytes != 0)  *bytes = 0UL;
    if (!OSMGAMesaBufferTextureArena(ctx, &org, &len))
        return 0;
    if (texMapped != 0 && texMapOrigin == org && texMapBytes == len) {
        if (origin != 0) *origin = org;
        if (bytes != 0)  *bytes = len;
        return texMapped;
    }
    if (texMapped != 0) {
        (void)vm_deallocate(task_self(), (vm_address_t)texMapped,
                            (vm_size_t)texMapBytes);
        texMapped = 0; texMapBytes = 0UL; texMapOrigin = 0UL;
    }
    if (vm_allocate(task_self(), &addr, (vm_size_t)len, TRUE) != KERN_SUCCESS)
        return 0;
    if ((int)mmap((caddr_t)addr, (int)len, PROT_READ | PROT_WRITE,
                  MAP_SHARED, OSMGAMesaProbeDeviceFd(), (long)org) == -1) {
        (void)vm_deallocate(task_self(), addr, (vm_size_t)len);
        return 0;
    }
    texMapped = (void *)addr;
    texMapOrigin = org;
    texMapBytes = len;
    if (origin != 0) *origin = org;
    if (bytes != 0)  *bytes = len;
    return texMapped;
}

/* Is this the context drawing into the surface right now? */
int OSMGAMesaBufferBoundTo(const void *ctx)
{
    return bufBound != 0 && bufBound == ctx;
}

/* The application's own buffer, for putting back what was substituted. */
void *OSMGAMesaBufferApp(void) { return bufApp; }

/*
 * The same three under the names the hook points use.
 *
 * osmesa.c is written against a back end it does not name, so everything it
 * calls carries the OpenStepMesaAccel prefix; the names above are this back
 * end's own and are what the rest of these files use.  Forwarders rather than
 * one set of names, so that neither side has to know the other's spelling.
 */
int   OpenStepMesaAccelBoundTo(const void *ctx) { return OSMGAMesaBufferBoundTo(ctx); }
void *OpenStepMesaAccelAppBuffer(void)          { return OSMGAMesaBufferApp(); }
void  OpenStepMesaAccelMirror(void)             { OSMGAMesaBufferMirror(); }
unsigned long OpenStepMesaAccelStride(void)      { return OSMGAMesaBufferStride(); }

/*
 * The shared depth, out into a buffer of Mesa's own.
 *
 * This exists for one moment: the back end handing the surface back in the
 * middle of a context.  The colour is mirrored out there and the depth was
 * simply dropped -- DepthBuffer to nought, the software one turned back on,
 * and Mesa's allocator, whose own comment is "allocate new depth buffer, but
 * don't initialize it".  So an application that had rendered depth found
 * malloc's leavings in it afterwards.  Measured: a code of 8000 written
 * while accelerated read back as 0000 after the fallback.
 *
 * Only the case the rest of this file already restricts itself to: sixteen
 * bits, and a surface whose stride IS its width -- which is the precondition
 * the depth mapping is handed out under in the first place, and which makes
 * Mesa's own Width-by-Height layout the same layout.  Anything else returns
 * nought and the caller keeps the uninitialised buffer, which is no worse
 * than before this existed.
 *
 * The read is direct, and that is not an assumption: the settling the
 * hardware needs is for the first 64 bytes of the mapping window, and the
 * depth surface sits past the colour one on a page boundary.  Every depth
 * test in this tree reads this mapping the same way immediately after a
 * finish and gets what the engine wrote.
 */
int
OpenStepMesaAccelCopyDepth(void *dst, int width, int height, int bytesPerValue)
{
    const unsigned short *src;
    unsigned short *d;
    unsigned long y;

    if (dst == 0 || depthMapped == 0 || bytesPerValue != 2)
        return 0;
    if (bufStride != bufWidth)
        return 0;
    if ((unsigned long)width != bufWidth ||
        (unsigned long)height != bufHeight)
        return 0;

    src = (const unsigned short *)depthMapped;
    d   = (unsigned short *)dst;
    for (y = 0UL; y < bufHeight; y++) {
        const unsigned short *s = src + y * bufStride;
        unsigned short *t = d + y * bufWidth;
        unsigned long x;

        for (x = 0UL; x < bufWidth; x++)
            t[x] = s[x];
    }
    return 1;
}

/*
 * Said once, and only when a resource was asked for and refused.
 *
 * Returning nothing from the depth accessor is ordinary for a second
 * context, an unsupported depth width, or a surface that does not match --
 * those are decisions, not failures, and saying so every frame would be
 * noise.  What was worth saying and never was: the mapping itself failed, so
 * the hardware depth buffer is gone and Mesa is quietly back on its own.
 */
static void
depthGrumble(const char *why)
{
    static int said;

    if (said)
        return;
    said = 1;
    fprintf(stderr, "OpenStepMGA: %s; falling back to a software depth "
                    "buffer\n", why);
}

void *
OpenStepMesaAccelDepthBuffer(void *ctx, int width, int height,
                             int bytesPerValue)
{
    OSMGAMesaProbe probe;
    unsigned long need, tail, avail, page;
    vm_address_t addr = 0;

    /*
     * The fork check first, because the fast path below returns without it.
     *
     * OSMGAMesaProbeRun notices a changed pid and lets go of everything the
     * child inherited, including this depth mapping -- but only if it is
     * reached.  It was reached after the return below, so a child was handed
     * its parent's depth buffer, through a descriptor that was about to
     * become the parent's alone.
     */
    OSMGAMesaProbeRun(&probe);
    if (probe.verdict != OSMGA_PROBE_HARDWARE)
        return 0;

    /*
     * The same buffer only to the caller it was made for, at the size and
     * width it was made at.
     *
     * This used to return the mapping to anybody who asked.  A caller asking
     * for 32-bit depth, or for a different size, or from another context, got
     * the 16-bit buffer belonging to the first caller -- the checks that would
     * have refused all three are below this return, not above it.  Nothing
     * else ever allocates a second one, so the wrong buffer was never made;
     * it was handed over.
     */
    if (depthMapped != 0) {
        if (bufCtx == ctx && bytesPerValue == 2 &&
            (unsigned long)width == bufWidth &&
            (unsigned long)height == bufHeight)
            return depthMapped;
        return 0;
    }
    /*
     * Only alongside a colour surface of ours, and only at sixteen bits:
     * that is what the engine writes, and a depth buffer the software path
     * reads at a different width than the engine writes it would be worse
     * than no sharing at all.
     */
    if (bufMapped == 0 || bufCtx != ctx || bytesPerValue != 2)
        return 0;
    /*
     * Only when the surface's pitch IS its width.  The engine reads depth at
     * the pitch the batch declares, and Mesa addresses depth rows by the
     * framebuffer's width and nothing else -- so a caller that asked for a
     * longer row through OSMesaPixelStore would leave the two counting rows
     * differently from the second row onwards, sharing a buffer while
     * disagreeing about where everything in it is.  Colour survives that,
     * because there the row length is honoured on both sides; depth does not.
     */
    if (bufStride != bufWidth)
        return 0;
    if ((unsigned long)width != bufWidth ||
        (unsigned long)height != bufHeight)
        return 0;

    /*
     * Immediately after the colour surface, on a page boundary so it can be
     * mapped on its own.  Checked by division rather than by forming the
     * product, as everywhere else here.
     *
     * The page is the machine's, not a number written here.  This used to
     * round to 4096, which is right on a machine whose page is 4096 and
     * quietly wrong on this one, where it is 8192: the device refuses an
     * offset that is not a whole page -- EINVAL, not a silent rounding --
     * so the mapping failed, this returned nothing, and Mesa went back to a
     * software depth buffer without anyone being told.  It needs h*w to be a
     * multiple of 2048 to land right by accident, which 1024x768 does and
     * 800x600 and 320x240 do not; every test here has used the first.
     */
    page = (unsigned long)vm_page_size;
    if (page == 0UL || (page & (page - 1UL)) != 0UL)
        return 0;               /* not a power of two: the mask below lies */

    tail = bufOrigin - probe.caps[OSMGA_HW3D_CAP_VRAMOFF] +
           bufHeight * bufStride * 4UL;
    if (tail > 0xFFFFFFFFUL - (page - 1UL))
        return 0;
    tail = (tail + (page - 1UL)) & ~(page - 1UL);
    avail = probe.caps[OSMGA_HW3D_CAP_VRAMLEN];
    if (tail >= avail)
        return 0;
    if (bufHeight > (avail - tail) / (bufStride * 2UL))
        return 0;
    /*
     * The length is left exact.  Only the offset has to be a whole page --
     * measured, a length that is not a multiple of one maps perfectly well --
     * and padding it would be worse than useless: the capacity check just
     * above is made against the unpadded size, so a padded length can reach
     * past the end of the window, which the device refuses a page at a time.
     */
    need = bufHeight * bufStride * 2UL;

    if (vm_allocate(task_self(), &addr, (vm_size_t)need, TRUE) != KERN_SUCCESS) {
        depthGrumble("no room for a depth mapping");
        return 0;
    }
    if ((int)mmap((caddr_t)addr, (int)need, PROT_READ | PROT_WRITE,
                  MAP_SHARED, OSMGAMesaProbeDeviceFd(),
                  (long)(probe.caps[OSMGA_HW3D_CAP_VRAMOFF] + tail)) == -1) {
        (void)vm_deallocate(task_self(), addr, (vm_size_t)need);
        depthGrumble("the depth mapping was refused");
        return 0;
    }
    depthMapped = (void *)addr;
    depthBytes  = need;
    depthOrigin = probe.caps[OSMGA_HW3D_CAP_VRAMOFF] + tail;
    return depthMapped;
}

/*
 * Give the surface back.  Called when the context that owns it goes away,
 * and when a fork leaves a child holding a mapping made through a descriptor
 * that is no longer its own.  Without it the first context to be destroyed
 * took the only surface with it and nothing in the process could accelerate
 * again.
 */
void
OSMGAMesaBufferSoiled(void)
{
    bufDirty = 1;
}

/*
 * Was it already dirty, and put it back.
 *
 * A render bracket that turns out to have been a pure READ has to leave this
 * exactly as it found it.  Marking at the start of every bracket is what
 * keeps a frame made only of primitives this back end refused from being
 * mirrored away as "nothing happened" -- the software rasteriser writes the
 * same surface and does not announce it -- so the mark cannot simply be
 * dropped.  What it can be is taken back, once the bracket is over and the
 * spans have said it never wrote anything.
 */
int
OSMGAMesaBufferIsDirty(void)
{
    return bufDirty;
}

void
OSMGAMesaBufferUnsoil(void)
{
    bufDirty = 0;
}

/*
 * The caller's own picture, into the surface.
 *
 * In OSMesa the buffer handed over IS the colour buffer: whatever is already
 * in it is what the frame starts from, and drawing over a background the
 * caller loaded is an ordinary use of the library.  This back end puts video
 * memory behind that pointer and copies the surface back out after every
 * frame -- and for a long time the copy ran ONE WAY.  A caller's picture was
 * simply gone: the first frame mirrored video memory over it.  Measured at
 * 128 by 96, every one of the 10688 pixels outside a drawn quad lost what
 * the caller had put there.
 *
 * So this is the inbound half, and it runs at BIND -- first bind and
 * same-size rebind alike -- because that is the moment the caller says which
 * memory is the buffer.  Once per bind, not per frame.
 *
 * It does NOT mark the surface dirty.  After this the two sides agree, and
 * saying otherwise would only buy a mirror back out of what just came in.
 *
 * What it does not cure: a caller that writes its buffer directly while it
 * is still current.  Nothing can see that happen, and the next frame will
 * render from video memory that no longer matches.  Rebinding is the
 * boundary at which the two are made to agree again.
 */
static void
osmgaMesaBufferImport(void)
{
    const unsigned long *src;
    unsigned long *dst;
    unsigned long y;

    if (bufMapped == 0 || bufApp == 0)
        return;
    src = (const unsigned long *)bufApp;
    dst = (unsigned long *)bufMapped;
    for (y = 0UL; y < bufHeight; y++) {
        const unsigned long *s = src + y * bufAppRow;
        unsigned long *d = dst + y * bufStride;
        unsigned long x;

        for (x = 0UL; x < bufWidth; x++)
            d[x] = s[x];
    }
}

void
OSMGAMesaBufferMirror(void)
{
    const unsigned long *src;
    unsigned long *dst;
    unsigned long y, w;

    if (bufMapped == 0 || bufApp == 0 || !bufDirty)
        return;
    bufDirty = 0;

    /*
     * Row by row, because the surface is laid out at the display's stride
     * and the application's buffer at its own width -- copying the whole
     * thing in one go would slide every row along by the difference.
     *
     * This copies the entire surface every time, which is honest rather than
     * clever: only the triangles this back end drew have a bounding box we
     * know, and the software rasteriser writing into the same surface has
     * none we can see.  Narrowing it needs both halves to report what they
     * touched, and is recorded as work rather than guessed at here.
     */
    src = (const unsigned long *)bufMapped;
    dst = (unsigned long *)bufApp;
    w = bufWidth;
    for (y = 0UL; y < bufHeight; y++) {
        const unsigned long *s = src + y * bufStride;
        unsigned long *d = dst + y * bufAppRow;
        unsigned long x;

        for (x = 0UL; x < w; x++)
            d[x] = s[x];
    }
}

/*
 * Deliver a surface that is known to hold ONE VALUE, without reading it.
 *
 * A whole-surface clear leaves the surface holding a single word, so the
 * caller's array can be brought up to date by writing that word rather than
 * by walking video memory.  The bytes delivered are the same bytes; what is
 * different is that they are not read back at 5.36 MB/s.  Measured on this
 * machine: 0.585 ms rather than 146.722 ms at 512 by 384.
 *
 * Exactly the rows and columns the mirror writes -- bufWidth words at the
 * caller's own row length -- so a caller whose array is wider than the
 * picture keeps whatever it had in the padding, which is the mirror's
 * contract and has to stay the contract.
 *
 * The word is NOT read from the surface.  A client's first read after a
 * submission returns can hold what was there before the draw, and the
 * offset it would have been read from -- the window's first word -- is the
 * one the driver's own note says settles nothing (OpenStepMGAHW3D.h, and
 * REMAINING_WORK 3-18).  The word comes from OSMesa's own packed clear
 * pixel instead, which is the same word its software clear writes.
 */
void
OSMGAMesaBufferFill(unsigned long word)
{
    unsigned long *dst;
    unsigned long y, w;

    if (bufMapped == 0 || bufApp == 0)
        return;
    bufDirty = 0;

    dst = (unsigned long *)bufApp;
    w = bufWidth;
    for (y = 0UL; y < bufHeight; y++) {
        unsigned long *d = dst + y * bufAppRow;
        unsigned long x;

        for (x = 0UL; x < w; x++)
            d[x] = word;
    }
}

void
OpenStepMesaAccelReleaseBuffer(void *ctx)
{
    /*
     * A context lets go of its own surface and nobody else's.  Without this
     * the second context to be destroyed handed back the first one's
     * mappings, and the first one was left holding a depth pointer into
     * memory that had gone -- which killed the process the next time that
     * pointer was freed.
     *
     * A null context means let go regardless.  The fork path depends on that:
     * a child inherits the mappings and has to discard them, and it has no
     * context to name.
     */
    if (ctx != 0 && bufCtx != 0 && bufCtx != ctx)
        return;
    bufBound = 0;
    if (depthMapped != 0) {
        (void)vm_deallocate(task_self(), (vm_address_t)depthMapped,
                            (vm_size_t)depthBytes);
        depthMapped = 0;
        depthBytes = 0UL;
    }
    depthOrigin = 0UL;
    if (texMapped != 0) {
        (void)vm_deallocate(task_self(), (vm_address_t)texMapped,
                            (vm_size_t)texMapBytes);
        texMapped = 0;
        texMapBytes = 0UL;
        texMapOrigin = 0UL;
    }
    /* Every residency made against this surface is now stale, and saying so
     * is a number nobody can accidentally match. */
    texEpoch++;
    if (bufMapped != 0) {
        (void)vm_deallocate(task_self(), (vm_address_t)bufMapped,
                            (vm_size_t)bufBytes);
        bufMapped = 0;
        bufBytes = 0UL;
    }
    bufCtx = 0;
    bufApp = 0;
    bufAppRow = 0UL;
    bufDirty = 0;
    bufOrigin = 0UL;
    bufWidth = bufHeight = bufStride = 0UL;
}

void *
OpenStepMesaAccelBuffer(void *ctx, void *buffer, int width, int height,
                        int rshift, int gshift, int bshift, int appRowLength,
                        int *rowLength)
{
    OSMGAMesaProbe probe;
    unsigned long stride, need, avail;
    vm_address_t addr = 0;


    /*
     * Let go of the binding first, and put it back only where a surface is
     * really handed over.
     *
     * Done here rather than at each refusal because there are twenty of them
     * and a twenty-first will be added one day without this being thought
     * about.  Only THIS context's binding is released -- a refusal handed to
     * somebody else must not unseat whoever is drawing.
     */
    if (bufBound == ctx)
        bufBound = 0;

    if (width <= 0 || height <= 0 || rowLength == 0)
        return 0;
    /*
     * How the caller's own array is laid out, which is what the copy back
     * has to follow.  It is usually the width, but OSMesaPixelStore lets a
     * caller choose otherwise before making the context current, and
     * assuming the width would then write every row at the wrong offset.
     */
    if (appRowLength <= 0)
        return 0;

    /*
     * A width the engine cannot walk.
     *
     * The destination pitch has to be a multiple of 32 pixels; measured, one
     * that is not is accepted by everything and drawn somewhere else, and
     * the surface covers as little as one per cent of what the software path
     * covers.  The kernel refuses such a pitch now, and the chooser refuses
     * to install a triangle function for one -- but refusing HERE is what
     * stops the rest from happening at all.
     *
     * Without this a surface that can never be accelerated is still taken
     * into video memory, and then the software rasteriser draws into the
     * card across the bus and the whole thing is copied back once a frame.
     * That is slower than plain software rendering, and it buys nothing.
     *
     * Refusing simply leaves OSMesa drawing into the buffer the application
     * gave it, which is what it does without this back end at all.
     *
     * Asked of the ROW LENGTH rather than the width, because the row length
     * is what becomes the surface's stride a few lines below and therefore
     * what becomes the pitch the engine walks.  A 333-pixel picture laid out
     * on a 352-pixel row is fine; a 333-pixel row is not.
     */
    if (((unsigned long)appRowLength % OSMGA_HW3D_PITCH_ALIGN) != 0UL)
        return 0;

    /*
     * Binding again at the same size is the ordinary case and gets the same
     * surface back.  Clearing the description first, as this used to, meant a
     * triangle function installed for the previous binding could submit
     * against an origin of zero and a size of zero -- the description has to
     * stay true for as long as anything might still be drawing through it.
     */
    /*
     * The fork check before the fast path, for the same reason as in the
     * depth function: the return inside the block below never reached the
     * probe, so a child went on drawing into the surface it inherited.
     */
    OSMGAMesaProbeRun(&probe);
    if (probe.verdict != OSMGA_PROBE_HARDWARE)
        return 0;

    if (bufMapped != 0) {
        /*
         * There is one surface, and it belongs to the context that got it.
         * A second context was being handed the same memory with only its
         * application pointer swapped in, so two contexts drew over each
         * other and destroying either unmapped the surface the other was
         * still using.  A second context renders in software instead.
         */
        if (bufCtx != ctx)
            return 0;
        if (bufWidth == (unsigned long)width &&
            bufHeight == (unsigned long)height) {
            bufApp = buffer;    /* it may be a different buffer this time */
            bufAppRow = (unsigned long)appRowLength;
            *rowLength = (int)bufStride;
            bufBound = ctx;
            osmgaMesaBufferImport();
            return bufMapped;
        }
        /*
         * A different size would need a different surface, and there is only
         * one.  Refused rather than resized: the description is left alone
         * and the caller renders in software, which is wrong only in being
         * slow.
         */
        return 0;
    }

    /*
     * The engine lays a pixel out as 0x00RRGGBB and cannot be told otherwise,
     * so a caller whose context packs any other way is left with its own
     * buffer.  Sharing a surface with a disagreement about byte order is
     * worse than not sharing it: both paths write plausible pixels and the
     * picture comes out with two different colour orders in it, which is a
     * fault no amount of looking at one triangle will reveal.
     */
    if (rshift != 16 || gshift != 8 || bshift != 0)
        return 0;

    /*
     * The surface is laid out the way Mesa already lays it out -- at the
     * caller's own row length -- rather than at the display's stride.  The
     * engine used to force the latter, and following it meant overriding
     * Mesa's stride and, worse, that the software rasteriser's depth buffer
     * could never share ours: Mesa addresses depth at the surface's width and
     * has no setting for anything else.  The batch declares the pitch now, so
     * both can simply agree with Mesa.
     */
    stride = (unsigned long)appRowLength;
    avail  = probe.caps[OSMGA_HW3D_CAP_VRAMLEN];
    if (stride == 0UL || (unsigned long)width > stride)
        return 0;               /* a row would run into the next */
    if (stride > probe.caps[OSMGA_HW3D_CAP_STRIDE])
        return 0;               /* wider than the pitch register is known to hold */

    /*
     * Rows times a row of bytes, checked by division so the product is never
     * formed: a height a caller is entitled to ask for can still overflow a
     * thirty-two bit multiply, and a check that overflows is not a check.
     */
    if ((unsigned long)height > avail / (stride * 4UL))
        return 0;
    need = (unsigned long)height * stride * 4UL;

    if (vm_allocate(task_self(), &addr, (vm_size_t)need, TRUE) != KERN_SUCCESS)
        return 0;
    if ((int)mmap((caddr_t)addr, (int)need, PROT_READ | PROT_WRITE,
                  MAP_SHARED, OSMGAMesaProbeDeviceFd(),
                  (long)probe.caps[OSMGA_HW3D_CAP_VRAMOFF]) == -1) {
        (void)vm_deallocate(task_self(), addr, (vm_size_t)need);
        return 0;
    }

    bufMapped = (void *)addr;
    bufCtx    = ctx;
    bufBound  = ctx;
    bufApp    = buffer;
    bufAppRow = (unsigned long)appRowLength;
    bufDirty  = 0;
    bufBytes  = need;
    bufOrigin = probe.caps[OSMGA_HW3D_CAP_VRAMOFF];
    bufWidth  = (unsigned long)width;
    bufHeight = (unsigned long)height;
    bufStride = stride;

    /*
     * Nothing to override: Mesa is already laying the surface out this way,
     * and the batch will tell the engine to read it the same.  Nothing is
     * flipped either -- OSMesa puts GL row y at base + y * pitch by default
     * and the engine draws its rows from the origin the same way, so the two
     * agree without being told to.
     */
    *rowLength = 0;
    osmgaMesaBufferImport();
    return bufMapped;
}
