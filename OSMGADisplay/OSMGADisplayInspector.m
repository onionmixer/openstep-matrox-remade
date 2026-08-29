/*
 * OSMGADisplayInspector.m - see OSMGADisplayInspector.h.
 */
#import "OSMGADisplayInspector.h"
#import "OpenStepMGAWindowMath.h"

#define KEY_STORM   "Storm 2D Test"
#define KEY_MMAP    "VRAM Mmap"
#define KEY_GRAY    "Gray Levels"
#define KEY_VRAMSIZE "MGA Memory Size"
#define KEY_MESA    "Mesa Acceleration"
#define KEY_WARP    "WARP 3D"

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

/*
 * The four values the driver accepts, in matrix-tag order.  Kept as strings
 * because that is what goes in the table and what comes back out; a numeric
 * round trip would only add a place for the two to disagree.
 */
static const char * const osmgaGrayValues[] = { "256", "16", "4", "2" };
#define OSMGA_GRAY_COUNT  4

static BOOL
osmgaStringsEqual(const char *a, const char *b)
{
    if (a == 0 || b == 0)
	return NO;
    while (*a != '\0' && *a == *b) {
	a++;
	b++;
    }
    return (*a == '\0' && *b == '\0');
}

/*
 * Which cell to select for what the table holds.
 *
 * Unrecognised or absent means 256, because that is what the driver does
 * with it -- read -selectModeFromConfig: rather than inventing a rule here.
 * "BW:4" in the mode string is the older spelling of BW:8 with four greys,
 * and the driver still honours it, so the panel has to show four greys or it
 * would be lying about the next boot.
 */
static int
osmgaGrayTagFor(const char *value, const char *mode)
{
    int i;

    if (value != 0) {
	for (i = 0; i < OSMGA_GRAY_COUNT; i++)
	    if (osmgaStringsEqual(value, osmgaGrayValues[i]))
		return i;
	return 0;
    }
    if (mode != 0) {
	const char *h;
	for (h = mode; *h != '\0'; h++)
	    if (h[0] == 'B' && h[1] == 'W' && h[2] == ':' && h[3] == '4')
		return 2;              /* the old spelling: four greys */
    }
    return 0;
}

/*
 * Which radio cell a stored declaration selects.
 *
 * Anything this driver does not support -- 8, 63, a typo -- selects 16,
 * because 16 is what the driver will actually use for it.  It deliberately
 * does NOT try to clear the selection: nothing establishes what
 * selectCellWithTag: does with a tag no cell has, and a matrix left in an
 * undefined state is a worse answer than the true one.
 */
/*
 * Cell tags, in the order the matrix lays them out: 0 = 8, 1 = 16, 2 = 32.
 *
 * Anything the driver does not support -- 12, 63, a typo -- selects 16,
 * because 16 is what the driver will actually use for it.  Note the
 * direction: 8 is SMALLER than that fallback, so an unusable value must
 * never land on it.  Only an operator asking for 8 exactly gets 8.
 *
 * It deliberately does not try to clear the selection: nothing establishes
 * what selectCellWithTag: does with a tag no cell has, and a matrix left in
 * an undefined state is a worse answer than the true one.
 */
static const char *const osmgaVramValues[] = { "8", "16", "32" };
#define OSMGA_VRAM_COUNT ((int)(sizeof(osmgaVramValues) / \
                                sizeof(osmgaVramValues[0])))

static int
osmgaVramTagFor(const char *value)
{
    unsigned long bytes = 0UL;
    OSMGADeclStatus status = OSMGA_DECL_MISSING;

    if (!OSMGAVramDeclaration(value, &bytes, &status))
	return 1;                                  /* falls back to 16 */
    if (bytes == 8UL * 1024UL * 1024UL)
	return 0;
    if (bytes == 32UL * 1024UL * 1024UL)
	return 2;
    return 1;
}

@implementation OSMGADisplayInspector

/*
 * Recomputed here and after every control, so the panel answers for what the
 * table says NOW rather than for what it said when it was opened.
 *
 * haveActual stays zero: this is Configure.app, it cannot ask the driver
 * anything, and it knows neither the board's aperture nor the result of any
 * boot proof.  Every sentence it produces therefore says "would".
 *
 * The page size is passed explicitly, from the shared header, because this
 * runs in userland where vm_page_size is not promised to be the kernel's --
 * and every offset in the arithmetic is rounded to a page.
 */
