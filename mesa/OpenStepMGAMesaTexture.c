/*
 * Getting a texture into video memory, and keeping track of whether it is
 * still there.
 *
 * Mesa keeps its own copy of every texture in host memory and hands the
 * driver a pointer to it.  The engine reads from video memory, so a copy has
 * to be made -- and then the question is when it stops being true.
 *
 * What Mesa gives us, read rather than assumed:
 *
 *   - gl_texture_image has a DriverData slot and Mesa never touches it: the
 *     only two mentions in the whole tree are comments, one of which says the
 *     proxy clear deliberately spares it.  The residency record lives there.
 *   - glTexImage2D REUSES the existing image struct and frees only its Data
 *     (teximage.c:1497-1503, :1376-1379), so a record survives a redefinition
 *     and has to be thrown away in the TexImage hook.
 *   - Driver.DeleteTexture runs BEFORE the images are freed (texobj.c:505,
 *     :634, context.c:526), so it is the one chance to give the arena back,
 *     and it has to walk every level because nothing else will.
 *   - glCompressedTexImage2DARB replaces the bytes and never calls TexImage
 *     (teximage.c:2787-2824), which is why a compressed image is refused
 *     outright rather than tracked.
 *
 * The copy is made when the texture is first drawn with, not when it is
 * defined: a texture that is never used costs nothing, and at definition
 * time there may be no arena yet.
 */
#include <string.h>

#include "glheader.h"
#include "context.h"
#include "types.h"
#include "mem.h"

#include "OpenStepMGAMesaBuffer.h"
#include "OpenStepMGAMesaHook.h"
#include "OpenStepMGAMesaTexArena.h"
#include "OpenStepMGAMesaTexture.h"
#include "OpenStepMGAHW3D.h"

/*
 * What we know about one image's copy in video memory.
 *
 * The epoch is the surface's, not a serial of our own: an origin would be
 * claimed by the next surface to be handed the same place.
 */
typedef struct {
    unsigned long origin;
    unsigned long bytes;
    unsigned long epoch;
    unsigned long w, h;
    /*
     * 1 for the single-level residency every texture had until M12; 1+N
     * when the block holds a mip chain of N maps below the base, with
     * levelOff[i] naming level i+1's byte offset from origin.  A record
     * of the wrong shape is dropped and rebuilt, like a size change.
     */
    unsigned long levelCount;
    unsigned long levelOff[4];
    int valid;                  /* the bytes in video memory are current */
} OSMGAMesaTexRes;

static unsigned long texUploads, texRefused, texEvicted;

static void (*prevTexImage)(GLcontext *, GLenum, struct gl_texture_object *,
                            GLint, GLint, const struct gl_texture_image *);
static void (*prevTexSubImage)(GLcontext *, GLenum,
                               struct gl_texture_object *, GLint, GLint, GLint,
                               GLsizei, GLsizei, GLint,
                               const struct gl_texture_image *);
static void (*prevDeleteTexture)(GLcontext *, struct gl_texture_object *);

unsigned long OSMGAMesaTexUploads(void)  { return texUploads; }
unsigned long OSMGAMesaTexRefused(void)  { return texRefused; }
unsigned long OSMGAMesaTexEvicted(void)  { return texEvicted; }

/*
 * Is this an image this back end can carry at all?
 *
 * Refusing here rather than half-way through is what sends the triangle to
 * the software path with nothing half-done behind it.
 */
static int
osmgaTexUsable(const struct gl_texture_image *img)
{
    if (img == 0 || img->Data == 0)
        return 0;
    if (img->IsCompressed)              /* never reaches the TexImage hook */
        return 0;
    if (img->Border != 0)               /* Width includes it; not handled */
        return 0;
    /*
     * GL_RGB, and not GL_RGBA.
     *
     * Not because the engine cannot carry four bytes, but because of what
     * GL_REPLACE means: with an RGBA texture the fragment's alpha comes from
     * the TEXTURE (texture.c:2419-2426, "Av = At"), and with RGB it comes
     * from the fragment -- which is the interpolated vertex alpha the builder
     * already programs.  So RGB is right by construction, and RGBA would need
     * the engine's texture alpha measured all the way to the destination
     * before it could be offered.
     */
    if (img->Format != GL_RGB && img->Format != GL_RGBA)
        return 0;
    if (img->Width == 0 || img->Height == 0)
        return 0;
    /*
     * The pitch must be at least the width and the engine's field holds
     * 2047, so 2048 is not a width this can take however large the limit on
     * the dimension says it is.
     */
    if (img->Width > OSMGA_HW3D_TEX_MAX_PIT ||
        img->Height > OSMGA_HW3D_TEX_MAX_DIM)
        return 0;
    return 1;
}

