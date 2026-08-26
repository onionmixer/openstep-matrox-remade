/*
 * OpenStepMGAWindowMath -- the offscreen-window arithmetic, once.
 *
 * WHY THIS FILE EXISTS.  Two programs have to agree about the same numbers
 * and cannot talk to each other.  The kernel driver decides where the
 * offscreen VRAM window starts and ends and whether OpenGL can be
 * accelerated in the selected mode; the Configure.app inspector has to tell
 * an operator the same thing, and it is an application bundle that can only
 * read the instance table -- there is no path from the panel to the running
 * driver.  Computing it twice means two answers, and a panel that disagrees
 * with the driver is worse than no panel.
 *
 * So the arithmetic lives here, in C89 with no libc and no kernel headers,
 * and is compiled into BOTH the driver (reloc CFILES) and the inspector
 * bundle (bundle CFILES).  OpenStepMGAManualConfig.c is the precedent for
 * the shape, not for the wiring: that one is built by the reloc target only.
 *
 * PREDICTED IS NOT ACTUAL.  Every entry point that produces operator-facing
 * text takes what was ASKED FOR and what was ESTABLISHED as separate inputs.
 * The driver fills both.  The inspector can only fill the first, and its
 * sentences are therefore conditional -- "would give" -- including the 16 MB
 * ones, because the inspector knows neither the board's aperture nor the
 * result of any proof.
 *
 * THE PAGE SIZE IS AN ARGUMENT, NEVER AN ASSUMPTION.  Every offset here is
 * rounded to a page, and this machine's kernel page is 8192.  The inspector
 * runs in userland, where vm_page_size is not promised to be the kernel's,
 * so nothing in this file reads a global: the caller states it.  Three
 * arithmetic corrections in cross-review were wrong for exactly this reason.
 *
 * NOTHING HERE TOUCHES HARDWARE.  It validates operator input and does
 * arithmetic.  It does not identify a board, map memory, prove that memory
 * exists, or authorize anything.
 */

#ifndef OPENSTEP_MGA_WINDOW_MATH_H
#define OPENSTEP_MGA_WINDOW_MATH_H

/*
 * The identity subset of the driver's mode tables.
 *
 * The driver's own osmgaRes/osmgaFmt carry pixel clocks, sync polarities,
 * RAMDAC multiplexer settings and pixel encodings, none of which this file
 * needs.  What both programs need is which modes exist, how big they are,
 * and how many bytes a pixel costs -- so that is what is here, in the SAME
 * ORDER as the driver's tables, and OSMGAWindowMathTablesAgree() lets the
 * driver check at init that the two have not drifted apart.
 */
typedef struct {
    const char *name;
    unsigned long width;
    unsigned long height;
} OSMGAWinRes;

typedef struct {
    const char *cspace;
    unsigned long bytesPerPixel;
} OSMGAWinFmt;

#define OSMGA_WIN_RES_COUNT     5U
#define OSMGA_WIN_FMT_COUNT     4U
#define OSMGA_WIN_RES_DEFAULT   2U      /* 1024x768 */
#define OSMGA_WIN_FMT_DEFAULT   0U      /* RGB:888/32 */

const OSMGAWinRes *OSMGAWinResAt(unsigned int index);
const OSMGAWinFmt *OSMGAWinFmtAt(unsigned int index);

/*
 * Geometry constants shared with the driver.  GUARD_ROWS and TOP_MARGIN are
 * the driver's existing choices, restated here rather than re-decided: the
 * guard is the 256 rows kept below the visible image, and the margin is the
 * four megabytes left at the top of VRAM because that is where a board
 * reserves things and nobody has established what.
 */
#define OSMGA_WIN_GUARD_ROWS    256UL
#define OSMGA_WIN_TOP_MARGIN    (4UL * 1024UL * 1024UL)

/*
 * The smallest surface worth calling the hardware ready for.  Mesa's back
 * end already treats 320x240 as its small case; a window that cannot hold
 * colour plus the reserved depth for one is a window with nothing in it.
 */
#define OSMGA_WIN_MIN_WIDTH     320UL
#define OSMGA_WIN_MIN_HEIGHT    240UL

