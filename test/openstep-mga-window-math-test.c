/*
 * Host regression for the shared offscreen-window arithmetic.
 *
 * Every expected number here was computed independently in python
 * (scratchpad/r9matrix.py and the derivation in
 * docs/R9_VRAM_DECLARATION_BUILD_PLAN.md) and is written out in full rather
 * than recomputed from the same expressions the code uses -- a test that
 * repeats the implementation proves only that it is self-consistent.
 *
 * The page size is passed explicitly everywhere, at the machine's real 8192.
 */

#include <stdio.h>
#include "OpenStepMGAWindowMath.h"

#define PAGE    8192UL
#define MB      (1024UL * 1024UL)

static int failures;

static void
expect(int condition, const char *label)
{
    if (!condition) {
        printf("OPENSTEP_MGA_WINDOW_MATH_TEST=fail:%s\n", label);
        failures++;
    }
}

static void
expect_ul(unsigned long got, unsigned long want, const char *label)
{
    if (got != want) {
        printf("OPENSTEP_MGA_WINDOW_MATH_TEST=fail:%s got=%lu want=%lu\n",
               label, got, want);
        failures++;
    }
}

static int
same_text(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void
expect_text(const char *got, const char *want, const char *label)
{
    if (!same_text(got, want)) {
        printf("OPENSTEP_MGA_WINDOW_MATH_TEST=fail:%s\n  got  \"%s\"\n"
               "  want \"%s\"\n", label, got, want);
        failures++;
    }
}

/* ------------------------------------------------------- declaration */

static void
expect_decl(const char *value, int expect_ok, unsigned long expect_bytes,
            OSMGADeclStatus expect_status, const char *label)
{
    unsigned long bytes = 0xdeadbeefUL;
    OSMGADeclStatus status = OSMGA_DECL_OK;
    int ok = OSMGAVramDeclaration(value, &bytes, &status);

    expect(ok == expect_ok, label);
    expect_ul(bytes, expect_bytes, label);
    expect(status == expect_status, label);
}

static void
test_declaration(void)
{
    expect_decl("16", 1, 16UL * MB, OSMGA_DECL_OK, "decl-16");
    expect_decl(" 32\t", 1, 32UL * MB, OSMGA_DECL_OK, "decl-32-spaced");
    /* Everything that is not 16 or 32 falls back to 16, never to zero. */
    expect_decl(0, 0, 16UL * MB, OSMGA_DECL_MISSING, "decl-null");
    expect_decl("", 0, 16UL * MB, OSMGA_DECL_MISSING, "decl-empty");
    /*
     * Eight is accepted, and ONLY when it was asked for exactly.  It is
     * smaller than the fallback, so everything unusable below must still
     * land on 16 -- a typo must not quietly shrink the board.
     */
    expect_decl("8", 1, 8UL * MB, OSMGA_DECL_OK, "decl-8-accepted");
    expect_decl(" 8 ", 1, 8UL * MB, OSMGA_DECL_OK, "decl-8-spaced");
    expect_decl("12", 0, 16UL * MB, OSMGA_DECL_UNSUPPORTED, "decl-12");
    expect_decl("4", 0, 16UL * MB, OSMGA_DECL_UNSUPPORTED, "decl-4");
    expect_decl("63", 0, 16UL * MB, OSMGA_DECL_UNSUPPORTED, "decl-63");
    expect_decl("8MB", 0, 16UL * MB, OSMGA_DECL_INVALID, "decl-8-with-suffix");
    expect_decl("64", 0, 16UL * MB, OSMGA_DECL_UNSUPPORTED, "decl-64");
    expect_decl("32MB", 0, 16UL * MB, OSMGA_DECL_INVALID, "decl-suffix");
    expect_decl("999999999999", 0, 16UL * MB, OSMGA_DECL_INVALID,
                "decl-overflow");
    expect(OSMGADeclStatusString(OSMGA_DECL_UNSUPPORTED)[0] == 'u',
           "decl-status-string");
}

static void
test_feature(void)
{
    expect(OSMGAFeatureIsOn("Yes") == 1, "feature-yes");
    expect(OSMGAFeatureIsOn("YES") == 0, "feature-case-sensitive");
    expect(OSMGAFeatureIsOn("No") == 0, "feature-no");
    expect(OSMGAFeatureIsOn(0) == 0, "feature-null");
    expect(OSMGAFeatureIsOn("") == 0, "feature-empty");
}

/* -------------------------------------------------------------- mode */

static void
test_mode(void)
{
    OSMGAModeSelection m;

    OSMGASelectMode("Height:1200 Width:1600 Refresh:60Hz "
                    "ColorSpace:RGB:888/32", &m);
    expect_ul(m.width, 1600UL, "mode-1600-w");
    expect_ul(m.height, 1200UL, "mode-1600-h");
    expect_ul(m.bytesPerPixel, 4UL, "mode-1600-bpp");
    expect(m.usedDefaultRes == 0 && m.usedDefaultFmt == 0, "mode-1600-explicit");

    OSMGASelectMode("Height:768 Width:1024 Refresh:60Hz ColorSpace:BW:8", &m);
    expect_ul(m.bytesPerPixel, 1UL, "mode-bw8-bpp");
    expect_ul(m.impliedGrayLevels, 0UL, "mode-bw8-no-implied-greys");

    /* The old spelling: BW:4 is BW:8 with four greys, not a format of its own. */
    OSMGASelectMode("Height:768 Width:1024 Refresh:60Hz ColorSpace:BW:4", &m);
    expect_ul(m.bytesPerPixel, 1UL, "mode-bw4-is-bw8");
    expect_ul(m.impliedGrayLevels, 4UL, "mode-bw4-implies-four-greys");

    /* Unparsable and absent both give the driver's default pair. */
    OSMGASelectMode(0, &m);
    expect_ul(m.width, 1024UL, "mode-null-default-w");
    expect_ul(m.bytesPerPixel, 4UL, "mode-null-default-bpp");
    expect(m.usedDefaultRes == 1 && m.usedDefaultFmt == 1, "mode-null-flags");

    OSMGASelectMode("garbage", &m);
    expect_ul(m.width, 1024UL, "mode-garbage-default-w");

    /* A size not in the table keeps the default rather than inventing one. */
    OSMGASelectMode("Height:900 Width:1440 Refresh:60Hz "
                    "ColorSpace:RGB:888/32", &m);
    expect_ul(m.width, 1024UL, "mode-unknown-size-default");
    expect(m.usedDefaultRes == 1, "mode-unknown-size-flag");
}

/* ------------------------------------------------------------ ceiling */

static void
test_ceiling(void)
{
    /* min(declared, aperture) less the 4 MiB top margin, page aligned. */
    expect_ul(OSMGAWindowCeiling(16UL * MB, 32UL * MB, PAGE), 12UL * MB,
              "ceiling-16-decl-32-board");
    expect_ul(OSMGAWindowCeiling(32UL * MB, 32UL * MB, PAGE), 28UL * MB,
              "ceiling-32-decl-32-board");
    /* A 32 declaration on a board surveyed at 16 gets the 16 answer: the
     * measurement narrows the declaration, never the other way round. */
    expect_ul(OSMGAWindowCeiling(32UL * MB, 16UL * MB, PAGE), 12UL * MB,
              "ceiling-32-decl-16-board");
    expect_ul(OSMGAWindowCeiling(4UL * MB, 4UL * MB, PAGE), 0UL,
              "ceiling-margin-eats-everything");
    expect_ul(OSMGAWindowCeiling(16UL * MB, 32UL * MB, 3UL), 0UL,
              "ceiling-bad-page");
}

/* ------------------------------------------------------------- window */

static void
expect_window(const char *mode, unsigned long declared,
              unsigned long expect_start, unsigned long expect_end,
              unsigned long expect_bytes, const char *label)
{
    OSMGAModeSelection m;
    OSMGAWindowGeom g;
    unsigned long ceiling = OSMGAWindowCeiling(declared, declared, PAGE);

    OSMGASelectMode(mode, &m);
    OSMGAWindowGeometry(&m, PAGE, ceiling, &g);
    expect_ul(g.start, expect_start, label);
    expect_ul(g.end, expect_end, label);
    expect_ul(g.bytes, expect_bytes, label);
    expect(g.usable == (expect_bytes != 0UL), label);
}

static void
test_window(void)
{
    /* python: start = align_up(1600*4*1200 + 256*1600*4, 8192) = 9,322,496 */
    expect_window("Height:1200 Width:1600 Refresh:60Hz ColorSpace:RGB:888/32",
                  16UL * MB, 9322496UL, 12UL * MB, 3260416UL, "window-1600-16");
    expect_window("Height:1200 Width:1600 Refresh:60Hz ColorSpace:RGB:888/32",
                  32UL * MB, 9322496UL, 28UL * MB, 20037632UL, "window-1600-32");
    expect_window("Height:768 Width:1024 Refresh:60Hz ColorSpace:RGB:888/32",
                  16UL * MB, 4194304UL, 12UL * MB, 8388608UL, "window-1024-16");
    expect_window("Height:480 Width:640 Refresh:60Hz ColorSpace:RGB:888/32",
                  16UL * MB, 1884160UL, 12UL * MB, 10698752UL, "window-640-16");

    /* A ceiling below the start leaves nothing, and says so rather than
     * wrapping into a huge window. */
    {
        OSMGAModeSelection m;
        OSMGAWindowGeom g;

        OSMGASelectMode("Height:1200 Width:1600 Refresh:60Hz "
                        "ColorSpace:RGB:888/32", &m);
        OSMGAWindowGeometry(&m, PAGE, 7UL * MB, &g);
        expect(g.usable == 0, "window-empty-usable");
        expect_ul(g.bytes, 0UL, "window-empty-bytes");
        expect(g.reason[0] != '\0', "window-empty-reason");
        /* This is today's 1600x1200 behaviour: the window is refused because
         * the visible image already reaches past the 7 MiB bound. */
        expect_ul(g.start, 9322496UL, "window-empty-start-still-reported");
    }
}

/* ------------------------------------------------------------ surface */

static void
expect_surface(unsigned long w, unsigned long h, unsigned long cap,
               unsigned long avail, int expect_fits, unsigned long expect_stride,
               unsigned long expect_depth_end, unsigned long expect_arena,
               const char *label)
{
    OSMGASurfaceLayout lay;
    int ok = OSMGASurfaceFits(w, h, cap, PAGE, avail, &lay);

    expect(ok == expect_fits, label);
    if (!expect_fits)
        return;
    expect_ul(lay.stridePixels, expect_stride, label);
    expect_ul(lay.depthEnd, expect_depth_end, label);
    expect_ul(lay.arenaBytes, expect_arena, label);
}

static void
test_surface(void)
{
    /* The 1600x1200 / 16 MB window.  python: 3,260,416 bytes available. */
    expect_surface(1600UL, 1200UL, 1600UL, 3260416UL, 0, 0UL, 0UL, 0UL,
                   "surface-1600-does-not-fit-16mb");
    /* ...but 800x600 does, which is why CAP_READY must not demand
     * full-screen: python gives colour+depth = 2,885,120. */
    expect_surface(800UL, 600UL, 1600UL, 3260416UL, 1, 800UL, 2885120UL,
                   368640UL, "surface-800-fits-16mb");
    expect_surface(320UL, 240UL, 1600UL, 3260416UL, 1, 320UL, 464896UL,
                   2793472UL, "surface-min-fits-16mb");

    /* The 1600x1200 / 32 MB window: 20,037,632 bytes, full screen fits. */
    expect_surface(1600UL, 1200UL, 1600UL, 20037632UL, 1, 1600UL, 11524096UL,
                   8511488UL, "surface-1600-fits-32mb");

    /* A surface can never be wider than the screen: the stride cap is the
     * display's own, and the allocator refuses anything past it. */
    expect_surface(1600UL, 1200UL, 640UL, 20037632UL, 0, 0UL, 0UL, 0UL,
                   "surface-wider-than-screen-refused");
    /* Rounding to 32 pixels can cross a cap the unrounded width was inside. */
    expect_surface(633UL, 100UL, 639UL, 20037632UL, 0, 0UL, 0UL, 0UL,
                   "surface-rounding-crosses-cap");
    expect_surface(0UL, 100UL, 640UL, 20037632UL, 0, 0UL, 0UL, 0UL,
                   "surface-zero-width");
    expect_surface(320UL, 240UL, 1600UL, 100UL, 0, 0UL, 0UL, 0UL,
                   "surface-window-too-small");

    /*
     * Absurd inputs are refused rather than wrapped into a small answer.
     *
     * HONEST LIMIT: this does NOT prove the module's 32-bit overflow guards.
     * The target's unsigned long is 32 bits; this host's is 64, and no 32-bit
     * libc is installed, so a value that would wrap on the target does not
     * wrap here -- a later bound catches it instead and the test passes with
     * the guard removed (checked: deleting the pre-rounding guard still
     * passes).  What these two do establish is that the refusal happens at
     * all.  The guards themselves are held by inspection and by the target
     * compile at stage 2, and that is written down rather than implied.
     */
    expect_surface(4096UL, 4294967295UL, 8192UL, 0xFFFFFFFFUL, 0, 0UL, 0UL,
                   0UL, "surface-absurd-height-refused");
    expect_surface(0xFFFFFFF0UL, 2UL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0, 0UL,
                   0UL, 0UL, "surface-absurd-width-refused");
}

/* ------------------------------------------------------------ verdict */

static void
run_verdict(const char *mode, const char *mem, const char *mmap,
            const char *mesa, OSMGAVerdictOut *out)
{
    OSMGAVerdictIn in;

    in.displayMode = mode;
    in.memorySizeValue = mem;
    in.mmapValue = mmap;
    in.mesaValue = mesa;
    in.pageBytes = PAGE;
    in.haveActual = 0;
    in.apertureBytes = 0UL;
    in.windowStart = 0UL;
    in.windowEnd = 0UL;
    in.hasWindow = 0;
    in.hasCommandWindow = 0;
    OSMGAAccelVerdict(&in, out);
}

#define M1600 "Height:1200 Width:1600 Refresh:60Hz ColorSpace:RGB:888/32"
#define M1024 "Height:768 Width:1024 Refresh:60Hz ColorSpace:RGB:888/32"
#define M1024BW "Height:768 Width:1024 Refresh:60Hz ColorSpace:BW:8"

static void
test_verdict(void)
{
    OSMGAVerdictOut v;

    /* The case the whole exercise is for. */
    run_verdict(M1600, "32", "Yes", "Yes", &v);
    expect(v.ready == 1, "verdict-1600-32-ready");
    expect(v.fullScreen == 1, "verdict-1600-32-fullscreen");
    expect_ul(v.windowBytes, 20037632UL, "verdict-1600-32-window");
    expect_ul(v.arenaBytes, 8511488UL, "verdict-1600-32-arena");
    expect_text(v.text, "1600x1200 RGB:888/32 -- 32.0 MB would give "
                        "19.1 MB offscreen, full-screen GL",
                "verdict-1600-32-text");

    /* Same mode, 16 declared: a real window and a real, smaller buffer. */
    run_verdict(M1600, "16", "Yes", "Yes", &v);
    expect(v.ready == 1, "verdict-1600-16-ready");
    expect(v.fullScreen == 0, "verdict-1600-16-not-fullscreen");
    expect_ul(v.largestWidth, 800UL, "verdict-1600-16-largest-w");
    expect_ul(v.largestHeight, 600UL, "verdict-1600-16-largest-h");
    expect_ul(v.windowBytes, 3260416UL, "verdict-1600-16-window");
    expect_text(v.text, "1600x1200 RGB:888/32 -- 16.0 MB would give "
                        "3.1 MB offscreen, GL up to 800x600",
                "verdict-1600-16-text");

    run_verdict(M1024, "16", "Yes", "Yes", &v);
    expect(v.fullScreen == 1, "verdict-1024-16-fullscreen");
    expect_ul(v.arenaBytes, 3670016UL, "verdict-1024-16-arena");

    /* The three refusals a person is most likely to hit. */
    run_verdict(M1024BW, "32", "Yes", "Yes", &v);
    expect(v.ready == 0, "verdict-bw-not-ready");
    expect_text(v.text, "1024x768 BW:8 -- no OpenGL: needs 32-bit colour",
                "verdict-bw-text");

    run_verdict(M1024, "32", "No", "Yes", &v);
    expect(v.ready == 0, "verdict-mmap-off-not-ready");
    expect_text(v.text, "1024x768 RGB:888/32 -- no OpenGL: VRAM Mmap is off",
                "verdict-mmap-off-text");

    run_verdict(M1024, "32", "Yes", "No", &v);
    expect_text(v.text,
                "1024x768 RGB:888/32 -- no OpenGL: Mesa Acceleration is off",
                "verdict-mesa-off-text");

    /* An unsupported declaration is reported AND falls back to 16. */
    run_verdict(M1600, "12", "Yes", "Yes", &v);
    expect(v.declStatus == OSMGA_DECL_UNSUPPORTED, "verdict-decl-12-status");
    expect_ul(v.declaredBytes, 16UL * MB, "verdict-decl-12-fallback");
    expect_ul(v.windowBytes, 3260416UL, "verdict-decl-12-window");

    /*
     * Eight, at a mode it cannot serve.  python: the ceiling is 4 MiB and
     * 1600x1200's window would start at 9,322,496, so there is nothing --
     * and the panel has to say so rather than print a number.
     */
    run_verdict(M1600, "8", "Yes", "Yes", &v);
    expect(v.declStatus == OSMGA_DECL_OK, "verdict-decl-8-status");
    expect_ul(v.declaredBytes, 8UL * MB, "verdict-decl-8-accepted");
    expect(v.ready == 0, "verdict-decl-8-1600-not-ready");
    expect_ul(v.windowBytes, 0UL, "verdict-decl-8-1600-no-window");

    /* A missing key behaves as 16, not as nothing. */
    run_verdict(M1600, 0, "Yes", "Yes", &v);
    expect_ul(v.declaredBytes, 16UL * MB, "verdict-decl-missing-clamped");
    expect(v.ready == 1, "verdict-decl-missing-ready");

    /* The driver's side: actual window beats the prediction, and the tense
     * of the sentence changes with it. */
    {
        OSMGAVerdictIn in;

        in.displayMode = M1600;
        in.memorySizeValue = "32";
        in.mmapValue = "Yes";
        in.mesaValue = "Yes";
        in.pageBytes = PAGE;
        in.haveActual = 1;
        in.apertureBytes = 32UL * MB;
        in.windowStart = 9322496UL;
        in.windowEnd = 12UL * MB;      /* stage two failed; 12 MiB was kept */
        in.hasWindow = 1;
        in.hasCommandWindow = 1;
        OSMGAAccelVerdict(&in, &v);
        expect(v.ready == 1, "verdict-actual-ready");
        expect(v.fullScreen == 0, "verdict-actual-not-fullscreen");
        expect_ul(v.windowBytes, 3260416UL, "verdict-actual-window");
        expect_text(v.text, "1600x1200 RGB:888/32 -- 32.0 MB gives "
                            "3.1 MB offscreen, GL up to 800x600",
                    "verdict-actual-text");

        /* No window registered at all: the driver says so plainly. */
        in.hasWindow = 0;
        in.windowEnd = in.windowStart;
        OSMGAAccelVerdict(&in, &v);
        expect(v.ready == 0, "verdict-actual-no-window-ready");
        expect_text(v.text, "1600x1200 RGB:888/32 -- no OpenGL: "
                            "no offscreen window was registered",
                    "verdict-actual-no-window-text");
    }

    /* Null input must not crash and must not claim anything. */
    OSMGAAccelVerdict(0, &v);
    expect(v.ready == 0, "verdict-null-in");
}

/* --------------------------------------------------------- ready bits */

/*
 * The CAP_READY predicate, enumerated exhaustively over its four boolean
 * inputs rather than sampled.  A capability bit that is wrong in one
 * combination is a bit nobody looks at again, and booting the demos
 * exercises exactly one combination.
 *
 * The reference expression below is written out independently of the
 * implementation, in the order the driver's own comments state it, so the
 * two agreeing means something.
 */
static int
ready_reference(int mmio, int linear, int registered, int agree,
                unsigned long bpp, unsigned long cap,
                unsigned long start, unsigned long end)
{
    unsigned long avail;

    if (!mmio) return 0;
    if (!linear) return 0;
    if (bpp != 4UL) return 0;
    if (!registered) return 0;
    /* end below start is the dangerous one: the subtraction would wrap to a
     * window of nearly four gigabytes and every size test below would pass. */
    if (end <= start) return 0;
    if (!agree) return 0;
    avail = end - start;
    /* 320x240 at 32 bpp: colour 307,200; depth start rounded to 8192 is
     * 311,296; depth 153,600; total 464,896 -- computed in python. */
    if (cap < 320UL) return 0;
    return (avail >= 464896UL) ? 1 : 0;
}

static void
test_ready_bits(void)
{
    /*
     * Window ends, as (start, end) pairs rather than lengths, so that an end
     * BELOW the start is among them.  A length can only express the cases
     * where the subtraction is sane, and the case worth testing is the one
     * where it is not.
     */
    static const unsigned long starts[5] = { 9322496UL, 9322496UL, 9322496UL,
                                             9322496UL, 9322496UL };
    static const unsigned long ends[5] = {
        9322496UL,                  /* empty: end == start */
        9322496UL - 8192UL,         /* INVERTED: end below start */
        9322496UL + 464895UL,       /* one byte short of the 320x240 pair */
        9322496UL + 464896UL,       /* exactly the 320x240 pair */
        12UL * MB                   /* the real 1600x1200 / 16 MB window */
    };
    OSMGAReadyIn in;
    int mmio, linear, reg, agree;
    unsigned int ai, bi;
    static const unsigned long bpps[3] = { 1UL, 2UL, 4UL };
    static const unsigned long caps[2] = { 319UL, 1600UL };
    unsigned int ci;
    int cases = 0;

    for (mmio = 0; mmio < 2; mmio++)
    for (linear = 0; linear < 2; linear++)
    for (reg = 0; reg < 2; reg++)
    for (agree = 0; agree < 2; agree++)
    for (ai = 0U; ai < 5U; ai++)
    for (bi = 0U; bi < 3U; bi++)
    for (ci = 0U; ci < 2U; ci++) {
        int got;
        int want;

        in.mmioMapped = mmio;
        in.linearModeActive = linear;
        in.windowRegistered = reg;
        in.tablesAgree = agree;
        in.bytesPerPixel = bpps[bi];
        in.strideCapPixels = caps[ci];
        in.windowStart = starts[ai];
        in.windowEnd = ends[ai];
        in.pageBytes = PAGE;
        got = OSMGAAccelReadyBits(&in);
        want = ready_reference(mmio, linear, reg, agree, bpps[bi], caps[ci],
                               in.windowStart, in.windowEnd);
        if (got != want) {
            printf("OPENSTEP_MGA_WINDOW_MATH_TEST=fail:ready-truth-table "
                   "mmio=%d linear=%d reg=%d agree=%d bpp=%lu cap=%lu "
                   "start=%lu end=%lu got=%d want=%d\n",
                   mmio, linear, reg, agree, bpps[bi], caps[ci],
                   in.windowStart, in.windowEnd, got, want);
            failures++;
        }
        cases++;
    }
    expect(cases == 2 * 2 * 2 * 2 * 5 * 3 * 2, "ready-case-count");

    /* The two that matter, named, so a failure says which. */
    in.mmioMapped = 1; in.linearModeActive = 1; in.windowRegistered = 1;
    in.tablesAgree = 1; in.bytesPerPixel = 4UL; in.strideCapPixels = 1600UL;
    in.windowStart = 9322496UL; in.windowEnd = 12UL * MB; in.pageBytes = PAGE;
    expect(OSMGAAccelReadyBits(&in) == 1, "ready-1600-16mb-window-is-ready");

    /* Today's 1600x1200 truth: no window at all, so not ready -- which is
     * the correction this change exists to make. */
    in.windowRegistered = 0; in.windowEnd = in.windowStart;
    expect(OSMGAAccelReadyBits(&in) == 0, "ready-no-window-is-not-ready");

    /* Disagreeing tables refuse, rather than accelerate on unvouched maths. */
    in.windowRegistered = 1; in.windowEnd = 12UL * MB; in.tablesAgree = 0;
    expect(OSMGAAccelReadyBits(&in) == 0, "ready-table-drift-fails-closed");

    expect(OSMGAAccelReadyBits(0) == 0, "ready-null");
}

/* ------------------------------------------------------------ briefs */

/*
 * The panel's line, asserted exactly for every path the caller can reach --
 * not merely checked for length.  A brief nobody asserted is a brief that
 * drifts away from the sentence it is supposed to be a short form of.
 *
 * The widths beside each are Adobe Helvetica advances at 12 pt, summed per
 * glyph, against the 340-unit field the existing switches already use.
 */
static void
expect_brief(const char *mode, const char *mem, const char *mmap,
             const char *mesa, const char *want, const char *label)
{
    OSMGAVerdictOut v;

    run_verdict(mode, mem, mmap, mesa, &v);
    expect_text(v.brief, want, label);
}

static void
test_brief(void)
{
    OSMGAVerdictOut v;

    /* Forecasts -- what the Configure.app panel can actually produce. */
    expect_brief(M1600, "32", "Yes", "Yes",
                 "32.0 MB would give 19.1 MB, full-screen",   /* 215 px */
                 "brief-1600-32");
    expect_brief(M1600, "16", "Yes", "Yes",
                 "16.0 MB would give 3.1 MB, up to 800x600",  /* 221 px */
                 "brief-1600-16");
    expect_brief(M1024, "16", "Yes", "Yes",
                 "16.0 MB would give 8.0 MB, full-screen",
                 "brief-1024-16");
    expect_brief(M1024BW, "32", "Yes", "Yes",
                 "no OpenGL: needs 32-bit colour",            /* 171 px */
                 "brief-bw8");
    expect_brief(M1024, "32", "No", "Yes",
                 "no OpenGL: VRAM Mmap is off",               /* 169 px */
                 "brief-mmap-off");
    expect_brief(M1024, "32", "Yes", "No",
                 "no OpenGL: Mesa Acceleration is off",
                 "brief-mesa-off");

    /* The driver's own path, where the tense changes and the window is real. */
    {
        OSMGAVerdictIn in;

        in.displayMode = M1600;
        in.memorySizeValue = "32";
        in.mmapValue = "Yes";
        in.mesaValue = "Yes";
        in.pageBytes = PAGE;
        in.haveActual = 1;
        in.apertureBytes = 32UL * MB;
        in.windowStart = 9322496UL;
        in.windowEnd = 29360128UL;
        in.hasWindow = 1;
        in.hasCommandWindow = 1;
        OSMGAAccelVerdict(&in, &v);
        expect_text(v.brief, "32.0 MB gives 19.1 MB, full-screen",
                    "brief-actual-32");

        in.hasWindow = 0;
        in.windowEnd = in.windowStart;
        OSMGAAccelVerdict(&in, &v);
        expect_text(v.brief,
                    "no OpenGL: no offscreen window was registered", /* 259 px */
                    "brief-actual-no-window");
    }

    OSMGAAccelVerdict(0, &v);
    expect_text(v.brief, "no configuration", "brief-null");

    /*
     * And the length contract, which is the cheap half: every brief above is
     * shorter than the buffer, so none of them is silently truncated.
     */
    run_verdict(M1600, "16", "Yes", "Yes", &v);
    {
        unsigned int n = 0U;

        while (v.brief[n] != '\0')
            n++;
        expect(n < OSMGA_VERDICT_BRIEF_MAX - 1U, "brief-not-truncated");
    }
}

/* ------------------------------------------------------ attempt limit */

static void
test_attempt_limit(void)
{
    unsigned long c = 16UL * MB;
    unsigned long g = 32UL * MB;

    /* The only combination that may reach past the conservative bound. */
    expect_ul(OSMGAAttemptLimit(32UL * MB, 1, c, g), g,
              "attempt-32-declared-gate-open");

    /* A clear gate asserts nothing on its own: the operator still has to ask. */
    expect_ul(OSMGAAttemptLimit(16UL * MB, 1, c, g), c,
              "attempt-16-declared-gate-open");
    /* And asking is not enough if the survey found something in the way. */
    expect_ul(OSMGAAttemptLimit(32UL * MB, 0, c, g), c,
              "attempt-32-declared-gate-shut");
    expect_ul(OSMGAAttemptLimit(16UL * MB, 0, c, g), c,
              "attempt-16-declared-gate-shut");

    /* A declaration between the two is not rounded up. */
    expect_ul(OSMGAAttemptLimit(24UL * MB, 1, c, g), c,
              "attempt-between-does-not-round-up");

    /*
     * BELOW the conservative bound the declaration is taken as it stands,
     * gate or no gate: the gate decides whether this boot may reach PAST
     * sixteen megabytes, and asking for less than sixteen needs no
     * permission.  This is the case the function could not express at all --
     * it returned only 16 or 32, so an 8 came out as 16 and an 8 MiB board
     * would have been offered twelve.
     */
    expect_ul(OSMGAAttemptLimit(8UL * MB, 1, c, g), 8UL * MB,
              "attempt-8-gate-open");
    expect_ul(OSMGAAttemptLimit(8UL * MB, 0, c, g), 8UL * MB,
              "attempt-8-gate-shut-still-8");
    /* python: 8 MiB - 4 MiB margin = 4 MiB, page aligned. */
    expect_ul(OSMGAWindowCeiling(OSMGAAttemptLimit(8UL * MB, 1, c, g),
                                 OSMGAAttemptLimit(8UL * MB, 1, c, g), PAGE),
              4UL * MB, "attempt-ceiling-8");
    /* Nothing asked for is not "zero allowed". */
    expect_ul(OSMGAAttemptLimit(0UL, 1, c, g), c, "attempt-zero-declared");
    /* Nonsense inputs decide nothing new rather than deciding wrongly. */
    expect_ul(OSMGAAttemptLimit(32UL * MB, 1, g, c), g,
              "attempt-inverted-bounds");

    /* And what the ceiling becomes, which is what the window is cut from.
     * python: 32 MiB - 4 MiB = 28 MiB; 16 MiB - 4 MiB = 12 MiB. */
    expect_ul(OSMGAWindowCeiling(OSMGAAttemptLimit(32UL * MB, 1, c, g),
                                 OSMGAAttemptLimit(32UL * MB, 1, c, g), PAGE),
              28UL * MB, "attempt-ceiling-32");
    expect_ul(OSMGAWindowCeiling(OSMGAAttemptLimit(32UL * MB, 0, c, g),
                                 OSMGAAttemptLimit(32UL * MB, 0, c, g), PAGE),
              12UL * MB, "attempt-ceiling-gate-shut");
}

/* ---------------------------------------------------- window lifecycle */

/*
 * The starts come from the real mode table and were computed in python:
 *   1024x768 BW:8          1,048,576
 *   1024x768 RGB:888/32    4,194,304
 *   1280x1024 RGB:888/32   6,553,600
 *   1600x1200 RGB:888/32   9,322,496
 * PROVEN is 7,340,032 and CEILING is 12,582,912.
 */
#define PROVEN   7340032UL
#define CEILING  12582912UL

static void
expect_open(unsigned long start, int passed, unsigned long wantEnd,
            OSMGAWindowState wantState, const char *label)
{
    unsigned long end = 0xdeadbeefUL;
    OSMGAWindowState st = OSMGA_WINDOW_UNOPENED;

    OSMGAWindowOpenDecision(start, passed, PROVEN, CEILING, PAGE, &end, &st);
    expect_ul(end, wantEnd, label);
    if (st != wantState) {
        printf("OPENSTEP_MGA_WINDOW_MATH_TEST=fail:%s state got=%d want=%d\n",
               label, (int)st, (int)wantState);
        failures++;
    }
}

static void
test_window_lifecycle(void)
{
    /*
     * Every mode that works today must get exactly what today's code gives
     * it -- in BOTH outcomes.  Today a failed widening also leaves the
     * conservative PROVEN end, so the failing column is not a regression.
     */
    expect_open(1048576UL, 1, CEILING, OSMGA_WINDOW_OPEN, "open-bw8-pass");
    expect_open(1048576UL, 0, PROVEN, OSMGA_WINDOW_OPEN, "open-bw8-fail");
    expect_open(4194304UL, 1, CEILING, OSMGA_WINDOW_OPEN, "open-1024x32-pass");
    expect_open(4194304UL, 0, PROVEN, OSMGA_WINDOW_OPEN, "open-1024x32-fail");
    expect_open(6553600UL, 1, CEILING, OSMGA_WINDOW_OPEN, "open-1280-pass");
    expect_open(6553600UL, 0, PROVEN, OSMGA_WINDOW_OPEN, "open-1280-fail");

    /* The row this stage exists for: it opens on a pass and is FAILED, not
     * an empty OPEN, when the proof does not carry it past its own start. */
    expect_open(9322496UL, 1, CEILING, OSMGA_WINDOW_OPEN, "open-1600-pass");
    expect_open(9322496UL, 0, 9322496UL, OSMGA_WINDOW_FAILED, "open-1600-fail");

    /* python: 12,582,912 - 9,322,496 = 3,260,416 */
    {
        unsigned long end = 0UL;
        OSMGAWindowState st = OSMGA_WINDOW_UNOPENED;

        OSMGAWindowOpenDecision(9322496UL, 1, PROVEN, CEILING, PAGE, &end, &st);
        expect_ul(end - 9322496UL, 3260416UL, "open-1600-window-bytes");
    }

    /* A ceiling one page above the start is the smallest openable window;
     * anything less is FAILED. */
    expect_open(CEILING - PAGE, 1, CEILING, OSMGA_WINDOW_OPEN,
                "open-exactly-one-page");
    expect_open(CEILING - PAGE + 8UL, 1, CEILING - PAGE + 8UL,
                OSMGA_WINDOW_FAILED, "open-less-than-one-page");
    expect_open(CEILING, 1, CEILING, OSMGA_WINDOW_FAILED, "open-start-at-end");
    expect_open(CEILING + PAGE, 1, CEILING + PAGE, OSMGA_WINDOW_FAILED,
                "open-start-past-end");

    /* A bad page size decides nothing rather than deciding wrongly. */
    {
        unsigned long end = 0UL;
        OSMGAWindowState st = OSMGA_WINDOW_OPEN;

        OSMGAWindowOpenDecision(1048576UL, 1, PROVEN, CEILING, 3UL, &end, &st);
        expect(st == OSMGA_WINDOW_FAILED, "open-bad-page-size");
    }
    /* Null outputs must not crash. */
    OSMGAWindowOpenDecision(1048576UL, 1, PROVEN, CEILING, PAGE, 0, 0);

    /* Registration is about whether the window can EVER become non-empty,
     * so it is judged against the ceiling, not against the proven bound.
     * This is the whole of the 1600x1200 fix. */
    expect(OSMGAWindowMayRegister(9322496UL, CEILING, 16UL * MB, PAGE) == 1,
           "register-1600-now-allowed");
    expect(OSMGAWindowMayRegister(9322496UL, PROVEN, 16UL * MB, PAGE) == 0,
           "register-1600-refused-against-the-old-bound");
    expect(OSMGAWindowMayRegister(1048576UL, CEILING, 16UL * MB, PAGE) == 1,
           "register-bw8-allowed");
    /* Nothing above the aperture the driver mapped is ever offered. */
    expect(OSMGAWindowMayRegister(1048576UL, 20UL * MB, 16UL * MB, PAGE) == 0,
           "register-past-aperture-refused");
    expect(OSMGAWindowMayRegister(CEILING, CEILING, 16UL * MB, PAGE) == 0,
           "register-empty-refused");
    /*
     * Between one byte and one page is still nothing: a client maps whole
     * pages.  Without this the suite passes with the minimum-size test
     * deleted, because every other case is either empty or many pages.
     */
    expect(OSMGAWindowMayRegister(CEILING - 8UL, CEILING, 16UL * MB, PAGE) == 0,
           "register-sub-page-refused");
    expect(OSMGAWindowMayRegister(CEILING - PAGE, CEILING, 16UL * MB, PAGE) == 1,
           "register-exactly-one-page-allowed");

    /*
     * Both real bounds are already page multiples (python: 7,340,032 and
     * 12,582,912 divide by 8192), so the rounding in the decision is a no-op
     * for them and deleting it passes every case above.  This one is not a
     * multiple, and a client may only be handed whole pages.
     */
    {
        unsigned long end = 0UL;
        OSMGAWindowState st = OSMGA_WINDOW_UNOPENED;

        OSMGAWindowOpenDecision(1048576UL, 0, PROVEN + 100UL, CEILING, PAGE,
                                &end, &st);
        expect_ul(end, PROVEN, "open-rounds-a-ragged-bound-down");
        expect(st == OSMGA_WINDOW_OPEN, "open-ragged-bound-state");
    }
}

/* ------------------------------------------------------------- survey */

/*
 * The machine's own numbers (docs/P0_TARGET_INVENTORY.md, docs/TEST_STATUS.md
 * H1 S1): the MGA is 04:00.0 with BAR0 at 0xf8000000, and the aperture we
 * would like to use for a 32 MB declaration is 0xf8000000..0xfa000000.
 * Every expectation below was computed in python first.
 */
#define FB      0xf8000000UL
#define WANT32  (32UL * MB)

static void
expect_base(unsigned long base, int expect_clear, const char *label)
{
    OSMGASurveyState st;

    OSMGASurveyBegin(&st, FB, WANT32);
    OSMGASurveyClaimBase(&st, base);
    expect(OSMGASurveyIsClear(&st) == expect_clear, label);
    if (!expect_clear)
        expect(st.verdict == OSMGA_SURVEY_BAR_INSIDE, label);
}

static void
expect_survey_window(unsigned long base, unsigned long end, int ancestor,
                     OSMGASurveyVerdict want, const char *label)
{
    OSMGASurveyState st;

    OSMGASurveyBegin(&st, FB, WANT32);
    OSMGASurveyClaimWindow(&st, base, end, ancestor);
    if (st.verdict != want) {
        printf("OPENSTEP_MGA_WINDOW_MATH_TEST=fail:%s got=%s want=%s\n",
               label, OSMGASurveyVerdictString(st.verdict),
               OSMGASurveyVerdictString(want));
        failures++;
    }
}

static void
test_survey(void)
{
    OSMGASurveyState st;

    /* This machine's real neighbours: the MGA's own BAR1 and BAR2 are below
     * the range and are not in the way even though they are on the card. */
    expect_base(0xe8200000UL, 1, "survey-bar1-below-clear");
    expect_base(0xe8800000UL, 1, "survey-bar2-below-clear");

    /*
     * The case that killed the earlier design.  A neighbour at 0xf0000000 has
     * an alignment bound of 256 MiB, so bounding its extent would have made
     * it collide with 0xf8000000..0xfa000000 -- refusing a 32 MB aperture on
     * account of a device that may be four kilobytes.  Judged by base alone
     * it is below us, and a device below us cannot reach in.
     */
    expect_base(0xf0000000UL, 1, "survey-below-us-clear-even-if-huge-aligned");
    expect(OSMGASurveyAlignmentBound(0xf0000000UL) == 256UL * MB,
           "survey-alignment-bound-diagnostic");

    /* The boundaries, which is where an off-by-one would live. */
    expect_base(0xf7ffffffUL, 1, "survey-one-below-start-clear");
    expect_base(FB, 0, "survey-exactly-at-start-collides");
    expect_base(0xf9000000UL, 0, "survey-middle-collides");
    expect_base(0xf9ffffffUL, 0, "survey-last-byte-collides");
    expect_base(0xfa000000UL, 1, "survey-exactly-at-end-clear");
    expect_base(0UL, 1, "survey-unassigned-ignored");

    /* Bridge windows: read, not inferred. */
    expect_survey_window(FB, 0xfc000000UL, 1, OSMGA_SURVEY_CLEAR,
                  "survey-parent-window-covers-us");
    expect_survey_window(FB, 0xf9000000UL, 1, OSMGA_SURVEY_PARENT_TOO_SMALL,
                  "survey-parent-window-ends-early");
    expect_survey_window(0xf0000000UL, 0xfa000000UL, 1, OSMGA_SURVEY_CLEAR,
                  "survey-parent-window-exactly-reaches");
    expect_survey_window(0xf0000000UL, 0xf9000000UL, 1, OSMGA_SURVEY_PARENT_TOO_SMALL,
                  "survey-parent-window-short-from-below");
    expect_survey_window(0xf9000000UL, 0xf9800000UL, 0, OSMGA_SURVEY_WINDOW_OVERLAPS,
                  "survey-foreign-window-inside");
    expect_survey_window(0xfa000000UL, 0xfb000000UL, 0, OSMGA_SURVEY_CLEAR,
                  "survey-foreign-window-above");
    expect_survey_window(0xf7000000UL, FB, 0, OSMGA_SURVEY_CLEAR,
                  "survey-foreign-window-abuts-below");
    expect_survey_window(0xf7000000UL, FB + 1UL, 0, OSMGA_SURVEY_WINDOW_OVERLAPS,
                  "survey-foreign-window-one-byte-in");
    expect_survey_window(0xf9000000UL, 0xf9000000UL, 0, OSMGA_SURVEY_CLEAR,
                  "survey-disabled-window-ignored");
    expect_survey_window(0xfb000000UL, 0xf9000000UL, 0, OSMGA_SURVEY_CLEAR,
                  "survey-inverted-window-ignored");

    /* The first problem is kept; a later one does not overwrite it, so the
     * walk can continue and log everything. */
    OSMGASurveyBegin(&st, FB, WANT32);
    OSMGASurveyClaimBase(&st, 0xf9000000UL);
    OSMGASurveyRefuse(&st, OSMGA_SURVEY_UNKNOWN_HEADER, 0x12345678UL);
    expect(st.verdict == OSMGA_SURVEY_BAR_INSIDE, "survey-first-problem-wins");
    expect_ul(st.offender, 0xf9000000UL, "survey-first-offender-kept");
    expect_ul(st.claimsSeen, 1UL, "survey-claims-counted");

    /* A request that would run off the end of the address space is refused
     * before anything is compared: python says 0xf8000000 + 128 MiB is
     * 0x100000000, which does not fit. */
    OSMGASurveyBegin(&st, FB, 128UL * MB);
    expect(st.verdict == OSMGA_SURVEY_MALFORMED, "survey-want-overflows");
    OSMGASurveyClaimBase(&st, 0xe8200000UL);
    expect(st.verdict == OSMGA_SURVEY_MALFORMED, "survey-overflow-stays");

    OSMGASurveyBegin(&st, FB, 0UL);
    expect(st.verdict == OSMGA_SURVEY_MALFORMED, "survey-zero-want");

    /* A 16 MB request on the same machine, for comparison. */
    OSMGASurveyBegin(&st, FB, 16UL * MB);
    OSMGASurveyClaimBase(&st, 0xf9000000UL);
    expect(OSMGASurveyIsClear(&st) == 1,
           "survey-16mb-unaffected-by-a-neighbour-at-f9000000");

    expect(OSMGASurveyIsClear(0) == 0, "survey-null");
    expect(OSMGASurveyVerdictString(OSMGA_SURVEY_CLEAR)[0] == 'c',
           "survey-verdict-string");
}

/* ------------------------------------------------------------- tables */

static void
test_tables(void)
{
    static const char *const names[5] = {
        "640x480", "800x600", "1024x768", "1280x1024", "1600x1200"
    };
    static const unsigned long widths[5] = { 640UL, 800UL, 1024UL, 1280UL, 1600UL };
    static const unsigned long heights[5] = { 480UL, 600UL, 768UL, 1024UL, 1200UL };
    static const char *const fmts[4] = {
        "RGB:888/32", "RGB:555/16", "RGB:256/8", "BW:8"
    };
    static const unsigned long bpp[4] = { 4UL, 2UL, 1UL, 1UL };
    static const unsigned long wrongBpp[4] = { 4UL, 2UL, 2UL, 1UL };

    expect(OSMGAWindowMathTablesAgree(names, widths, heights, 5U,
                                      fmts, bpp, 4U) == 1, "tables-agree");
    expect(OSMGAWindowMathTablesAgree(names, widths, heights, 5U,
                                      fmts, wrongBpp, 4U) == 0,
           "tables-detect-bpp-drift");
    expect(OSMGAWindowMathTablesAgree(names, widths, heights, 4U,
                                      fmts, bpp, 4U) == 0,
           "tables-detect-count-drift");
    expect(OSMGAWinResAt(4U) != 0 && OSMGAWinResAt(5U) == 0, "tables-res-bounds");
    expect(OSMGAWinFmtAt(3U) != 0 && OSMGAWinFmtAt(4U) == 0, "tables-fmt-bounds");
}

int
main(void)
{
    test_declaration();
    test_feature();
    test_mode();
    test_ceiling();
    test_window();
    test_surface();
    test_verdict();
    test_ready_bits();
    test_survey();
    test_brief();
    test_attempt_limit();
    test_window_lifecycle();
    test_tables();
    if (failures != 0)
        return 1;
    printf("OPENSTEP_MGA_WINDOW_MATH_TEST=pass\n");
    return 0;
}