static void
osmgaTexDrop(struct gl_texture_image *img)
{
    OSMGAMesaTexRes *r;

    if (img == 0 || img->DriverData == 0)
        return;
    r = (OSMGAMesaTexRes *)img->DriverData;
    if (r->origin != 0UL)
        (void)OSMGAMesaTexFree(r->origin, r->epoch);
    FREE(r);
    img->DriverData = 0;
    texEvicted++;
}

/*
 * The bytes.
 *
 * Mesa stores a GL_RGBA image as four bytes a texel in that order, tightly
 * packed at Width by Height -- the client's unpack alignment applies to what
 * it reads, not to what Mesa keeps.  The engine wants the number 0x00RRGGBB.
 * Those are different things, so the bytes are read one at a time and the
 * number is built: a cast would be right on one endianness and silently
 * wrong on the other.
 */
static void
osmgaTexCopy(const struct gl_texture_image *img, unsigned long *dst,
             unsigned long pitch)
{
    const GLubyte *src = img->Data;
    unsigned long step = (img->Format == GL_RGB) ? 3UL : 4UL;
    unsigned long x, y;

    for (y = 0UL; y < (unsigned long)img->Height; y++) {
        const GLubyte *row = src + y * (unsigned long)img->Width * step;
        unsigned long *out = dst + y * pitch;

        for (x = 0UL; x < (unsigned long)img->Width; x++) {
            /*
             * The top byte carries the texture's own alpha for GL_RGBA and
             * is left at NOUGHT for GL_RGB -- not 0xFF.
             *
             * An RGB texture's alpha is never meant to be read: the engine is
             * told to take the interpolated one instead.  Leaving the byte
             * empty means that if it ever IS read the picture goes black in
             * the alpha channel and says so, which is exactly how the last
             * such bug was found.  A texture carrying 255 would have hidden
             * it.  This is a poison value and it is only safe while the gate
             * takes GL_REPLACE alone; a mode that can reference texture alpha
             * has to revisit it.
             */
            out[x] = ((unsigned long)row[x * step + 0UL] << 16)
                   | ((unsigned long)row[x * step + 1UL] << 8)
                   |  (unsigned long)row[x * step + 2UL]
                   | ((step == 4UL)
                      ? ((unsigned long)row[x * step + 3UL] << 24) : 0UL);
        }
    }
}

/*
 * Make sure the currently bound texture is in video memory, and say where.
 *
 * Zero means it is not there and cannot be put there, which is the caller's
 * cue to draw this triangle in software.
 */
