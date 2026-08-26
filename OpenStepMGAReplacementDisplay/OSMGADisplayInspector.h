/*
 * OSMGADisplayInspector.h - Configure.app inspector for the replacement
 * display driver.
 *
 * Configure.app is an old-AppKit (libNeXT) application; inspectors are
 * loaded out of the driver's .config bundle.  Display drivers get
 * IODisplayInspector, which owns the resolution/pixel-format picker; a
 * driver that wants extra controls subclasses it.  That is a shipped
 * pattern, not an invention: the stock MatroxMGA2064W display driver
 * ships MatroxInspector : IODisplayInspector the same way.
 *
 * The main nib is Configure.app's own DisplayInspector.nib with the
 * File's Owner class renamed and two switches grafted in
 * (nib-src/build-inspector-nib.py).  Display-family inspector nibs are
 * always called DisplayInspector.nib, and every driver that subclasses
 * ships its own copy; C1-0 confirmed on hardware that Configure loads
 * ours.
 *
 * All three controls are read once, when the driver initialises, so they
 * write the config table and nothing else -- there is no live path to push
 * them into the running driver, and the panel says so.
 * See docs/C1_CONFIGURE_INSPECTOR_PLAN.md.
 *
 * TWO STATUS ROWS, NOT ONE.  The driver's one-line answer is 430 px of
 * Helvetica 12 and the field is 340, so it cannot be shown; and dropping the
 * mode to make it fit would hide the one thing this panel cannot do.  It
 * reads the mode in -setTable: and nothing tells it when the stock resolution
 * picker changes -- that picker is not ours -- so the line can be stale.  A
 * line that NAMES the mode it is talking about is visibly stale; the same
 * line without the mode is silently wrong.  Hence a row for the mode and a
 * row for the verdict.
 *
 * Both come from OSMGAAccelVerdict, the same function that writes the
 * driver's log line, so the panel and the driver cannot come to disagree
 * about the arithmetic.  What the panel cannot know it does not claim: it
 * passes haveActual = 0 and every sentence it produces says "would".
 *
 * "Gray Levels" is a matrix rather than a switch because it has four values,
 * and it is a separate control rather than part of the mode list because it
 * is not part of the mode: 256, 16, 4 and 2 greys are the same 8bpp scanout
 * with the same IODisplayInfo and differ only in the DAC ramp.  It applies
 * only when the selected ColorSpace is BW:8.
 */
#ifndef OSMGA_DISPLAY_INSPECTOR_H
#define OSMGA_DISPLAY_INSPECTOR_H

#import <appkit/appkit.h>
#import <driverkit/IODisplayInspector.h>

@interface OSMGADisplayInspector : IODisplayInspector
{
    id stormSwitch;    /* "Storm 2D Test" */
    id mmapSwitch;     /* "VRAM Mmap" */
    id grayMatrix;     /* "Gray Levels": tags 0..3 = 256, 16, 4, 2 */
    id vramMatrix;     /* "MGA Memory Size": tags 0..1 = 16, 32 */
    id statusMode;     /* which mode the line below is talking about */
    id statusBrief;    /* what that mode WOULD get */
}

- setTable:(NXStringTable *)instance;
- toggleStorm:sender;
- toggleMmap:sender;
- grayChanged:sender;
- vramChanged:sender;

@end

#endif /* OSMGA_DISPLAY_INSPECTOR_H */
