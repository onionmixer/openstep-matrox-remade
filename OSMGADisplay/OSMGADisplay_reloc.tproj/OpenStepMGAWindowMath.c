/* See OpenStepMGAWindowMath.h.  C89, no libc, no kernel headers. */

#include "OpenStepMGAWindowMath.h"
#include "OpenStepMGAEDID.h"
#include "OpenStepMGAManualConfig.h"

/*
 * The identity subset, in the driver's table order.  Kept beside the driver's
 * own tables rather than derived from them because the inspector cannot see
 * those; OSMGAWindowMathTablesAgree() is how the two are held together.
 */
static const OSMGAWinRes osmgaWinRes[OSMGA_WIN_RES_COUNT] = {
    { "640x480",   640UL,  480UL },
    { "800x600",   800UL,  600UL },
    { "1024x768", 1024UL,  768UL },
    { "1280x1024",1280UL, 1024UL },
    { "1600x1200",1600UL, 1200UL }
};

static const OSMGAWinFmt osmgaWinFmt[OSMGA_WIN_FMT_COUNT] = {
    { "RGB:888/32", 4UL },
    { "RGB:555/16", 2UL },
    { "RGB:256/8",  1UL },
    { "BW:8",       1UL }
};

const OSMGAWinRes *
OSMGAWinResAt(unsigned int index)
{
    if (index >= OSMGA_WIN_RES_COUNT)
        return 0;
    return &osmgaWinRes[index];
}

const OSMGAWinFmt *
OSMGAWinFmtAt(unsigned int index)
{
    if (index >= OSMGA_WIN_FMT_COUNT)
        return 0;
    return &osmgaWinFmt[index];
}

/* ------------------------------------------------------------ text bits */