/* ---------------------------------------------------------------- input */

typedef enum {
    OSMGA_DECL_OK = 0,
    OSMGA_DECL_MISSING,
    OSMGA_DECL_INVALID,
    OSMGA_DECL_UNSUPPORTED
} OSMGADeclStatus;

/*
 * The VRAM declaration, which is the existing "MGA Memory Size" key.
 *
 * Only 16 and 32 are accepted.  The key's older parser accepts 3..63 because
 * the original driver did, and that parser is untouched; this is a NARROWER
 * rule layered on top, because 16 and 32 are the only two sizes this board
 * was sold in and the only two the window arithmetic has been reasoned about
 * at.  A missing or unusable value is 16 -- the conservative one -- and the
 * status says which so a caller can say so out loud.
 */
int OSMGAVramDeclaration(const char *value, unsigned long *bytes,
                         OSMGADeclStatus *status);
const char *OSMGADeclStatusString(OSMGADeclStatus status);

/*
 * "Yes" the way the driver reads it, so a switch cannot mean one thing to
 * the panel and another to the driver.  Absent is off.
 */
int OSMGAFeatureIsOn(const char *value);

typedef struct {
    unsigned int resIndex;
    unsigned int fmtIndex;
    unsigned long width;
    unsigned long height;
    unsigned long bytesPerPixel;
    unsigned int impliedGrayLevels;  /* 4 if the old "BW:4" spelling was used */
    int usedDefaultRes;
    int usedDefaultFmt;
} OSMGAModeSelection;

/*
 * Resolve a "Display Mode" value exactly as -selectModeFromConfig: does:
 * the OPENSTEP "Height:H Width:W Refresh:NHz ColorSpace:C" form, the first
 * ColorSpace token that appears anywhere in the string, the legacy "BW:4"
 * spelling meaning BW:8 with four greys, and the driver's defaults when
 * anything is missing.  Never fails; an unparsable value yields the default
 * mode with both usedDefault flags set.
 */
void OSMGASelectMode(const char *displayMode, OSMGAModeSelection *out);

/* -------------------------------------------------------------- window */

typedef struct {
    unsigned long visibleEnd;
    unsigned long start;
    unsigned long end;
    unsigned long bytes;
    int usable;
    const char *reason;      /* why not, when usable is 0 */
} OSMGAWindowGeom;

/*
 * The ceiling the window may be widened to: the declaration and the aperture,
 * whichever is smaller, less the top-of-VRAM margin, rounded down to a page.
 * Zero if the inputs leave nothing.
 */
unsigned long OSMGAWindowCeiling(unsigned long declaredBytes,
                                 unsigned long apertureBytes,
                                 unsigned long pageBytes);

/*
 * Where the offscreen window sits for a mode: after the visible image plus
 * the guard rows, rounded up to a page, up to the ceiling.  This is the same
 * expression the driver registers the character device from.
 */
void OSMGAWindowGeometry(const OSMGAModeSelection *mode,
                         unsigned long pageBytes,
                         unsigned long ceilingBytes,
                         OSMGAWindowGeom *out);

/* ------------------------------------------------------------- surface */

typedef struct {
    unsigned long stridePixels;
    unsigned long colourEnd;
    unsigned long depthStart;
    unsigned long depthEnd;
    unsigned long texStart;
    unsigned long arenaBytes;
    int fits;
} OSMGASurfaceLayout;

/*
 * Does a w by h 32-bit surface fit a window of availBytes?
 *
 * This mirrors what the Mesa back end actually lays out: the caller's row
 * length rounded up to 32 pixels, colour, then a page-aligned depth extent
 * RESERVED whether or not depth is ever asked for, then whatever is left as
 * the texture arena.  strideCapPixels is the display's own stride, which the
 * capability parameter publishes and the allocator refuses to exceed -- so a
 * surface can never be wider than the screen, and this says so.
 *
 * It answers about the LAYOUT only.  Whether Mesa will accept a particular
 * surface also depends on colour order, the pitch register and its own
 * single-surface rule, none of which are here.
 */
