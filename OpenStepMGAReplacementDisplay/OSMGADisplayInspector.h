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
 * The two flags are read once, when the driver initialises, so the
 * switches write the config table and nothing else -- there is no live
 * path to push them into the running driver, and the panel says so.
 * See docs/C1_CONFIGURE_INSPECTOR_PLAN.md.
 */
#ifndef OSMGA_DISPLAY_INSPECTOR_H
#define OSMGA_DISPLAY_INSPECTOR_H

#import <appkit/appkit.h>
#import <driverkit/IODisplayInspector.h>

@interface OSMGADisplayInspector : IODisplayInspector
{
    id stormSwitch;    /* "Storm 2D Test" */
    id mmapSwitch;     /* "VRAM Mmap" */
}

- setTable:(NXStringTable *)instance;
- toggleStorm:sender;
- toggleMmap:sender;

@end

#endif /* OSMGA_DISPLAY_INSPECTOR_H */
