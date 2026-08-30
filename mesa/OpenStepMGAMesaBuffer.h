/*
 * OpenStepMGAMesaBuffer.h - M1-3c: give Mesa a buffer in video memory.
 *
 * The software rasteriser writes wherever it is pointed, so pointing it at
 * the card means both paths accumulate into one surface and a triangle that
 * falls back to software lands beside the ones that did not.  Without this
 * the two would draw into different memory and the frame would be split
 * between them, which is why per-primitive fallback needs this to exist.
 *
 * No Mesa headers here: the context is carried as an opaque pointer, so this
 * file stays readable and testable on its own.
 */

#ifndef OPENSTEP_MGA_MESA_BUFFER_H
#define OPENSTEP_MGA_MESA_BUFFER_H

/*
 * Called from OSMesaMakeCurrent with the buffer the application supplied.
 * Returns the buffer to render into -- video memory when acceleration is
 * available and the surface fits, and NULL to mean "leave the application's
 * alone", which is what happens for software rendering.
 *
 * `rowLength` is set to the stride the surface must use, in pixels, because
 * the engine takes the destination pitch from a register holding the
 * display's and will read the surface that way whatever Mesa was told.
 */
void *OpenStepMesaAccelBuffer(void *ctx, void *buffer,
                              int width, int height,
                              int rshift, int gshift, int bshift,
                              int appRowLength, int *rowLength);

/*
 * Where that surface is, as a byte offset into video memory, or 0 if the
 * application's own buffer is in use.  This is what a batch must name as its
 * destination, and it is deliberately not something the drawing code works
 * out for itself -- one place decides where the surface is.
 */
unsigned long OSMGAMesaBufferOrigin(void);
unsigned long OSMGAMesaBufferWidth(void);
unsigned long OSMGAMesaBufferHeight(void);
unsigned long OSMGAMesaBufferStride(void);

/*
 * Depth for the same surface, laid out as Mesa addresses it: one value per
 * pixel, `width` values per row -- which is the surface's pitch, and the
 * reason the batch had to be allowed to declare that pitch at all.  Returns
 * null unless a colour surface was taken and the value size is one the
 * engine writes, which is sixteen bits.
 */
/* Is this the context drawing into the surface right now?  Not the same as
 * owning it: a rebind at another size is refused and leaves the owner set. */
int OSMGAMesaBufferBoundTo(const void *ctx);
/*
 * Where a texture may live: after the colour surface and after the space a
 * depth buffer would take, reserved whether or not one was asked for.  Zero
 * means there is no room, which is a refusal and not a small arena.
 */
int OSMGAMesaBufferTextureArena(const void *ctx, unsigned long *origin,
                                unsigned long *bytes);
/*
 * The same arena, mapped.  Colour and depth each had a mapping and this did
 * not, so there was no way to put anything in it.
 */
void *OSMGAMesaBufferTextureMap(const void *ctx, unsigned long *origin,
                                unsigned long *bytes);
/*
 * Which surface the arena belongs to.  A residency record carries this and is
 * refused when it no longer matches -- an origin would not do, because the
 * next surface can be handed the same one.
 */
unsigned long OSMGAMesaBufferTexEpoch(void);
/* The application's own buffer, for putting back what was substituted. */
void *OSMGAMesaBufferApp(void);

void *OpenStepMesaAccelDepthBuffer(void *ctx, int width, int height,
                                   int bytesPerValue);

/* Where it is, as a byte offset into video memory, or 0. */
unsigned long OSMGAMesaBufferDepthOrigin(void);

/*
 * Copy the surface into the buffer the application handed us, which is
 * otherwise never written -- the picture is in video memory.  Does nothing
 * if no substitution happened, or if nothing has been drawn since the last
 * time, so calling it often is cheap when it is not needed.
 */
void OSMGAMesaBufferMirror(void);
unsigned long OSMGAMesaBufferCopies(void);
/* How often present mode was switched on and off, and how often a present
 * was refused, by the kernel's verdict (OSMGA_PRESENT_E_*, below 8). */
unsigned long OSMGAMesaBufferPresentOnCount(void);
unsigned long OSMGAMesaBufferPresentOffCount(void);
unsigned long OSMGAMesaBufferPresentRefused(unsigned long verdict);
void OSMGAMesaBufferMirrorNarrow(unsigned long x0, unsigned long y0,
                                 unsigned long x1, unsigned long y1);

/*
 * TEST ONLY, under the rule R20 set: a box copy exists to be TIMED, and the
 * disagreement count reads the driver's surface against the caller's array.
 * Neither is a feature -- one writes the caller's memory outside any
 * contract and the other exposes internal state that would harden into ABI
 * if it shipped.  The packaging script refuses a library defining them.
 */
#ifdef OSMGA_MESA_TESTHOOKS
unsigned long OSMGAMesaBufferDisagree(void);
void OSMGAMesaBufferMirrorBox(unsigned long x0, unsigned long y0,
                              unsigned long x1, unsigned long y1);
#endif /* OSMGA_MESA_TESTHOOKS */

/*
 * Deliver a surface known to hold one value by writing that value, instead
 * of reading the surface back.  Same bytes, and it does not touch video
 * memory.  See the definition for why the word is not read from there.
 */
void OSMGAMesaBufferFill(unsigned long word);

/*
 * On-screen delivery.  PresentMode(1) declares the caller's array stale --
 * the mirror, the clear's constant delivery and the rebind import all stand
 * down -- and Present() asks the kernel to blit the whole picture to
 * (dstX, dstY) on the visible screen, VRAM to VRAM, nothing crossing the
 * bus.  The kernel re-validates both rectangles; its refusal comes back in
 * *outVerdict as an OSMGA_PRESENT_* code.
 */
void OSMGAMesaBufferPresentMode(int on);
int  OSMGAMesaBufferPresent(long dstX, long dstY, unsigned long *outVerdict);
int  OSMGAMesaBufferPresentRect(unsigned long srcX, unsigned long srcY,
                                unsigned long w, unsigned long h,
                                long dstX, long dstY,
                                unsigned long *outVerdict);

/* Something has been drawn; the next mirror has work to do. */
void OSMGAMesaBufferSoiled(void);
/* Read it back, and take it back -- for a bracket that turns out to have
 * been a pure read.  See the definitions. */
int  OSMGAMesaBufferIsDirty(void);
void OSMGAMesaBufferUnsoil(void);

/*
 * Give the surface back.  Named the way the Mesa port calls it -- that tree
 * asks a back end to release without naming which one.
 */
void OpenStepMesaAccelReleaseBuffer(void *ctx);

#endif /* OPENSTEP_MGA_MESA_BUFFER_H */