int OSMGASurfaceFits(unsigned long width, unsigned long height,
                     unsigned long strideCapPixels, unsigned long pageBytes,
                     unsigned long availBytes, OSMGASurfaceLayout *out);

/* ------------------------------------------------------------- verdict */

typedef struct {
    const char *displayMode;       /* the raw config value; may be null */
    const char *memorySizeValue;   /* the raw "MGA Memory Size" value */
    const char *mmapValue;         /* the raw "VRAM Mmap" value */
    const char *mesaValue;         /* the raw "Mesa Acceleration" value */
    unsigned long pageBytes;

    /*
     * Everything below is what the driver ESTABLISHED.  The inspector leaves
     * haveActual zero and gets conditional text; the driver sets it and gets
     * text in the present tense.
     */
    int haveActual;
    unsigned long apertureBytes;
    unsigned long windowStart;
    unsigned long windowEnd;
    int hasWindow;
    int hasCommandWindow;
} OSMGAVerdictIn;

/*
 * The kernel's page size, named rather than assumed.
 *
 * Every offset here is rounded to a page and this machine's kernel page is
 * 8192.  The driver passes its own PAGE_SIZE and never uses this; the
 * Configure.app panel has no PAGE_SIZE to pass -- it runs in userland, where
 * vm_page_size is not promised to be the kernel's -- so it passes this, and
 * the two agreeing is then something a reader can check rather than assume.
 */
#define OSMGA_WIN_TARGET_PAGE   8192UL

#define OSMGA_VERDICT_TEXT_MAX  192U

/*
 * The same answer, short enough for the panel.
 *
 * Measured, not guessed: the driver's one-line sentence is 430 px of Adobe
 * Helvetica at 12 pt and the field is 340, so the panel cannot show it.  The
 * brief drops the mode prefix -- the panel puts the mode on a row of its own,
 * which is what makes a stale line visibly stale -- and keeps the word
 * "would", because that is the word that makes a forecast honest.
 *
 * 48 bytes is 47 visible characters and a terminator.  The real constraint is
 * the pixel width, and the host suite asserts the exact string for every path
 * rather than only its length.
 */
#define OSMGA_VERDICT_BRIEF_MAX 48U

typedef struct {
    OSMGAModeSelection mode;
    unsigned long declaredBytes;
    OSMGADeclStatus declStatus;
    int mmapOn;
    int mesaOn;

    /*
     * ready is the CAP_READY-shaped answer: this mode can be accelerated at
     * all.  It deliberately does NOT require a full-screen surface -- an
     * 800x600 window on a 1600x1200 screen is real work that fits where a
     * full-screen pair does not.  fullScreen is reported beside it because
     * that is what the declaration buys, and it never gates ready.
     */
    int ready;
    int fullScreen;
    unsigned long windowBytes;
    unsigned long arenaBytes;      /* with a full-screen surface bound */
    unsigned long largestWidth;
    unsigned long largestHeight;

    char text[OSMGA_VERDICT_TEXT_MAX];
    char brief[OSMGA_VERDICT_BRIEF_MAX];
} OSMGAVerdictOut;

void OSMGAAccelVerdict(const OSMGAVerdictIn *in, OSMGAVerdictOut *out);

/*
 * The CAP_READY predicate, as a pure function of the driver's state.
 *
 * It lives here rather than in the driver so that it can be enumerated
 * exhaustively on a host: a capability bit that is wrong is a bit no one
 * looks at again, and "the demos still worked" exercises one combination out
 * of many.  The driver reads its own flags and calls this; nothing else
 * decides what READY means.
 *
 * tablesAgree is the drift alarm from OSMGAWindowMathTablesAgree().  It is an
 * input rather than an assertion because the honest answer to "this file and
 * the driver describe different hardware" is to refuse acceleration, not to
 * accelerate on arithmetic nobody can vouch for.
 */
typedef struct {
    int mmioMapped;
    int linearModeActive;
    int windowRegistered;
    int tablesAgree;
    unsigned long bytesPerPixel;
    unsigned long strideCapPixels;   /* rowBytes / 4, what CAP_STRIDE holds */
    unsigned long windowStart;
    unsigned long windowEnd;         /* exclusive */
    unsigned long pageBytes;
} OSMGAReadyIn;