static int
osmgaTexResidentN(void *ctxv, struct gl_texture_object *tObj,
                  unsigned long levels, unsigned long *origin,
                  unsigned long *w, unsigned long *h, unsigned long *pitch,
                  unsigned long *mipOrg)
{
    GLcontext *ctx = (GLcontext *)ctxv;
    struct gl_texture_image *img;
    OSMGAMesaTexRes *r;
    unsigned long aOrg = 0UL, aLen = 0UL, epoch;
    void *map;
    unsigned long need, li, off;

    if (origin != 0) *origin = 0UL;
    if (tObj == 0 || levels == 0UL || levels > 5UL)
        return 0;
    /*
     * The level the software path would sample (triangle.c:658), not level
     * zero.  A CHAIN is only ever asked for with BaseLevel zero -- the
     * admission gate holds that -- so the levels below the base are
     * Image[1..N].
     */
    if (tObj->BaseLevel < 0 || tObj->BaseLevel >= MAX_TEXTURE_LEVELS)
        return 0;
    if (levels > 1UL && tObj->BaseLevel != 0)
        return 0;
    img = tObj->Image[tObj->BaseLevel];
    if (!osmgaTexUsable(img)) {
        texRefused++;
        return 0;
    }
    /*
     * Every level of the chain, EVERY time: completeness says the levels
     * exist at the right sizes, but Data and compression are residency's
     * own contract (a compressed TexImage never reaches our hook).
     */
    for (li = 1UL; li < levels; li++) {
        const struct gl_texture_image *lv = tObj->Image[li];

        if (!osmgaTexUsable(lv) ||
            (unsigned long)lv->Width  != ((unsigned long)img->Width  >> li) ||
            (unsigned long)lv->Height != ((unsigned long)img->Height >> li)) {
            texRefused++;
            return 0;
        }
    }

    map = OSMGAMesaBufferTextureMap(ctx, &aOrg, &aLen);
    if (map == 0) {
        texRefused++;
        return 0;
    }
    epoch = OSMGAMesaBufferTexEpoch();
    OSMGAMesaTexArenaSet(aOrg, aLen, epoch);

    r = (OSMGAMesaTexRes *)img->DriverData;
    if (r != 0 && (r->epoch != epoch || r->w != (unsigned long)img->Width ||
                   r->h != (unsigned long)img->Height ||
                   r->levelCount != levels)) {
        /* the surface moved under it, the image was redefined at another
         * size without our hook seeing it, or the record has the wrong
         * SHAPE -- single where a chain is wanted or the other way */
        osmgaTexDrop(img);
        r = 0;
    }
    if (r == 0) {
        r = (OSMGAMesaTexRes *)MALLOC(sizeof *r);
        if (r == 0) {
            texRefused++;
            return 0;
        }
        memset(r, 0, sizeof *r);
        r->epoch = epoch;
        r->w = (unsigned long)img->Width;
        r->h = (unsigned long)img->Height;
        r->levelCount = levels;
        img->DriverData = r;
    }
    if (r->origin == 0UL) {
        need = 0UL;
        for (li = 0UL; li < levels; li++) {
            unsigned long lb = (r->w >> li) * (r->h >> li) * 4UL;

            lb = (lb + 31UL) & ~31UL;   /* the register's own alignment */
            if (li != 0UL)
                r->levelOff[li - 1UL] = need;
            need += lb;
        }
        if (!OSMGAMesaTexAlloc(need, epoch, &r->origin)) {
            /* the arena is full, or there is none: software, not a botched
             * draw */
            osmgaTexDrop(img);
            texRefused++;
            return 0;
        }
        r->bytes = need;
        r->valid = 0;
    }
    if (!r->valid) {
        /*
         * About to WRITE texels into the arena.  Trapezoids still pending in
         * the batch may reference this very region (a re-upload of the same
         * texture, or an evicted slot being reused), and they must sample
         * the OLD texels -- so they ship first.  Uploads are lazy (the
         * TexImage wrappers only invalidate), which makes this copy the one
         * choke point where the ordering can be enforced.
         */
        OSMGAMesaHookFlushPending();
        for (li = 0UL; li < levels; li++) {
            off = (li == 0UL) ? 0UL : r->levelOff[li - 1UL];
            osmgaTexCopy(tObj->Image[li],
                         (unsigned long *)((char *)map +
                                           (r->origin - aOrg) + off),
                         r->w >> li);
        }
        r->valid = 1;
        texUploads++;
    }
    if (origin != 0) *origin = r->origin;
    if (w != 0)      *w = r->w;
    if (h != 0)      *h = r->h;
    if (pitch != 0)  *pitch = r->w;
    if (mipOrg != 0)
        for (li = 0UL; li < 4UL; li++)
            mipOrg[li] = (li + 1UL < levels)
                             ? r->origin + r->levelOff[li] : 0UL;
    return 1;
}

int
OSMGAMesaTexResident(void *ctxv, struct gl_texture_object *tObj,
                     unsigned long *origin, unsigned long *w,
                     unsigned long *h, unsigned long *pitch)
{
    return osmgaTexResidentN(ctxv, tObj, 1UL, origin, w, h, pitch,
                             (unsigned long *)0);
}

/*
 * The same, for whatever 2D texture is bound now.
 *
 * The draw path has a context and nothing else; digging the object out of the
 * unit belongs here rather than in three callers.
 */
