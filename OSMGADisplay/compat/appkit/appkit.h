/*
 * compat/appkit/appkit.h - the little of the NEXTSTEP AppKit that a
 * Configure.app driver inspector needs.
 *
 * Copied from openstep-spacesaver2ps2, where it is proven against a
 * shipping inspector; kept identical apart from the category name so the
 * two can be diffed.
 *
 * Configure.app is built on the compatibility AppKit (libNeXT_s), and
 * <driverkit/IODeviceInspector.h> imports <appkit/appkit.h>; the OPENSTEP
 * 4.2 developer installation on the target has no such header tree.  This
 * declares only the types and selectors this bundle uses, with the
 * signatures the shipped PS2Mouse inspector was seen calling.  Symbols
 * resolve at load time inside Configure.app.
 */
#ifndef OSMGA_COMPAT_APPKIT_H
#define OSMGA_COMPAT_APPKIT_H

#import <objc/Object.h>
#import <sys/param.h>		/* MAXPATHLEN */

/* Classes we hold pointers to. Declared, not implemented: the class
 * symbols resolve inside Configure.app when the bundle is loaded, exactly
 * as for the shipped inspectors. */
@interface View : Object
@end

@interface NXBundle : Object
+ bundleForClass:(Class)aClass;
- (BOOL)getPath:(char *)path forResource:(const char *)name ofType:(const char *)ext;
@end

@interface NXStringTable : Object
- (const char *)valueForStringKey:(const char *)key;
- insertKey:(const char *)key value:(void *)value;
@end

extern id NXApp;

#define NX_ALERTDEFAULT		1
#define NX_ALERTALTERNATE	0
#define NX_ALERTOTHER		-1
extern int NXRunAlertPanel(const char *title, const char *msg,
			   const char *defaultButton, const char *alternateButton,
			   const char *otherButton, ...);
extern const char *NXLoadLocalizedStringFromTableInBundle(const char *tableName,
			   NXBundle *bundle, const char *key, const char *value);
extern char *NXCopyStringBuffer(const char *buf);
extern void NXLogError(const char *format, ...);

/* Selectors sent to objects the nib hands us (Box, Slider, TextField,
 * Button) and to NXApp. */
@interface Object (OSMGACompatAppKit)
- (int)intValue;
- setIntValue:(int)value;
- setTitle:(const char *)title;
- setStringValue:(const char *)value;
- loadNibFile:(const char *)path owner:owner withNames:(BOOL)flag;
/* Object(NXDelayedPerform): ms is milliseconds */
- perform:(SEL)aSelector with:anObject afterDelay:(int)ms cancelPrevious:(BOOL)flag;
/* Matrix / Cell */
- selectCellWithTag:(int)tag;
- selectedCell;
- (int)tag;
@end

#endif