static int
osmgaWinEquals(const char *a, const char *b)
{
    if (a == 0 || b == 0)
        return 0;
    while (*a != '\0' && *b != '\0') {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int
osmgaWinContains(const char *haystack, const char *needle)
{
    const char *h;
    const char *n;
    const char *p;

    if (haystack == 0 || needle == 0 || *needle == '\0')
        return 0;
    for (h = haystack; *h != '\0'; h++) {
        p = h;
        n = needle;
        while (*n != '\0' && *p == *n) {
            p++;
            n++;
        }
        if (*n == '\0')
            return 1;
    }
    return 0;
}

typedef struct {
    char *buf;
    unsigned int cap;
    unsigned int pos;
} OSMGAWinText;

static void
osmgaWinTextInit(OSMGAWinText *t, char *buf, unsigned int cap)
{
    t->buf = buf;
    t->cap = cap;
    t->pos = 0U;
    if (cap > 0U)
        buf[0] = '\0';
}

/*
 * Truncating, never overrunning, and the terminator is written every time --
 * a caller that ignores the length still gets a valid string.
 */
static void
osmgaWinPut(OSMGAWinText *t, const char *s)
{
    if (t->cap == 0U || s == 0)
        return;
    while (*s != '\0' && t->pos + 1U < t->cap) {
        t->buf[t->pos] = *s;
        t->pos++;
        s++;
    }
    t->buf[t->pos] = '\0';
}

static void
osmgaWinPutULong(OSMGAWinText *t, unsigned long v)
{
    char tmp[24];
    int i = 0;

    if (v == 0UL) {
        osmgaWinPut(t, "0");
        return;
    }
    while (v != 0UL && i < (int)sizeof(tmp)) {
        tmp[i] = (char)('0' + (int)(v % 10UL));
        v /= 10UL;
        i++;
    }
    while (i > 0) {
        char one[2];

        i--;
        one[0] = tmp[i];
        one[1] = '\0';
        osmgaWinPut(t, one);
    }
}

/* Megabytes with one decimal, rounded to nearest, without floating point. */
static void
osmgaWinPutMB(OSMGAWinText *t, unsigned long bytes)
{
    unsigned long meg = 1024UL * 1024UL;
    unsigned long whole = bytes / meg;
    unsigned long rem = bytes % meg;
    unsigned long tenths = (rem * 10UL + meg / 2UL) / meg;

    if (tenths >= 10UL) {
        whole++;
        tenths = 0UL;
    }
    osmgaWinPutULong(t, whole);
    osmgaWinPut(t, ".");
    osmgaWinPutULong(t, tenths);
    osmgaWinPut(t, " MB");
}

/* ----------------------------------------------------------- declaration */

int
OSMGAVramDeclaration(const char *value, unsigned long *bytes,
                     OSMGADeclStatus *status)
{
    unsigned int parsed = 0U;
    OSMGAManualMemoryStatus manual = OSMGA_MANUAL_MEMORY_MISSING;

    if (bytes == 0 || status == 0)
        return 0;

    /*
     * The conservative answer is written FIRST and unconditionally, so every
     * refusal below leaves a caller with 16 MB rather than with zero.  A zero
     * aperture would not be a safe default; it would be a different bug.
     */
    *bytes = 16UL * 1024UL * 1024UL;

    if (!OSMGAParseManualMemoryMB(value, &parsed, &manual)) {
        switch (manual) {
        case OSMGA_MANUAL_MEMORY_MISSING:
            *status = OSMGA_DECL_MISSING;
            break;
        case OSMGA_MANUAL_MEMORY_INVALID:
            *status = OSMGA_DECL_INVALID;
            break;
        default:
            *status = OSMGA_DECL_UNSUPPORTED;
            break;
        }
        return 0;
    }

    /*
     * Eight is accepted only when it was asked for EXACTLY.  It is smaller
     * than the fallback, so a missing key, a malformed value or an
     * unsupported number must never land on it -- they land on sixteen, as
     * they always have.  Only an operator typing 8 gets 8.
     */
    if (parsed == 8U * 1024U * 1024U) {
        *bytes = 8UL * 1024UL * 1024UL;
        *status = OSMGA_DECL_OK;
        return 1;
    }
    if (parsed == 16U * 1024U * 1024U) {
        *status = OSMGA_DECL_OK;
        return 1;
    }
    if (parsed == 32U * 1024U * 1024U) {
        *bytes = 32UL * 1024UL * 1024UL;
        *status = OSMGA_DECL_OK;
        return 1;
    }

    /*
     * A number the old parser accepts and this one does not -- 8, 12, 63.
     * Clamped DOWN to 16 rather than up: the declaration is a promise about
     * memory that may not be there, and the only safe direction to be wrong
     * in is the small one.
     */
    *status = OSMGA_DECL_UNSUPPORTED;
    return 0;
}

const char *
OSMGADeclStatusString(OSMGADeclStatus status)
{
    switch (status) {
    case OSMGA_DECL_OK:
        return "ok";
    case OSMGA_DECL_MISSING:
        return "missing";
    case OSMGA_DECL_INVALID:
        return "invalid";
    case OSMGA_DECL_UNSUPPORTED:
        return "unsupported";
    default:
        return "unknown";
    }
}

int
OSMGAFeatureIsOn(const char *value)
{
    return osmgaWinContains(value, "Yes") ? 1 : 0;
}

/* --------------------------------------------------------------- mode */

static int
osmgaWinPitchIsExact(const OSMGAWinRes *r, const OSMGAWinFmt *f)
{
    return ((r->width * f->bytesPerPixel) % 16UL) == 0UL;
}

void
OSMGASelectMode(const char *displayMode, OSMGAModeSelection *out)
{
    OSMGAMode parsed;
    unsigned int i;

    if (out == 0)
        return;

    out->resIndex = OSMGA_WIN_RES_DEFAULT;
    out->fmtIndex = OSMGA_WIN_FMT_DEFAULT;
    out->impliedGrayLevels = 0U;
    out->usedDefaultRes = 1;
    out->usedDefaultFmt = 1;

    if (displayMode != 0) {
        if (OSMGAParseManualDisplayMode(displayMode, &parsed)) {
            for (i = 0U; i < OSMGA_WIN_RES_COUNT; i++)
                if (osmgaWinRes[i].width == (unsigned long)parsed.width &&
                    osmgaWinRes[i].height == (unsigned long)parsed.height) {
                    out->resIndex = i;
                    out->usedDefaultRes = 0;
                    break;
                }
        }
        /* First token that appears anywhere wins, in table order -- the
         * driver's rule, replicated rather than improved. */
        for (i = 0U; i < OSMGA_WIN_FMT_COUNT; i++)
            if (osmgaWinContains(displayMode, osmgaWinFmt[i].cspace)) {
                out->fmtIndex = i;
                out->usedDefaultFmt = 0;
                break;
            }
        /* "BW:4" is the old spelling of BW:8 with four greys. */
        if (osmgaWinContains(displayMode, "BW:4")) {
            for (i = 0U; i < OSMGA_WIN_FMT_COUNT; i++)
                if (osmgaWinEquals(osmgaWinFmt[i].cspace, "BW:8")) {
                    out->fmtIndex = i;
                    out->usedDefaultFmt = 0;
                    break;
                }
            out->impliedGrayLevels = 4U;
        }
    }

    /* A pair whose row the CRTC cannot describe exactly goes back to the
     * default pair whole, not rounded -- the driver refuses it the same way. */
    if (!osmgaWinPitchIsExact(&osmgaWinRes[out->resIndex],
                              &osmgaWinFmt[out->fmtIndex])) {
        out->resIndex = OSMGA_WIN_RES_DEFAULT;
        out->fmtIndex = OSMGA_WIN_FMT_DEFAULT;
        out->usedDefaultRes = 1;
        out->usedDefaultFmt = 1;
    }

    out->width = osmgaWinRes[out->resIndex].width;
    out->height = osmgaWinRes[out->resIndex].height;
    out->bytesPerPixel = osmgaWinFmt[out->fmtIndex].bytesPerPixel;
}

/* ------------------------------------------------------------- window */

static int
osmgaWinPageOk(unsigned long pageBytes)
{
    return pageBytes != 0UL && (pageBytes & (pageBytes - 1UL)) == 0UL;
}

unsigned long
OSMGAWindowCeiling(unsigned long declaredBytes, unsigned long apertureBytes,
                   unsigned long pageBytes)
{
    unsigned long smaller;

    if (!osmgaWinPageOk(pageBytes))
        return 0UL;
    smaller = (declaredBytes < apertureBytes) ? declaredBytes : apertureBytes;
    if (smaller <= OSMGA_WIN_TOP_MARGIN)
        return 0UL;
    return (smaller - OSMGA_WIN_TOP_MARGIN) & ~(pageBytes - 1UL);
}

void
OSMGAWindowGeometry(const OSMGAModeSelection *mode, unsigned long pageBytes,
                    unsigned long ceilingBytes, OSMGAWindowGeom *out)
{
    unsigned long visible;
    unsigned long guard;
    unsigned long start;

    if (out == 0)
        return;
    out->visibleEnd = 0UL;
    out->start = 0UL;
    out->end = 0UL;
    out->bytes = 0UL;
    out->usable = 0;
    out->reason = "no mode";
    if (mode == 0)
        return;

    if (!osmgaWinPageOk(pageBytes)) {
        out->reason = "page size is not a power of two";
        return;
    }

    /* No product is formed without proving it fits first: a check that
     * overflows is not a check, which is the rule this driver follows. */
    if (mode->height != 0UL &&
        mode->width > 0xFFFFFFFFUL / mode->height / mode->bytesPerPixel) {
        out->reason = "visible image does not fit an address";
        return;
    }
    visible = mode->width * mode->bytesPerPixel * mode->height;
    guard = OSMGA_WIN_GUARD_ROWS * mode->width * mode->bytesPerPixel;
    out->visibleEnd = visible;

    if (visible > 0xFFFFFFFFUL - guard) {
        out->reason = "guard rows do not fit an address";
        return;
    }
    start = visible + guard;
    if (start > 0xFFFFFFFFUL - (pageBytes - 1UL)) {
        out->reason = "window start does not fit an address";
        return;
    }
    start = (start + pageBytes - 1UL) & ~(pageBytes - 1UL);
    out->start = start;
    out->end = ceilingBytes;

    if (ceilingBytes <= start) {
        out->reason = "the visible image leaves no room below the ceiling";
        return;
    }
    if (ceilingBytes - start < pageBytes) {
        out->reason = "less than one page would be left";
        return;
    }
    out->bytes = ceilingBytes - start;
    out->usable = 1;
    out->reason = "";
}

/* ------------------------------------------------------------ surface */

#define OSMGA_WIN_PITCH_ALIGN   32UL

int
OSMGASurfaceFits(unsigned long width, unsigned long height,
                 unsigned long strideCapPixels, unsigned long pageBytes,
                 unsigned long availBytes, OSMGASurfaceLayout *out)
{
    OSMGASurfaceLayout tmp;

    if (out == 0)
        out = &tmp;
    out->stridePixels = 0UL;
    out->colourEnd = 0UL;
    out->depthStart = 0UL;
    out->depthEnd = 0UL;
    out->texStart = 0UL;
    out->arenaBytes = 0UL;
    out->fits = 0;

    if (width == 0UL || height == 0UL || !osmgaWinPageOk(pageBytes))
        return 0;

    /* Rounded up to whole 32-pixel rows, which is the pitch the engine can
     * walk, and bounded against the cap both before and after -- rounding can
     * cross a limit the unrounded value was inside. */
    if (width > strideCapPixels)
        return 0;
    if (width > 0xFFFFFFFFUL - (OSMGA_WIN_PITCH_ALIGN - 1UL))
        return 0;
    out->stridePixels = ((width + OSMGA_WIN_PITCH_ALIGN - 1UL) /
                         OSMGA_WIN_PITCH_ALIGN) * OSMGA_WIN_PITCH_ALIGN;
    if (out->stridePixels > strideCapPixels)
        return 0;
    /* The cap is the caller's; guard the products it feeds before forming
     * them, so a preposterous cap cannot make the checks below wrap. */
    if (out->stridePixels > 0xFFFFFFFFUL / 4UL)
        return 0;

    if (height > 0xFFFFFFFFUL / (out->stridePixels * 4UL))
        return 0;
    out->colourEnd = height * out->stridePixels * 4UL;

    if (out->colourEnd > 0xFFFFFFFFUL - (pageBytes - 1UL))
        return 0;
    out->depthStart = (out->colourEnd + pageBytes - 1UL) & ~(pageBytes - 1UL);

    if (height > (0xFFFFFFFFUL - out->depthStart) /
                 (out->stridePixels * 2UL))
        return 0;
    out->depthEnd = out->depthStart + height * out->stridePixels * 2UL;

    if (out->depthEnd > availBytes)
        return 0;

    if (out->depthEnd > 0xFFFFFFFFUL - (pageBytes - 1UL))
        return 0;
    out->texStart = (out->depthEnd + pageBytes - 1UL) & ~(pageBytes - 1UL);
    out->arenaBytes = (out->texStart < availBytes)
                    ? availBytes - out->texStart : 0UL;
    out->fits = 1;
    return 1;
}

/* ------------------------------------------------------------- verdict */

/*
 * Writes the same words into both cursors.  The two strings differ only in
 * what precedes them -- the full one names the mode, the brief does not -- so
 * everything after that point is emitted once, to both, and they cannot come
 * to disagree about the answer.
 */
static void
osmgaWinPut2(OSMGAWinText *a, OSMGAWinText *b, const char *s)
{
    osmgaWinPut(a, s);
    osmgaWinPut(b, s);
}

static void
osmgaWinPutMB2(OSMGAWinText *a, OSMGAWinText *b, unsigned long bytes)
{
    osmgaWinPutMB(a, bytes);
    osmgaWinPutMB(b, bytes);
}

static void
osmgaWinPutULong2(OSMGAWinText *a, OSMGAWinText *b, unsigned long v)
{
    osmgaWinPutULong(a, v);
    osmgaWinPutULong(b, v);
}

void
OSMGAAccelVerdict(const OSMGAVerdictIn *in, OSMGAVerdictOut *out)
{
    OSMGAWinText t;
    OSMGAWinText b;
    OSMGAWindowGeom geom;
    OSMGASurfaceLayout lay;
    unsigned long declared = 16UL * 1024UL * 1024UL;
    unsigned long ceiling;
    unsigned long avail = 0UL;
    unsigned long strideCap;
    unsigned int i;

    if (out == 0)
        return;
    out->declaredBytes = declared;
    out->declStatus = OSMGA_DECL_MISSING;
    out->mmapOn = 0;
    out->mesaOn = 0;
    out->ready = 0;
    out->fullScreen = 0;
    out->windowBytes = 0UL;
    out->arenaBytes = 0UL;
    out->largestWidth = 0UL;
    out->largestHeight = 0UL;
    osmgaWinTextInit(&t, out->text, OSMGA_VERDICT_TEXT_MAX);
    osmgaWinTextInit(&b, out->brief, OSMGA_VERDICT_BRIEF_MAX);
    OSMGASelectMode(0, &out->mode);
    if (in == 0) {
        osmgaWinPut2(&t, &b, "no configuration");
        return;
    }

    OSMGASelectMode(in->displayMode, &out->mode);
    (void)OSMGAVramDeclaration(in->memorySizeValue, &declared,
                               &out->declStatus);
    out->declaredBytes = declared;
    out->mmapOn = OSMGAFeatureIsOn(in->mmapValue);
    out->mesaOn = OSMGAFeatureIsOn(in->mesaValue);

    /* The mode names itself in the full sentence only; the panel shows it on
     * a row of its own, which is what makes a stale line visibly stale. */
    osmgaWinPut(&t, osmgaWinRes[out->mode.resIndex].name);
    osmgaWinPut(&t, " ");
    osmgaWinPut(&t, osmgaWinFmt[out->mode.fmtIndex].cspace);
    osmgaWinPut(&t, " -- ");

    if (out->mode.bytesPerPixel != 4UL) {
        osmgaWinPut2(&t, &b, "no OpenGL: needs 32-bit colour");
        return;
    }
    if (!out->mmapOn) {
        osmgaWinPut2(&t, &b, "no OpenGL: VRAM Mmap is off");
        return;
    }
    if (!out->mesaOn) {
        osmgaWinPut2(&t, &b, "no OpenGL: Mesa Acceleration is off");
        return;
    }

    /*
     * Actual beats predicted whenever the caller has it.  The driver passes
     * the window it really registered; the inspector cannot, and everything
     * it says is therefore about what WOULD happen.
     */
    if (in->haveActual) {
        if (!in->hasWindow || in->windowEnd <= in->windowStart) {
            osmgaWinPut2(&t, &b, "no OpenGL: no offscreen window was "
                                 "registered");
            return;
        }
        avail = in->windowEnd - in->windowStart;
    } else {
        ceiling = OSMGAWindowCeiling(declared, declared, in->pageBytes);
        OSMGAWindowGeometry(&out->mode, in->pageBytes, ceiling, &geom);
        if (!geom.usable) {
            osmgaWinPut2(&t, &b, "no OpenGL: ");
            osmgaWinPut2(&t, &b, geom.reason);
            return;
        }
        avail = geom.bytes;
    }
    out->windowBytes = avail;

    strideCap = out->mode.width;
    for (i = 0U; i < OSMGA_WIN_RES_COUNT; i++) {
        if (osmgaWinRes[i].width > out->mode.width ||
            osmgaWinRes[i].height > out->mode.height)
            continue;
        if (OSMGASurfaceFits(osmgaWinRes[i].width, osmgaWinRes[i].height,
                             strideCap, in->pageBytes, avail, &lay)) {
            out->largestWidth = osmgaWinRes[i].width;
            out->largestHeight = osmgaWinRes[i].height;
        }
    }
    out->fullScreen = (out->largestWidth == out->mode.width &&
                       out->largestHeight == out->mode.height) ? 1 : 0;
    if (out->fullScreen &&
        OSMGASurfaceFits(out->mode.width, out->mode.height, strideCap,
                         in->pageBytes, avail, &lay))
        out->arenaBytes = lay.arenaBytes;

    /*
     * ready asks the smallest honest question: is there a surface here at
     * all?  A full-screen pair not fitting is a smaller window, not a dead
     * one, and refusing acceleration that demonstrably works would be a
     * worse answer than a modest one.
     */
    out->ready = OSMGASurfaceFits(OSMGA_WIN_MIN_WIDTH, OSMGA_WIN_MIN_HEIGHT,
                                  strideCap, in->pageBytes, avail, &lay);
    if (!out->ready) {
        osmgaWinPut2(&t, &b, "no OpenGL: the offscreen window is too small");
        return;
    }

    osmgaWinPutMB2(&t, &b, declared);
    osmgaWinPut2(&t, &b, in->haveActual ? " gives " : " would give ");
    osmgaWinPutMB2(&t, &b, avail);
    /* "offscreen" is what the brief drops for width; the conditional is not. */
    osmgaWinPut(&t, " offscreen, ");
    osmgaWinPut(&b, ", ");
    if (out->fullScreen) {
        osmgaWinPut(&t, "full-screen GL");
        osmgaWinPut(&b, "full-screen");
    } else {
        osmgaWinPut(&t, "GL up to ");
        osmgaWinPut(&b, "up to ");
        osmgaWinPutULong2(&t, &b, out->largestWidth);
        osmgaWinPut2(&t, &b, "x");
        osmgaWinPutULong2(&t, &b, out->largestHeight);
    }
}

/* ------------------------------------------------ the attempt limit */

unsigned long
OSMGAAttemptLimit(unsigned long declaredBytes, int gateAllows,
                  unsigned long conservativeBytes, unsigned long gatedBytes)
{
    if (conservativeBytes > gatedBytes || conservativeBytes == 0UL)
        return conservativeBytes;   /* nonsense inputs decide nothing new */
    if (declaredBytes == 0UL)
        return conservativeBytes;   /* nothing was asked for */

    /*
     * BELOW the conservative bound the gate has nothing to say.
     *
     * The gate exists to decide whether this boot may reach PAST sixteen
     * megabytes -- the amount the driver has always mapped and declared and
     * which every boot since the first has used.  A declaration smaller than
     * that asks for LESS than the established ground, and less needs no
     * permission from a survey.
     *
     * This function used to return only one of two values, so a declaration
     * of eight came out as sixteen and an eight-megabyte board would have
     * been offered twelve.  Cross-review caught it; it was the whole of what
     * made "8" look like a two-line change.
     */
    if (declaredBytes <= conservativeBytes)
        return declaredBytes;

    if (!gateAllows)
        return conservativeBytes;
    /* Only the gated size itself is reached; something in between is not
     * rounded up to it. */
    if (declaredBytes < gatedBytes)
        return conservativeBytes;
    return gatedBytes;
}

/* -------------------------------------------------- window lifecycle */

void
OSMGAWindowOpenDecision(unsigned long start, int proofPassed,
                        unsigned long provenEnd, unsigned long ceilingEnd,
                        unsigned long pageBytes,
                        unsigned long *outEnd, OSMGAWindowState *outState)
{
    unsigned long end;

    if (outEnd != 0)
        *outEnd = start;
    if (outState != 0)
        *outState = OSMGA_WINDOW_FAILED;
    if (!osmgaWinPageOk(pageBytes))
        return;

    /*
     * A failed proof falls back to the bound that never needed one, which is
     * exactly what the driver did before: the conservative end is justified
     * by a scanout that works, not by anything written this boot.
     */
    end = proofPassed ? ceilingEnd : provenEnd;
    end &= ~(pageBytes - 1UL);

    if (end <= start || end - start < pageBytes)
        return;                 /* nothing to open; FAILED, not empty-OPEN */

    if (outEnd != 0)
        *outEnd = end;
    if (outState != 0)
        *outState = OSMGA_WINDOW_OPEN;
}

int
OSMGAWindowMayRegister(unsigned long start, unsigned long ceilingEnd,
                       unsigned long apertureBytes, unsigned long pageBytes)
{
    if (!osmgaWinPageOk(pageBytes))
        return 0;
    if (ceilingEnd > apertureBytes)
        return 0;               /* never offer past what was mapped */
    ceilingEnd &= ~(pageBytes - 1UL);
    if (ceilingEnd <= start)
        return 0;
    return (ceilingEnd - start >= pageBytes) ? 1 : 0;
}

/* ------------------------------------------------------- PCI survey */

void
OSMGASurveyBegin(OSMGASurveyState *st, unsigned long fbPhysical,
                 unsigned long wantBytes)
{
    if (st == 0)
        return;
    st->fbPhysical = fbPhysical;
    st->wantBytes = wantBytes;
    st->verdict = OSMGA_SURVEY_CLEAR;
    st->offender = 0UL;
    st->claimsSeen = 0UL;
    /*
     * A want that would run past the end of the address space is refused
     * before anything is compared, so no test below has to worry about the
     * sum wrapping.
     */
    if (wantBytes == 0UL || fbPhysical > 0xFFFFFFFFUL - wantBytes)
        st->verdict = OSMGA_SURVEY_MALFORMED;
}

void
OSMGASurveyRefuse(OSMGASurveyState *st, OSMGASurveyVerdict why,
                  unsigned long offender)
{
    if (st == 0 || why == OSMGA_SURVEY_CLEAR)
        return;
    /* First problem wins: the walk keeps going so the log is complete, but
     * the reason it stopped being clear does not get overwritten. */
    if (st->verdict == OSMGA_SURVEY_CLEAR) {
        st->verdict = why;
        st->offender = offender;
    }
}

void
OSMGASurveyClaimBase(OSMGASurveyState *st, unsigned long base)
{
    if (st == 0)
        return;
    st->claimsSeen++;
    if (base == 0UL)
        return;                 /* unassigned by the firmware */
    if (st->verdict == OSMGA_SURVEY_MALFORMED)
        return;
    if (base >= st->fbPhysical && base < st->fbPhysical + st->wantBytes)
        OSMGASurveyRefuse(st, OSMGA_SURVEY_BAR_INSIDE, base);
    /*
     * Below and above need no test.  Below cannot reach us (see the header);
     * above is not in the range.  Deliberately written as one comparison so
     * that there is no second rule to get wrong.
     */
}

void
OSMGASurveyClaimWindow(OSMGASurveyState *st, unsigned long base,
                       unsigned long end, int isAncestor)
{
    unsigned long want;

    if (st == 0)
        return;
    st->claimsSeen++;
    if (st->verdict == OSMGA_SURVEY_MALFORMED)
        return;
    if (base >= end)
        return;                 /* a disabled window forwards nothing */
    want = st->fbPhysical + st->wantBytes;

    if (isAncestor && base <= st->fbPhysical && end > st->fbPhysical) {
        /*
         * This is the window our own aperture is forwarded through.  It is
         * not a collision -- and if it stops short of what we mean to use,
         * the aperture cannot be that large, because a bridge does not
         * forward what it does not claim.
         */
        if (end < want)
            OSMGASurveyRefuse(st, OSMGA_SURVEY_PARENT_TOO_SMALL, end);
        return;
    }
    if (isAncestor)
        return;                 /* our ancestor's other window; still ours */

    if (end > st->fbPhysical && base < want)
        OSMGASurveyRefuse(st, OSMGA_SURVEY_WINDOW_OVERLAPS, base);
}

int
OSMGASurveyIsClear(const OSMGASurveyState *st)
{
    if (st == 0)
        return 0;
    return st->verdict == OSMGA_SURVEY_CLEAR;
}

const char *
OSMGASurveyVerdictString(OSMGASurveyVerdict verdict)
{
    switch (verdict) {
    case OSMGA_SURVEY_CLEAR:
        return "clear";
    case OSMGA_SURVEY_BAR_INSIDE:
        return "a device is based inside the range";
    case OSMGA_SURVEY_WINDOW_OVERLAPS:
        return "a bridge forwards part of the range elsewhere";
    case OSMGA_SURVEY_PARENT_TOO_SMALL:
        return "our own bridge window ends before the range does";
    case OSMGA_SURVEY_UNKNOWN_HEADER:
        return "a function has a header layout we cannot read";
    case OSMGA_SURVEY_MALFORMED:
        return "a base register or the request itself is malformed";
    default:
        return "unknown";
    }
}

unsigned long
OSMGASurveyAlignmentBound(unsigned long base)
{
    if (base == 0UL)
        return 0UL;
    /* Lowest set bit.  A memory BAR hardwires the bits below its size to
     * zero, so the base is a multiple of the size and this is an upper
     * bound on it.  Diagnostic only. */
    return base & (0UL - base);
}

int
OSMGAAccelReadyBits(const OSMGAReadyIn *in)
{
    if (in == 0)
        return 0;
    if (!in->mmioMapped || !in->linearModeActive)
        return 0;
    if (in->bytesPerPixel != 4UL)
        return 0;
    /*
     * A window that was never registered, or one whose end is not above its
     * start, is not a small window -- it is no window.  Today the driver
     * refuses to register that case at all, so this is an assertion rather
     * than a new refusal; it is written down because the whole point of this
     * bit is that it stops being true by accident.
     */
    if (!in->windowRegistered || in->windowEnd <= in->windowStart)
        return 0;
    if (!in->tablesAgree)
        return 0;
    return OSMGASurfaceFits(OSMGA_WIN_MIN_WIDTH, OSMGA_WIN_MIN_HEIGHT,
                            in->strideCapPixels, in->pageBytes,
                            in->windowEnd - in->windowStart, 0);
}

int
OSMGAWindowMathTablesAgree(const char *const *resNames,
                           const unsigned long *resWidths,
                           const unsigned long *resHeights,
                           unsigned int resCount,
                           const char *const *fmtNames,
                           const unsigned long *fmtBytesPerPixel,
                           unsigned int fmtCount)
{
    unsigned int i;

    if (resCount != OSMGA_WIN_RES_COUNT || fmtCount != OSMGA_WIN_FMT_COUNT)
        return 0;
    if (resNames == 0 || resWidths == 0 || resHeights == 0 ||
        fmtNames == 0 || fmtBytesPerPixel == 0)
        return 0;
    for (i = 0U; i < resCount; i++) {
        if (!osmgaWinEquals(resNames[i], osmgaWinRes[i].name))
            return 0;
        if (resWidths[i] != osmgaWinRes[i].width)
            return 0;
        if (resHeights[i] != osmgaWinRes[i].height)
            return 0;
    }
    for (i = 0U; i < fmtCount; i++) {
        if (!osmgaWinEquals(fmtNames[i], osmgaWinFmt[i].cspace))
            return 0;
        if (fmtBytesPerPixel[i] != osmgaWinFmt[i].bytesPerPixel)
            return 0;
    }
    return 1;
}
