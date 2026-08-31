#ifndef OPENSTEP_MGA_MESA_TEXTURE_H
#define OPENSTEP_MGA_MESA_TEXTURE_H

struct gl_texture_object;

/*
 * Put the three texture hooks in place, chaining to whatever was there.  Safe
 * to call again: it only replaces what is not already ours.
 */
void OSMGAMesaTexInstall(void *ctx);

/*
 * Is this texture in video memory, and where?  Zero means no, and no means
 * draw in software -- the copy is made here when it is first needed.
 */
int OSMGAMesaTexResident(void *ctx, struct gl_texture_object *tObj,
                         unsigned long *origin, unsigned long *w,
                         unsigned long *h, unsigned long *pitch);

/* The same for whatever 2D texture is bound in the current unit.
 * mipLevels asks for that many maps BELOW the base as one chain (0 is
 * the old single-level residency); mipOrg[4] receives their absolute
 * origins when a chain was asked for. */
int OSMGAMesaTexResidentCurrent(void *ctx, unsigned long mipLevels,
                                unsigned long *origin,
                                unsigned long *w, unsigned long *h,
                                unsigned long *pitch,
                                unsigned long *mipOrg);

unsigned long OSMGAMesaTexUploads(void);
unsigned long OSMGAMesaTexRefused(void);
unsigned long OSMGAMesaTexEvicted(void);

#endif /* OPENSTEP_MGA_MESA_TEXTURE_H */
