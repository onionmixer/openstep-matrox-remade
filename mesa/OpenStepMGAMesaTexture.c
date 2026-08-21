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
            out[x] = ((unsigned long)row[x * step + 0UL] << 16)
                   | ((unsigned long)row[x * step + 1UL] << 8)
                   |  (unsigned long)row[x * step + 2UL];
        }
    }
}

/*
 * Make sure the currently bound texture is in video memory, and say where.
 *
 * Zero means it is not there and cannot be put there, which is the caller's
 * cue to draw this triangle in software.
 */
int
OSMGAMesaTexResident(void *ctxv, struct gl_texture_object *tObj,
                     unsigned long *origin, unsigned long *w,
                     unsigned long *h, unsigned long *pitch)
{
    GLcontext *ctx = (GLcontext *)ctxv;
    struct gl_texture_image *img;
    OSMGAMesaTexRes *r;
    unsigned long aOrg = 0UL, aLen = 0UL, epoch;
    void *map;
    unsigned long need;

    if (origin != 0) *origin = 0UL;
    if (tObj == 0)
        return 0;
    /*
     * The level the software path would sample (triangle.c:658), not level
     * zero.  Uploading one image while the other path reads another is a
     * disagreement nobody would look for.
     */
    if (tObj->BaseLevel < 0 || tObj->BaseLevel >= MAX_TEXTURE_LEVELS)
        return 0;
    img = tObj->Image[tObj->BaseLevel];
    if (!osmgaTexUsable(img)) {
        texRefused++;
        return 0;
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
                   r->h != (unsigned long)img->Height)) {
        /* the surface moved under it, or the image was redefined at another
         * size without our hook seeing it */
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
        img->DriverData = r;
    }
    if (r->origin == 0UL) {
        need = r->w * r->h * 4UL;
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
        osmgaTexCopy(img, (unsigned long *)((char *)map +
                                            (r->origin - aOrg)), r->w);
        r->valid = 1;
        texUploads++;
    }
    if (origin != 0) *origin = r->origin;
    if (w != 0)      *w = r->w;
    if (h != 0)      *h = r->h;
    if (pitch != 0)  *pitch = r->w;
    return 1;
}

/*
 * The same, for whatever 2D texture is bound now.
 *
 * The draw path has a context and nothing else; digging the object out of the
 * unit belongs here rather than in three callers.
 */
int
OSMGAMesaTexResidentCurrent(void *ctxv, unsigned long *origin,
                            unsigned long *w, unsigned long *h,
                            unsigned long *pitch)
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
    return OSMGAMesaTexResident(ctxv, ctx->Texture.Unit[0].CurrentD[2],
                                origin, w, h, pitch);
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