int OSMGAAccelReadyBits(const OSMGAReadyIn *in);

/* ------------------------------------------------ the attempt limit */

/*
 * How far the driver may ATTEMPT to prove, which is not a measurement of
 * anything.
 *
 * Three different things get confused here if they are not named apart:
 *
 *   the GATE       -- the PCI survey.  It establishes that no other function
 *                     claims the range and that our own bridges forward it.
 *                     It does NOT establish that the card decodes it: a
 *                     16 MiB board behind a 32 MiB bridge window surveys
 *                     perfectly clear.  So it may permit an attempt and must
 *                     never assert a capacity.
 *   the DECLARATION -- what the operator asked for.  Also not a measurement.
 *   the PROOF      -- the only thing that answers, and only for the pages it
 *                     writes, at this boot, at two words a page.
 *
 * An earlier design composed the first two with a minimum and called the
 * result "the surveyed aperture", which reads as though something had been
 * measured.  Cross-review was right to refuse it.
 */
unsigned long OSMGAAttemptLimit(unsigned long declaredBytes, int gateAllows,
                                unsigned long conservativeBytes,
                                unsigned long gatedBytes);

/* -------------------------------------------------- window lifecycle */

/*
 * The offscreen window is published EMPTY and opened once, after everything
 * that is going to write into video memory has finished writing.
 *
 * WHY IT CANNOT BE OPENED AT REGISTRATION.  The window's far end depends on a
 * proof that can only run at one moment -- the mode is programmed and the
 * screen is still blanked -- which is long after the character device has to
 * exist.  Registering with the conservative end and widening afterwards is
 * what the driver used to do, and it has two faults.  The mode whose visible
 * image already reaches past that conservative end never gets registered at
 * all, so the widening that would have made its window real never runs
 * (1600x1200x32, measured).  And the proof writes witnesses BELOW the region
 * it is testing, inside a window a client may already hold -- twelve of them
 * at 1024x768 -- restoring saved words over whatever the client put there.
 *
 * So: register empty, refuse `open` until the state is OPEN, and make the
 * transition once.  While anything is being proved there is no file
 * descriptor, so neither the video-memory window nor the command batch can be
 * mapped -- the batch matters, because the mmap handler tries it FIRST and it
 * does not depend on the video-memory interval at all.
 *
 * FAILED IS NOT UNOPENED.  A proof that fails at 1600x1200 leaves the end
 * equal to the start, which is indistinguishable from "not attempted" if the
 * endpoints are all there is.  The state is explicit so that a failure is
 * never retried and never mistaken for a fresh start.
 */
typedef enum {
    OSMGA_WINDOW_UNOPENED = 0,
    OSMGA_WINDOW_OPEN,
    OSMGA_WINDOW_FAILED
} OSMGAWindowState;

/*
 * What the window becomes once the proving is over.  `provenEnd` is the bound
 * justified without a proof (the working scanout); `ceilingEnd` is the bound
 * the proof was for.  A result that leaves nothing is FAILED, not an empty
 * OPEN.
 */
void OSMGAWindowOpenDecision(unsigned long start, int proofPassed,
                             unsigned long provenEnd, unsigned long ceilingEnd,
                             unsigned long pageBytes,
                             unsigned long *outEnd, OSMGAWindowState *outState);

/*
 * Whether the device may be registered for this mode at all: the window has
 * to be able to become non-empty later, and its far end must stay inside the
 * aperture the driver actually mapped.
 */
int OSMGAWindowMayRegister(unsigned long start, unsigned long ceilingEnd,
                           unsigned long apertureBytes,
                           unsigned long pageBytes);

/* ------------------------------------------------------- PCI survey */