- (void)refreshStatus
{
    OSMGAVerdictIn in;
    OSMGAVerdictOut out;

    in.displayMode     = (const char *)[table valueForStringKey:"Display Mode"];
    in.memorySizeValue = (const char *)[table valueForStringKey:KEY_VRAMSIZE];
    in.mmapValue       = (const char *)[table valueForStringKey:KEY_MMAP];
    in.mesaValue       = (const char *)[table valueForStringKey:KEY_MESA];
    in.pageBytes       = OSMGA_WIN_TARGET_PAGE;
    in.haveActual      = 0;
    in.apertureBytes   = 0UL;
    in.windowStart     = 0UL;
    in.windowEnd       = 0UL;
    in.hasWindow       = 0;
    in.hasCommandWindow = 0;
    OSMGAAccelVerdict(&in, &out);

    [statusMode setStringValue:
	OSMGAWinResAt(out.mode.resIndex)->name];
    [statusBrief setStringValue:out.brief];
}

- setTable:(NXStringTable *)instance
{
    [super setTable:instance];

    [stormSwitch setIntValue:osmgaFlagIsOn([table valueForStringKey:KEY_STORM])];
    [mmapSwitch setIntValue:osmgaFlagIsOn([table valueForStringKey:KEY_MMAP])];
    [warpSwitch setIntValue:osmgaFlagIsOn([table valueForStringKey:KEY_WARP])];
    [grayMatrix selectCellWithTag:
	osmgaGrayTagFor([table valueForStringKey:KEY_GRAY],
			[table valueForStringKey:"Display Mode"])];
    [vramMatrix selectCellWithTag:
	osmgaVramTagFor([table valueForStringKey:KEY_VRAMSIZE])];
    [self refreshStatus];
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
    [self refreshStatus];
    return self;
}

- toggleMmap:sender
{
    [self storeFlag:KEY_MMAP on:[sender intValue] ? YES : NO];
    [self refreshStatus];
    return self;
}

/*
 * Which tier draws, and it is the only switch here that changes a PICTURE
 * rather than whether a facility exists.
 *
 * WARP is faster -- measured on this hardware, 53.7 fps against 40.6 for a
 * spinning teapot -- and on ordinary geometry the two tiers agree.  On
 * near-degenerate slivers WARP diverges further from software than the
 * trapezoid tier does, and no quantity was found that tells the shapes it
 * gets wrong from the ones it gets right, so this cannot be decided for the
 * operator.  Hence a switch, defaulting off, with the caveat in the label.
 *
 * It does not turn 3D on by itself: "Mesa Acceleration" and the VRAM window
 * still have to be there, and when they are not this preference is carried
 * to a library that is not accelerating anything.
 */
- toggleWarp:sender
{
    [self storeFlag:KEY_WARP on:[sender intValue] ? YES : NO];
    [self refreshStatus];
    return self;
}

- grayChanged:sender
{
    int tag = [[sender selectedCell] tag];

    if (tag < 0 || tag >= OSMGA_GRAY_COUNT)
	tag = 0;
    [table insertKey:KEY_GRAY
	      value:NXCopyStringBuffer(osmgaGrayValues[tag])];
    [self refreshStatus];
    return self;
}

/*
 * The declaration.  Stored as asked, because that is what a declaration is:
 * the panel cannot know what the board has, the driver treats the survey as
 * a gate and the boot proof as the answer, and a 32 that turns out to be
 * wrong is attempted and then narrowed by the proof -- not acted on blindly.
 * Saying "would" is the honest half of that; refusing to store the operator's
 * choice would not be.
 */
- vramChanged:sender
{
    int tag = [[sender selectedCell] tag];

    if (tag < 0 || tag >= OSMGA_VRAM_COUNT)
	tag = 1;                                   /* 16, the safe one */
    [table insertKey:KEY_VRAMSIZE
	      value:NXCopyStringBuffer(osmgaVramValues[tag])];
    [self refreshStatus];
    return self;
}

@end