int
OSMGAMesaTexResidentCurrent(void *ctxv, unsigned long mipLevels,
                            unsigned long *origin,
                            unsigned long *w, unsigned long *h,
                            unsigned long *pitch, unsigned long *mipOrg)
{
    GLcontext *ctx = (GLcontext *)ctxv;

    if (ctx == 0)
        return 0;
    /*
     * Unit ZERO, not the active one.  glActiveTexture moves CurrentUnit and
     * the client can leave it on a unit that is not enabled, while Mesa's
     * single-unit texture path samples unit zero -- so following CurrentUnit
     * would upload one object and draw with another.
     */
    return osmgaTexResidentN(ctxv, ctx->Texture.Unit[0].CurrentD[2],
                             mipLevels + 1UL, origin, w, h, pitch, mipOrg);
}

/* ---- the hooks ---- */

static void
osmgaTexImage(GLcontext *ctx, GLenum target, struct gl_texture_object *tObj,
              GLint level, GLint internalFormat,
              const struct gl_texture_image *image)
{
    /*
     * The image struct is reused across a redefinition, so whatever we had is
     * about to describe the wrong thing.  The pointer is const, but the level
     * is enough to reach the mutable one.
     */
    if (tObj != 0 && level >= 0 && level < MAX_TEXTURE_LEVELS)
        osmgaTexDrop(tObj->Image[level]);
    /* A chain lives on the BASE image's record; redefining any lower
     * level reshapes the chain, so the base record goes too. */
    if (tObj != 0 && level > 0 && tObj->Image[0] != 0)
        osmgaTexDrop(tObj->Image[0]);
    (void)target; (void)internalFormat; (void)image;
    if (prevTexImage != 0)
        (*prevTexImage)(ctx, target, tObj, level, internalFormat, image);
}

static void
osmgaTexSubImage(GLcontext *ctx, GLenum target, struct gl_texture_object *tObj,
                 GLint level, GLint xoffset, GLint yoffset,
                 GLsizei width, GLsizei height, GLint internalFormat,
                 const struct gl_texture_image *image)
{
    /*
     * The bytes changed under a pointer that did not, which is the case a
     * dirty flag on the object would miss.  The copy is stale; the block
     * stays, because the size did not change.
     */
    if (tObj != 0 && level >= 0 && level < MAX_TEXTURE_LEVELS &&
        tObj->Image[level] != 0 && tObj->Image[level]->DriverData != 0)
        ((OSMGAMesaTexRes *)tObj->Image[level]->DriverData)->valid = 0;
    /* The chain's copy of a lower level lives under the base record. */
    if (tObj != 0 && level > 0 && tObj->Image[0] != 0 &&
        tObj->Image[0]->DriverData != 0)
        ((OSMGAMesaTexRes *)tObj->Image[0]->DriverData)->valid = 0;
    (void)target; (void)xoffset; (void)yoffset;
    (void)width; (void)height; (void)internalFormat; (void)image;
    if (prevTexSubImage != 0)
        (*prevTexSubImage)(ctx, target, tObj, level, xoffset, yoffset,
                           width, height, internalFormat, image);
}

static void
osmgaDeleteTexture(GLcontext *ctx, struct gl_texture_object *tObj)
{
    GLint i;

    /* Every level, because Mesa frees the images straight after this and
     * asks nobody about them. */
    if (tObj != 0)
        for (i = 0; i < MAX_TEXTURE_LEVELS; i++)
            osmgaTexDrop(tObj->Image[i]);
    if (prevDeleteTexture != 0)
        (*prevDeleteTexture)(ctx, tObj);
}

void
OSMGAMesaTexInstall(void *ctxv)
{
    GLcontext *ctx = (GLcontext *)ctxv;

    if (ctx == 0)
        return;
    if (ctx->Driver.TexImage != osmgaTexImage) {
        prevTexImage = ctx->Driver.TexImage;
        ctx->Driver.TexImage = osmgaTexImage;
    }
    if (ctx->Driver.TexSubImage != osmgaTexSubImage) {
        prevTexSubImage = ctx->Driver.TexSubImage;
        ctx->Driver.TexSubImage = osmgaTexSubImage;
    }
    if (ctx->Driver.DeleteTexture != osmgaDeleteTexture) {
        prevDeleteTexture = ctx->Driver.DeleteTexture;
        ctx->Driver.DeleteTexture = osmgaDeleteTexture;
    }
}