/*
 * Judging what the PCI bus says about the aperture we intend to use.
 *
 * The reading of configuration space has to happen in the driver; the
 * DECIDING does not, and deciding is where the mistakes are. So the driver
 * walks the bus and reports what it finds, one claim at a time, and this
 * accumulates a verdict that a host test can enumerate.
 *
 * WHY A BASE ALONE IS ENOUGH FOR A DEVICE.  A neighbour's size cannot be read
 * without writing to its base register, which this design refuses to do. It
 * does not need to be:
 *
 *   - a neighbour based INSIDE our range is a collision whatever its size;
 *   - a neighbour based BELOW our range cannot reach into it, because to do
 *     so it would have to cover fbPhysical itself, and this driver's proof
 *     establishes [fbPhysical, fbPhysical + 12 MiB) as ours at every boot --
 *     two enabled memory decoders may not overlap;
 *   - a neighbour based ABOVE our range is not in it.
 *
 * That covers every device base without inferring an extent. The earlier
 * design bounded extents by `size <= lowbit(base)`, which is true (proved
 * over every legal pair in scratchpad/r10lowbit.py) but so loose that an
 * ordinary neighbour at 0xf0000000 would refuse a 32 MB aperture on account
 * of a device that may be four kilobytes. The bound is kept only as a
 * diagnostic in the log.
 *
 * BRIDGES ARE DIFFERENT and are not judged by their base at all: a
 * forwarding window states its own limit, so the extent is read rather than
 * inferred. A bridge that is our ANCESTOR is not a neighbour -- forwarding
 * our aperture is its job -- but its window ending early is a disproof: it
 * cannot forward what it does not claim.
 *
 * Fail-closed: the FIRST problem is kept and later ones do not overwrite it,
 * so the walk can continue and log everything without losing the reason.
 */
typedef enum {
    OSMGA_SURVEY_CLEAR = 0,
    OSMGA_SURVEY_BAR_INSIDE,        /* a foreign base sits in our range */
    OSMGA_SURVEY_WINDOW_OVERLAPS,   /* a foreign bridge forwards part of it */
    OSMGA_SURVEY_PARENT_TOO_SMALL,  /* our own bridge does not reach that far */
    OSMGA_SURVEY_UNKNOWN_HEADER,    /* a layout we may not reason about */
    OSMGA_SURVEY_MALFORMED          /* a BAR pair or type we cannot read */
} OSMGASurveyVerdict;

typedef struct {
    unsigned long fbPhysical;
    unsigned long wantBytes;
    OSMGASurveyVerdict verdict;
    unsigned long offender;         /* the address that decided it */
    unsigned long claimsSeen;
} OSMGASurveyState;

void OSMGASurveyBegin(OSMGASurveyState *st, unsigned long fbPhysical,
                      unsigned long wantBytes);
/* A device (or enabled expansion ROM) base address. */
void OSMGASurveyClaimBase(OSMGASurveyState *st, unsigned long base);
/* A bridge forwarding window, [base, end) exclusive, base >= end = disabled. */
void OSMGASurveyClaimWindow(OSMGASurveyState *st, unsigned long base,
                            unsigned long end, int isAncestor);
void OSMGASurveyRefuse(OSMGASurveyState *st, OSMGASurveyVerdict why,
                       unsigned long offender);
int OSMGASurveyIsClear(const OSMGASurveyState *st);
const char *OSMGASurveyVerdictString(OSMGASurveyVerdict verdict);

/* Diagnostic only: the largest size a naturally aligned BAR at this base can
 * have.  Never used to decide anything -- see the note above. */
unsigned long OSMGASurveyAlignmentBound(unsigned long base);

/*
 * Drift alarm.  The driver calls this once at init with its own tables and
 * logs loudly if this file's identity subset no longer matches them.  Two
 * tables that disagree are exactly the failure this file exists to prevent,
 * so it is detected rather than assumed away.  Returns 1 when they agree.
 */
int OSMGAWindowMathTablesAgree(const char *const *resNames,
                               const unsigned long *resWidths,
                               const unsigned long *resHeights,
                               unsigned int resCount,
                               const char *const *fmtNames,
                               const unsigned long *fmtBytesPerPixel,
                               unsigned int fmtCount);

#endif /* OPENSTEP_MGA_WINDOW_MATH_H */
