/*
 * OSMGADisplayInspector.m - see OSMGADisplayInspector.h.
 */
#import "OSMGADisplayInspector.h"

#define KEY_STORM   "Storm 2D Test"
#define KEY_MMAP    "VRAM Mmap"

/*
 * The driver decides with osmgaTextContains(value, "Yes") -- a
 * case-sensitive substring test, not an exact compare -- and treats a
 * missing key as off (OpenStepMGAReplacementDisplay.m, "Storm 2D Test"
 * and "VRAM Mmap" reads).  So read the same way the driver does rather
 * than inventing a stricter rule here: whatever the panel shows must be
 * what the driver will do at the next boot.
 */
static BOOL
osmgaFlagIsOn(const char *value)
{
    const char *h;
    const char *n;
    const char *hp;

    if (value == 0)
	return NO;
    for (h = value; *h != '\0'; h++) {
	hp = h;
	n = "Yes";
	while (*n != '\0' && *hp == *n) {
	    hp++;
	    n++;
	}
	if (*n == '\0')
	    return YES;
    }
    return NO;
}

@implementation OSMGADisplayInspector

- setTable:(NXStringTable *)instance
{
    [super setTable:instance];

    [stormSwitch setIntValue:osmgaFlagIsOn([table valueForStringKey:KEY_STORM])];
    [mmapSwitch setIntValue:osmgaFlagIsOn([table valueForStringKey:KEY_MMAP])];
    return self;
}

/*
 * NXStringTable inherits insertKey:value: from HashTable, whose header
 * does not say whether the value is copied or kept as a pointer.  Every
 * shipped inspector hands it an NXCopyStringBuffer and never frees it, so
 * do exactly that: a stack buffer would dangle if the table keeps the
 * pointer, and freeing afterwards would too.
 */
- (void)storeFlag:(const char *)key on:(BOOL)on
{
    [table insertKey:key value:NXCopyStringBuffer(on ? "Yes" : "No")];
}

- toggleStorm:sender
{
    [self storeFlag:KEY_STORM on:[sender intValue] ? YES : NO];
    return self;
}

- toggleMmap:sender
{
    [self storeFlag:KEY_MMAP on:[sender intValue] ? YES : NO];
    return self;
}

@end
