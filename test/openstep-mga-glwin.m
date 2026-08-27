/*
 * GL in a window the user can move.
 *
 * The spin sample proved the present path at a FIXED screen rectangle: 295
 * fps hardware against 33 software, same screen path.  This puts the same
 * scene inside an AppKit window: every frame asks the window where its
 * content sits on the screen, converts that to framebuffer coordinates, and
 * presents there.  Move the window and the picture follows -- with at most
 * one frame of lag, and only when frames are being delivered at all (see the
 * note on dragging below).
 *
 * Structure lifted from OnionApp.m, the code-only AppKit app this workspace
 * already runs on this machine.  Decisions that came out of review:
 *
 *  - The coordinate chain is convertPoint:toView:nil then
 *    convertBaseToScreen:.  A view's frame origin lives in its SUPERVIEW's
 *    coordinates; the chain is the invariant, the shortcut only happens to
 *    work for a root content view.
 *  - The accelerated window is NONRETAINED, and from birth: we repaint its
 *    interior every frame ourselves, a buffered backing would let the window
 *    server repaint stale darkness over the live picture, and AppKit does
 *    not allow a nonretained window to change its backing later.  The stock
 *    Mesa build uses a buffered window because AppKit owns its delivery.
 *  - The timer runs at sixty hertz, not zero: a zero timer starves event
 *    dispatch and buys nothing past the refresh rate.  It is ALSO registered
 *    in NSEventTrackingRunLoopMode -- this machine's Foundation has
 *    addTimer:forMode: and its AppKit exports the tracking mode (checked in
 *    the real headers, NSRunLoop.h:35 and NSApplication.h:21) -- so frames
 *    keep coming while the title bar is being dragged.  Whether the window
 *    server actually delivers them mid-drag is recorded by trying it.
 *  - Every frame re-checks visibility (isVisible, isMiniaturized, app
 *    isHidden) before presenting; a rectangle that leaves the screen skips
 *    the present rather than asking the kernel to refuse it.
 *  - A present refusal is a state change, not a log line: presenting stops
 *    and the reason is shown in the window title.
 *
 * Known and accepted, same family as spin: no z-order authority (we draw
 * over whatever overlaps us), exposes wipe the rect until the next frame,
 * tearing.
 */
#import <AppKit/AppKit.h>
#ifdef OSMGA_GLWIN_PLAIN
#import <AppKit/NSDPSContext.h>
#else
#import <AppKit/psopsNeXT.h>
#endif
#import <Foundation/Foundation.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#ifndef OSMGA_GLWIN_PLAIN
/*
 * By bare name, not by relative path, because this file ships.
 *
 * In the tree these three sit in ../mesa and ../hw3d and the build passes
 * -I for both.  In the package they arrive together in Headers/ and the
 * shipped build passes -I$prefix/Headers.  A path with ../mesa in it would
 * work in exactly one of those two places.  The offline teapot demo already
 * does it this way for the same reason.
 */
#include "OpenStepMGAMesaHook.h"
#include "OpenStepMGAMesaBuffer.h"
#include "OpenStepMGAHW3D.h"
#endif

/* The Utah teapot, cut from the Mesa tree at build time -- same arrangement
 * as the teapot renderer, same licence reasoning (nothing committed). */
#include "teapot-geometry.h"

/*
 * The window, fixed at 800 by 600.
 *
 * Chosen against a 1600x1200 screen -- a quarter of its area, big enough to
 * see the pot and small enough to leave the desktop around it.  Two things
 * had to hold and both were checked before it moved:
 *
 *   The frustum below is 2.0 wide by 1.5 tall, so it wants 4:3.  640x480 and
 *   800x600 are both exactly 4:3, so the pot does not stretch and the
 *   projection needs no change.
 *
 *   The extra pixels are nearly free.  A frame's cost here is the geometry,
 *   not the fill: covering a whole surface on the engine was measured at
 *   5.53 ns per pixel, so the 172,800 pixels 800x600 adds over 640x480 cost
 *   about 0.96 ms against a frame of roughly thirty -- three per cent.
 *
 * The mirror does not enter into it at all: PresentMode(1) stands it down,
 * and it is the mirror, not the drawing, that makes the offline renderer
 * scale with area (a whole-surface walk-back was measured at 749 ns per
 * pixel -- a hundred and thirty times the engine's fill).
 */
#define GLW 800
#define GLH 600

#ifndef OSMGA_GLWIN_PLAIN
@interface GLDarkView : NSView
@end
@implementation GLDarkView
- (BOOL)isFlipped { return NO; }
- (void)drawRect:(NSRect)r
{
    /* Only ever seen on an expose, and overwritten by the next present. */
    PSsetgray(0.08);
    NSRectFill(r);
}
@end
#endif

#ifdef OSMGA_GLWIN_PLAIN
/*
 * Stock Mesa renders OSMESA_ARGB into caller memory.  On i386 that word's
 * bytes are B,G,R,A, while the hardware-verified OPENSTEP bitmap presenter
 * takes packed R,G,B.  Its non-NULL plane is referenced, not copied or
 * freed, so rgb remains allocated until after rep is released.
 */
@interface GLBitmapView : NSView
{
    unsigned char *rgb;
    NSBitmapImageRep *rep;
}
- (void)copyFromBGRA:(unsigned char *)source;
- (void)drawBitmap;
@end

@implementation GLBitmapView
- initWithFrame:(NSRect)frame
{
    int pitch;
    unsigned char *planes[1];

    [super initWithFrame:frame];
    pitch = GLW * 3;
    rgb = (unsigned char *)malloc((unsigned long)pitch * GLH);
    if (rgb == NULL)
        return nil;
    memset(rgb, 0, (unsigned long)pitch * GLH);
    planes[0] = rgb;
    rep = [[NSBitmapImageRep alloc]
              initWithBitmapDataPlanes:planes
                            pixelsWide:GLW
                            pixelsHigh:GLH
                         bitsPerSample:8
                       samplesPerPixel:3
                              hasAlpha:NO
                               isPlanar:NO
                     colorSpaceName:NSCalibratedRGBColorSpace
                        bytesPerRow:pitch
                       bitsPerPixel:24];
    if (rep == nil) {
        free(rgb);
        rgb = NULL;
        return nil;
    }
    return self;
}

- (void)dealloc
{
    [rep release];
    if (rgb != NULL)
        free(rgb);
    [super dealloc];
}

- (BOOL)isFlipped
{
    return YES;
}

- (void)copyFromBGRA:(unsigned char *)source
{
    unsigned char *src;
    unsigned char *dst;
    int x;
    int y;

    for (y = 0; y < GLH; ++y) {
        src = source + (unsigned long)y * GLW * 4;
        /* NSBitmapImageRep backing is bottom-up: source row zero is the
         * visual top, so it belongs in the bitmap's last stored row. */
        dst = rgb + (unsigned long)(GLH - 1 - y) * GLW * 3;
        for (x = 0; x < GLW; ++x) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            src += 4;
            dst += 3;
        }
    }
}

- (void)drawBitmap
{
    NSRect target;

    target = [self bounds];
    [rep drawInRect:target];
}

- (void)drawRect:(NSRect)rect
{
    [self drawBitmap];
}
@end
#endif

@interface GLWinController : NSObject
{
    NSWindow *win;
#ifdef OSMGA_GLWIN_PLAIN
    GLBitmapView *view;
#else
    GLDarkView *view;
#endif
    NSTimer *timer;
    OSMesaContext ctx;
    unsigned long *buf;
    double angle;
    float screenW, screenH;
    unsigned long frames, skips, moveSkips, evSkips;
    float lastX, lastY;
    float srvOffX, srvOffY;     /* server frame origin -> content origin */
    int havePos;
    int moving;
    unsigned long stillTicks;
    float mvX, mvY;
    int haveMv;
    double sumMs, minMs, maxMs, lastReport, wallStart;
    int presenting;
#ifdef OSMGA_GLWIN_PLAIN
    int waitingForServer;
#endif
    /*
     * The rolling half-second, kept apart from the totals above.
     *
     * The totals are a mean since start-up, and after twenty-six thousand
     * frames a mean like that barely moves -- useless for a number meant to
     * answer "what is it doing NOW", and worse than useless if the two
     * builds are to be told apart at a glance.  These values are emptied at
     * every report.
     */
    unsigned long winFrames;
    double winSumMs, winStart;
    double winClrMs, winDrwMs, winPrsMs, winCvtMs, winAppMs, winWaitMs;
    double totClrMs, totDrwMs, totPrsMs, totCvtMs, totAppMs, totWaitMs;
    int forcedSoftware;         /* argv "soft": Mesa rasterises, we still present */
    int measureArm;             /* argv "armC"/"armD": see OSMGAMesaHookMeasureArm */
    int toldRefusal;            /* one-shot diagnostic latch */
    int grid;                   /* teapot tessellation; 4 is what it has always drawn */
    double potScale;            /* teapot size; 1.0 is what it has always drawn */
}
- (void)setup;
- (void)tick:(NSTimer *)t;
- (void)setForcedSoftware:(int)on;
- (void)setMeasureArm:(int)arm;
- (void)setGrid:(int)g;
- (void)setPotScale:(double)v;
@end

@implementation GLWinController

- (void)setForcedSoftware:(int)on
{
    forcedSoftware = on;
}

/*
 * The measurement arm.  1 and 2 draw NOTHING on the screen by design -- they
 * exist to time the frame with one stage removed, not to show a picture.
 */
- (void)setMeasureArm:(int)arm
{
    measureArm = arm;
}

/*
 * Tessellation.  Triangles go as the SQUARE of this while the pot covers the
 * same pixels, which is what lets a sweep separate per-triangle cost from
 * per-pixel cost.
 */
- (void)setGrid:(int)g
{
    grid = g;
}

/*
 * Size.  The triangle COUNT does not change with this, only the pixels they
 * cover -- which is what lets a sweep measure the per-pixel cost on its own,
 * instead of backing it out of a clear.
 */
- (void)setPotScale:(double)v
{
    potScale = v;
}

- (void)setup
{
    NSRect wr = NSMakeRect(192, 140, GLW, GLH);
#ifndef OSMGA_GLWIN_PLAIN
    NSRect sf = [[NSScreen mainScreen] frame];

    screenW = sf.size.width;
    screenH = sf.size.height;
#endif

    win = [[NSWindow alloc]
              initWithContentRect:wr
                        styleMask:(NSTitledWindowMask |
                                   NSClosableWindowMask |
                                   NSMiniaturizableWindowMask)
#ifdef OSMGA_GLWIN_PLAIN
                          backing:NSBackingStoreBuffered
#else
                          backing:NSBackingStoreNonretained
#endif
                            defer:NO];
    [win setTitle:@"OpenGL"];
    [win setDelegate:self];
#ifdef OSMGA_GLWIN_PLAIN
    view = [[GLBitmapView alloc] initWithFrame:
               NSMakeRect(0, 0, GLW, GLH)];
#else
    view = [[GLDarkView alloc] initWithFrame:
               NSMakeRect(0, 0, GLW, GLH)];
#endif
    [view setAutoresizingMask:NSViewNotSizable];
    [win setContentView:view];

    buf = (unsigned long *)malloc((unsigned)(GLW * GLH) * sizeof *buf);
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
#ifdef OSMGA_GLWIN_PLAIN
    if (win == nil || view == nil || buf == 0 || ctx == 0 ||
        !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, GLW, GLH)) {
        [win setTitle:@"stock: no software surface"];
        presenting = 0;
    } else {
        presenting = 1;
    }
#else
    if (buf == 0 || ctx == 0 ||
        !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, GLW, GLH) ||
        OSMGAMesaBufferOrigin() == 0UL) {
        [win setTitle:@"OpenGL: no accelerated surface"];
        presenting = 0;
    } else {
        OSMGAMesaBufferPresentMode(1);
        presenting = 1;
    }
#endif

    /*
     * "soft" sends every triangle to Mesa's own rasteriser and makes the
     * back end decline the engine clear, so the picture is drawn entirely in
     * software -- and then delivered by exactly the same VRAM-to-VRAM blit.
     *
     * That is the point of doing it this way.  Only the rasteriser differs;
     * the surface, the geometry, the lighting, the evaluators, the clear's
     * coverage and the delivery are all identical, so the difference between
     * the two titles is the rasteriser and nothing else.
     *
     * It is NOT the same thing as a stock-Mesa build.  Mesa is writing its
     * spans into video memory here, not into system memory, and this file
     * cannot say what that costs -- nobody has measured writes in that
     * direction.  A stock build would write to system memory and then have
     * to move the result to the screen itself, which this does not do.  So
     * read the software figure as "Mesa rasterising into the surface the
     * driver gave it", which is what it is.
     */
#ifndef OSMGA_GLWIN_PLAIN
    if (forcedSoftware)
        OSMGAMesaHookForceSoftware(1);
#ifdef OSMGA_MESA_TESTHOOKS
    if (measureArm != 0)
        OSMGAMesaHookMeasureArm(measureArm);
#endif
#endif

    glViewport(0, 0, GLW, GLH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    /* Top and bottom swapped: memory row nought lands on the top screen
     * row and a blit cannot flip, so the projection does.  Winding reverses
     * with it, which is why culling stays off. */
    glFrustum(-1.0, 1.0, 0.75, -0.75, 2.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, -0.2f, -6.0f);
    glRotatef(-20.0f, 1.0f, 0.0f, 0.0f);
    glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearDepth(1.0);
    {
        GLfloat amb[4], dif[4], pos[4], lamb[4];
        GLfloat mamb[4], mdif[4], mspec[4];

        amb[0] = 0.0f; amb[1] = 0.0f; amb[2] = 0.0f; amb[3] = 1.0f;
        dif[0] = 1.0f; dif[1] = 1.0f; dif[2] = 1.0f; dif[3] = 1.0f;
        pos[0] = 0.0f; pos[1] = 3.0f; pos[2] = 3.0f; pos[3] = 0.0f;
        lamb[0] = 0.2f; lamb[1] = 0.2f; lamb[2] = 0.2f; lamb[3] = 1.0f;
        glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
        glLightfv(GL_LIGHT0, GL_POSITION, pos);
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, lamb);
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        mamb[0] = 0.18f; mamb[1] = 0.07f; mamb[2] = 0.03f; mamb[3] = 1.0f;
        mdif[0] = 0.9f;  mdif[1] = 0.35f; mdif[2] = 0.15f; mdif[3] = 1.0f;
        mspec[0] = 0.9f; mspec[1] = 0.9f; mspec[2] = 0.9f; mspec[3] = 1.0f;
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mamb);
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mdif);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mspec);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 50.0f);
    }
    glClearColor(0.06f, 0.08f, 0.14f, 1.0f);

    if (grid <= 0) grid = 4;
    if (potScale <= 0.0) potScale = 1.0;
    minMs = 1e9; maxMs = 0.0; sumMs = 0.0;
    lastReport = [NSDate timeIntervalSinceReferenceDate];
    wallStart = lastReport;
    winFrames = 0UL; winSumMs = 0.0; winStart = lastReport;
    winClrMs = 0.0; winDrwMs = 0.0; winPrsMs = 0.0;
    winCvtMs = 0.0; winAppMs = 0.0; winWaitMs = 0.0;
    totClrMs = 0.0; totDrwMs = 0.0; totPrsMs = 0.0;
    totCvtMs = 0.0; totAppMs = 0.0; totWaitMs = 0.0;
#ifdef OSMGA_GLWIN_PLAIN
    waitingForServer = 0;
    [win setTitle:@"stock -- measuring"];
#else
    /*
     * Only when there is something to measure.
     *
     * The setup above may already have put "no accelerated surface" in the
     * title, which is the one thing a user without the driver needs to see,
     * and an unconditional set here wiped it out and replaced it with
     * "measuring" -- on a window that then draws nothing at all, because
     * tick: returns immediately while `presenting` is false.  An empty
     * window labelled as measuring hardware is worse than an empty window
     * that says why it is empty.
     */
    if (presenting)
        [win setTitle:(forcedSoftware ? @"teapot -- software -- measuring"
                                      : @"teapot -- hardware -- measuring")];
#endif

    timer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 60.0)
                                             target:self
                                           selector:@selector(tick:)
                                           userInfo:nil
                                            repeats:YES];
    /* And in the tracking mode too, so dragging does not starve frames. */
    [[NSRunLoop currentRunLoop] addTimer:timer
                                 forMode:NSEventTrackingRunLoopMode];

    [win makeKeyAndOrderFront:nil];

    /*
     * Calibrate the server-bounds offset while the cache is provably
     * right: the window has just been placed and nothing has moved it.
     * From here on the tick asks the server, not the cache.
     */
#ifndef OSMGA_GLWIN_PLAIN
    {
        NSPoint c = [view convertPoint:NSMakePoint(0, 0) toView:nil];
        float bx, by, bw, bh;

        c = [win convertBaseToScreen:c];
        PScurrentwindowbounds([win windowNumber], &bx, &by, &bw, &bh);
        srvOffX = c.x - bx;
        srvOffY = c.y - by;
        printf("glwin: server bounds %g %g %gx%g, content offset %g %g\n",
               bx, by, bw, bh, srvOffX, srvOffY);
        fflush(stdout);
    }
#endif
}

/*
 * WHERE THE WINDOW IS, BY THE SERVER'S OWN ACCOUNT.  The app-side frame
 * cache lags a server-side drag -- that lag is the trail.  The DPS operator
 * currentwindowbounds is a synchronous round trip to the server, so the
 * answer is the position the server is actually drawing the window at, now.
 * The bounds are the FRAME's; the constant offset to the content's origin
 * is measured once at setup, while the window is provably where the cache
 * says it is.
 */
#ifndef OSMGA_GLWIN_PLAIN
- (NSPoint)serverOrigin
{
    float bx, by, bw, bh;
    PScurrentwindowbounds([win windowNumber], &bx, &by, &bw, &bh);
    return NSMakePoint(bx + srvOffX, by + srvOffY);
}
#endif

- (void)tick:(NSTimer *)t
{
#ifndef OSMGA_GLWIN_PLAIN
    NSPoint p;
    long dstX, dstY;
    unsigned long verdict = 0UL;
    unsigned long srcX, srcY;
    long pw, ph;
#endif
    double t0, tC, tD, tP, tV, tS, t1, ms, nowT;
#ifdef OSMGA_GLWIN_PLAIN
    /*
     * -[NSDPSContext wait] runs the run loop in its DPS waiting mode.  If
     * that mode admits our timer, tick: can be called again before the
     * outer frame returns.  The flag is set only around that nested run
     * loop, so such a callback returns before drawing or touching counters;
     * normal timer calls and every existing early-return path are unchanged.
     */
    if (waitingForServer)
        return;
#endif
    if (!presenting)
        return;
    /*
     * And not while somebody ELSE'S window has the user.  True z-order is
     * unenforceable -- the server does not know our pixels exist, so a stamp
     * always lands on top of whatever overlaps the rectangle -- but raising
     * another application's window requires CLICKING it, and that instant
     * deactivates this one.  Stamping only while active means the raised
     * window is painted over our region by the server and stays there, which
     * is exactly what occlusion is supposed to look like; clicking our
     * window reactivates, raises it, and the animation resumes.
     */
    if (![win isVisible] || [win isMiniaturized] || [NSApp isHidden] ||
        ![NSApp isActive])
        return;
#ifndef OSMGA_GLWIN_PLAIN
    /*
     * ANYTHING WAITING IN THE EVENT QUEUE skips the frame.  The trail race
     * is the mouse-down that arrives while a frame is being drawn: the
     * server starts carrying the window at once, the app has not yet seen
     * the event, and a stamp goes to the position being vacated.  The
     * notifications cannot fire until the event is dispatched, but the
     * event is VISIBLE in the queue the moment it is pressed -- so a peek
     * (dequeue NO, distantPast: a pure look, no waiting, nothing consumed)
     * stands the frame down and lets the event dispatch first.
     */
    if ([NSApp nextEventMatchingMask:NSAnyEventMask
                           untilDate:[NSDate distantPast]
                              inMode:NSEventTrackingRunLoopMode
                             dequeue:NO] != nil) {
        evSkips++;
        return;
    }

    if (moving) {
        /*
         * willMove arrives on the title bar's MOUSE-DOWN, before anything
         * has moved -- and if the user releases without dragging, didMove
         * never comes and the freeze would be permanent.  There is no
         * button-state query in this AppKit's operator headers (checked),
         * so the tie-breaker is time: a full second of the window not
         * moving means it was a click, and the animation resumes.  A real
         * drag still resumes instantly through didMove.
         *
         * The residual risk is accepted and named: hold the title bar for
         * longer than the second and THEN drag, and a few stale stamps can
         * trail again until the drag's own notifications catch up.
         */
        NSPoint m = [self serverOrigin];

        if (!haveMv || m.x != mvX || m.y != mvY) {
            mvX = m.x; mvY = m.y; haveMv = 1;
            stillTicks = 0UL;
        } else if (++stillTicks >= 60UL) {
            moving = 0;
            havePos = 0;
        }
        moveSkips++;
        return;
    }

    p = [self serverOrigin];

    /*
     * PRESENT ONLY WHEN THE WINDOW IS STANDING STILL.
     *
     * Title-bar drags on this system are performed by the WINDOW SERVER,
     * which moves the on-screen pixels itself and repaints what the window
     * vacated.  Our present bypasses the server, so a stamp issued at a
     * position the server has already left lands on repainted screen and
     * nothing ever erases it -- that is the trail the first version drew
     * whenever a drag outran the position the app could see.
     *
     * So a frame is presented only when the position matches the PREVIOUS
     * tick's.  While the window is in motion nothing is stamped at all --
     * the server's own blit-move carries the last presented picture along
     * with the window, so it still follows perfectly, merely frozen -- and
     * the animation resumes one tick after the window settles.
     */
    if (!havePos || p.x != lastX || p.y != lastY) {
        lastX = p.x; lastY = p.y; havePos = 1;
        moveSkips++;
        return;
    }

    dstX = (long)p.x;
    dstY = (long)(screenH - (p.y + (float)GLH));

    /*
     * Clip against the screen rather than skipping.  Skipping left the
     * window empty the moment one edge crossed the screen border; the
     * kernel's present already takes a source rectangle, so the part that
     * is on the screen is presented and the rest simply is not.  Memory row
     * nought is the visual top (the projection is flipped), so trimming the
     * top of the picture and advancing srcY are the same act.
     */
    srcX = 0UL; srcY = 0UL; pw = (long)GLW; ph = (long)GLH;
    if (dstX < 0) { srcX = (unsigned long)(-dstX); pw += dstX; dstX = 0; }
    if (dstY < 0) { srcY = (unsigned long)(-dstY); ph += dstY; dstY = 0; }
    if (dstX + pw > (long)screenW) pw = (long)screenW - dstX;
    if (dstY + ph > (long)screenH) ph = (long)screenH - dstY;
    if (pw <= 0 || ph <= 0) {
        skips++;
        return;                 /* fully off-screen */
    }
#endif

    /*
     * Three clocks, not one, and this is a correction rather than a nicety.
     *
     * The frame used to be one span, and working out what was inside it
     * meant subtracting an estimated clear and an estimated present -- the
     * latter scaled from a DIFFERENT programme at a DIFFERENT size.  Cross-
     * review was right about that: an estimate subtracted from a measurement
     * is not a measurement, and the bound it produced was even stated the
     * wrong way round.  So each phase is timed where it happens.
     *
     * tC closes the clear, tD closes the drawing (glFinish is inside it, so
     * the engine really is done), and tP opens the present.  What falls
     * between tD and tP -- the event peek and the position recheck -- is in
     * neither, and is the difference between the phases and the whole.
     */
    t0 = [NSDate timeIntervalSinceReferenceDate];
    glClear((GLbitfield)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    tC = [NSDate timeIntervalSinceReferenceDate];
    glPushMatrix();
    /*
     * The teapot data carries the demo's own idea of up, and the flipped
     * frustum mirrors the image once more; 180 about x, applied before the
     * spin, is what stands it upright -- judged by eye on offline renders
     * before it was put here.  The pot sits at -6 at unit scale so the
     * spout cannot swing across the near plane (at -5 x 1.3 it did, and the
     * clip cut a notch out of the body).
     */
    glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
    glRotatef((float)(angle * 57.29578), 0.0f, 1.0f, 0.0f);
    teapot(grid, potScale, GL_FILL);
    glPopMatrix();
    glFinish();
    tD = [NSDate timeIntervalSinceReferenceDate];

#ifndef OSMGA_GLWIN_PLAIN
    /*
     * And once more AFTER the render: the teapot takes some thirty
     * milliseconds, ten times the window the old triangle left open, and a
     * drag begun inside it stamps the position being vacated.  The position
     * recheck reads the app's own frame cache, which lags a server-side
     * drag -- the queue peek does not: the press is in the queue before any
     * cache moves.  Peek first, then recheck; a frame dropped here costs
     * one tick.
     */
    if ([NSApp nextEventMatchingMask:NSAnyEventMask
                           untilDate:[NSDate distantPast]
                              inMode:NSEventTrackingRunLoopMode
                             dequeue:NO] != nil) {
        evSkips++;
        return;
    }
    {
        NSPoint q = [self serverOrigin];

        if (q.x != lastX || q.y != lastY) {
            lastX = q.x; lastY = q.y;
            moveSkips++;
            return;
        }
    }

    tP = [NSDate timeIntervalSinceReferenceDate];
    if (OSMGAMesaBufferPresentRect(srcX, srcY, (unsigned long)pw,
                                   (unsigned long)ph,
                                   dstX, dstY, &verdict) != 0) {
        presenting = 0;
        [win setTitle:[NSString stringWithFormat:
                          @"OpenGL: present refused (%lu)", verdict]];
        return;
    }
    t1 = [NSDate timeIntervalSinceReferenceDate];
#else
    [view copyFromBGRA:(unsigned char *)buf];
    tV = [NSDate timeIntervalSinceReferenceDate];
    /*
     * Submission is exactly lock focus, draw the bitmap rep, unlock focus,
     * and flush the window.  OPENSTEP 4.2's NSDPSContext.h then gives us the
     * real per-call fence: -wait pings the server and waits for its reply.
     * Keep the return from flushWindow as tS so submission and server wait
     * remain separate measurements.
     *
     * Whatever the wait measures is stock system-memory-to-screen delivery;
     * it is not Mesa rasterisation.  Mesa's draw phase was already completed
     * separately while it wrote the system-memory OSMesa buffer.
     */
    [view lockFocus];
    [view drawBitmap];
    [view unlockFocus];
    [win flushWindow];
    tS = [NSDate timeIntervalSinceReferenceDate];
    waitingForServer = 1;
    [[NSDPSContext currentContext] wait];
    waitingForServer = 0;
    t1 = [NSDate timeIntervalSinceReferenceDate];
#endif

    angle += 0.0261799;              /* one and a half degrees */
    ms = (t1 - t0) * 1000.0;
    frames++;
    sumMs += ms;
    winClrMs += (tC - t0) * 1000.0;
    winDrwMs += (tD - tC) * 1000.0;
#ifndef OSMGA_GLWIN_PLAIN
    winPrsMs += (t1 - tP) * 1000.0;
#else
    winCvtMs += (tV - tD) * 1000.0;
    winAppMs += (tS - tV) * 1000.0;
    winWaitMs += (t1 - tS) * 1000.0;
#endif
    totClrMs += (tC - t0) * 1000.0;
    totDrwMs += (tD - tC) * 1000.0;
#ifndef OSMGA_GLWIN_PLAIN
    totPrsMs += (t1 - tP) * 1000.0;
#else
    totCvtMs += (tV - tD) * 1000.0;
    totAppMs += (tS - tV) * 1000.0;
    totWaitMs += (t1 - tS) * 1000.0;
#endif
    if (ms < minMs) minMs = ms;
    if (ms > maxMs) maxMs = ms;

    nowT = t1;

    /*
     * The half-second title.
     *
     * Two numbers, and they are not the same measurement.  The rate is
     * frames divided by WALL-CLOCK seconds, so everything is in it -- the
     * timer's own gaps, the event peek, and setting this very title.  The
     * milliseconds beside it are the mean of the timed span only (clear,
     * draw, finish, and either accelerated present or stock conversion plus
     * AppKit submission and the DPS server wait).  Where the two disagree,
     * the difference is what the frame costs outside the measured work.
     *
     * Set AFTER t1 on purpose: a title set inside the timed span would put
     * the window server's work into the figure the title reports.  It still
     * costs wall-clock, which is exactly why the rate is measured that way
     * and not as 1000/ms.
     */
    winFrames++;
    winSumMs += ms;
#ifndef OSMGA_GLWIN_PLAIN
    /* one-shot diagnostic: what did the very first submissions come back as? */
#ifdef OSMGA_MESA_TESTHOOKS
    if (measureArm == 3 && !toldRefusal && frames == 60UL) {
        /* Arm B has to be checked POSITIVELY: a dry submission that the
         * kernel refused would time a validation that never happened. */
        toldRefusal = 1;
        printf("glwin: arm B check: %lu dry submissions, last status %lu "
               "(0 = the kernel validated and encoded)\n",
               OSMGAMesaHookDryCount(), OSMGAMesaHookDryStatus());
        fflush(stdout);
    }
#endif /* OSMGA_MESA_TESTHOOKS */
#endif
    if (nowT - winStart >= 0.5) {
        double el;
        double n;
        double timedMs;
        NSString *mode;

        el = nowT - winStart;
        n = (double)winFrames;
        timedMs = winSumMs / n;
#ifdef OSMGA_GLWIN_PLAIN
        mode = @"stock";
#else
        mode = forcedSoftware ? @"SOFTWARE" : @"hardware";
#endif

        /*
         * The rate is frames over WALL-CLOCK seconds, so the timer's gaps,
         * the event peek and this very title are all in it.  The three
         * phases beside it are means of their own measured spans.  The
         * title calls the span "timed" so it cannot be read as 1000/rate.
         * The terminal report prints both periods and their difference.
         */
#ifdef OSMGA_GLWIN_PLAIN
        [win setTitle:[NSString stringWithFormat:
            @"%@ wall %.1f fps -- timed %.2f ms -- clear %.2f draw %.2f convert %.2f appkit-submit %.2f server-wait %.2f",
            mode, n / el, timedMs, winClrMs / n, winDrwMs / n,
            winCvtMs / n, winAppMs / n, winWaitMs / n]];
#else
        [win setTitle:[NSString stringWithFormat:
            @"%@ wall %.1f fps -- timed %.2f ms -- clear %.2f draw %.2f present %.2f",
            mode, n / el, timedMs, winClrMs / n, winDrwMs / n,
            winPrsMs / n]];
#endif
        winFrames = 0UL; winSumMs = 0.0; winStart = nowT;
        winClrMs = 0.0; winDrwMs = 0.0; winPrsMs = 0.0;
        winCvtMs = 0.0; winAppMs = 0.0; winWaitMs = 0.0;
    }

    if (nowT - lastReport >= 5.0) {
        double wallElapsed;
        double wallFps;
        double wallMs;
        double timedMs;
        double unaccountedMs;

        wallElapsed = nowT - wallStart;
        wallFps = (double)frames / wallElapsed;
        wallMs = 1000.0 / wallFps;
        timedMs = sumMs / (double)frames;
        unaccountedMs = wallMs - timedMs;
#ifdef OSMGA_GLWIN_PLAIN
        printf("glwin: stock %lu frames, wall %.2f fps / %.2f ms per frame, "
               "timed span %.2f ms (clear %.2f draw %.2f convert %.2f "
               "appkit-submit %.2f server-wait %.2f), unaccounted %.2f ms, "
               "timed-span min %.2f max %.2f\n",
               frames, wallFps, wallMs, timedMs,
               totClrMs / (double)frames, totDrwMs / (double)frames,
               totCvtMs / (double)frames, totAppMs / (double)frames,
               totWaitMs / (double)frames,
               unaccountedMs, minMs, maxMs);
#else
        printf("glwin: %s %lu frames, wall %.2f fps / %.2f ms per frame, "
               "timed span %.2f ms (clear %.2f draw %.2f present %.2f), "
               "unaccounted %.2f ms, timed-span min %.2f max %.2f, "
               "%lu offscreen, %lu while moving, %lu queue-peek\n",
               forcedSoftware ? "SOFTWARE" : "hardware",
               frames, wallFps, wallMs, timedMs,
               totClrMs / (double)frames, totDrwMs / (double)frames,
               totPrsMs / (double)frames,
               unaccountedMs, minMs, maxMs,
               skips, moveSkips, evSkips);
        {
            const OSMGAMesaRefusal *r = OSMGAMesaHookLastRefusal();
            if (r != 0 && (r->status != 0UL || r->verdict != 0UL))
                printf("glwin: last refusal: status %lu verdict %lu tri %lu of %lu\n",
                       r->status, r->verdict, r->triangle, r->triCount);
        }
        printf("glwin: grid %d scale %.2f, per frame: drawn %.1f traps %.1f batches %.1f\n",
               grid, potScale,
               (double)OSMGAMesaHookDrawn()   / (double)frames,
               (double)OSMGAMesaHookTraps()   / (double)frames,
               (double)OSMGAMesaHookBatches() / (double)frames);
        {
            /*
             * The kernel answers every submission -- dry ones too -- with the
             * length of the list it encoded and the number of times it spun
             * waiting for the engine.  Both come back through the same block,
             * so arm B reports the real encoded size while running no engine
             * at all.  That is what separates the barrier read (host work,
             * proportional to dwords) from the wait (engine time, spins).
             */
            unsigned long st[6];
            OSMGAMesaHookSubmitStats(st);
            printf("glwin: per frame: dwords %.0f (%.1f per trap), "
                   "spins %.0f (max %lu, %.0f of %.0f submits spun)\n",
                   (double)st[2] / (double)frames,
                   OSMGAMesaHookTraps() ? (double)st[2] /
                                          (double)OSMGAMesaHookTraps() : 0.0,
                   (double)st[3] / (double)frames, st[4],
                   (double)st[5] / (double)frames,
                   (double)st[0] / (double)frames);
        }
#endif
        fflush(stdout);
        lastReport = nowT;
    }
}

/*
 * The server tells us when a title-bar drag BEGINS and ENDS, and that
 * bracket is the real fix for the trails: the stillness gate below can only
 * see positions the app has been told about, and a drag whose updates
 * arrive late looks exactly like standing still -- so the gate happily
 * stamped the old spot while the window was elsewhere.  From willMove to
 * didMove nothing is stamped at all; the server's own blit-move carries the
 * last frame along with the window.
 */
- (void)windowWillMove:(NSNotification *)n
{
#ifndef OSMGA_GLWIN_PLAIN
    moving = 1;
    stillTicks = 0UL;
    haveMv = 0;
#endif
}

- (void)windowDidMove:(NSNotification *)n
{
#ifndef OSMGA_GLWIN_PLAIN
    moving = 0;
    havePos = 0;                /* re-learn the position before presenting */
#endif
}

- (void)windowWillClose:(NSNotification *)n
{
    if (timer) { [timer invalidate]; timer = nil; }
    presenting = 0;
#ifndef OSMGA_GLWIN_PLAIN
    OSMGAMesaBufferPresentMode(0);
#endif
    if (ctx) { OSMesaDestroyContext(ctx); ctx = 0; }
#ifdef OSMGA_GLWIN_PLAIN
    printf("glwin: stock closed after %lu frames\n", frames);
#else
    printf("glwin: closed after %lu frames (%lu skipped)\n", frames, skips);
#endif
    [NSApp terminate:nil];
}

@end

int
main(int argc, const char *argv[])
{
    NSAutoreleasePool *pool;
    NSApplication *app;
    GLWinController *ctrl;

    pool = [[NSAutoreleasePool alloc] init];
    app = [NSApplication sharedApplication];
    ctrl = [[GLWinController alloc] init];
#ifdef OSMGA_GLWIN_PLAIN
    if (argc > 1) {
        fprintf(stderr, "glwin_sw: this build takes no arguments\n");
        return 2;
    }
#else
    /*
     * One argument, and it is read before setup because setup is where the
     * back end is told.  Anything else is refused rather than ignored: a
     * typo that silently gave the hardware path would be compared against
     * the hardware path and called a result.
     */
    if (argc > 1) {
        if (strcmp(argv[1], "soft") == 0) {
            [ctrl setForcedSoftware:1];
#ifdef OSMGA_MESA_TESTHOOKS
        } else if (strcmp(argv[1], "armC") == 0) {
            /* build trapezoids, never submit -- the picture stays blank */
            [ctrl setMeasureArm:1];
        } else if (strcmp(argv[1], "armB") == 0) {
            /* the kernel validates and encodes, then stops -- needs a driver
             * that answers OSMGA_IOC_SUBMIT_DRY */
            [ctrl setMeasureArm:3];
        } else if (strcmp(argv[1], "armD") == 0) {
            /* return before the builder -- blank too */
            [ctrl setMeasureArm:2];
#endif /* OSMGA_MESA_TESTHOOKS */
        } else if (argv[1][0] >= '1' && argv[1][0] <= '9') {
            [ctrl setGrid:atoi(argv[1])];
        } else {
            /*
              * The arms are named here only when they exist.  A release
              * build of this demo links the release library, which has no
              * selector to set -- so offering "armB" in the usage of a
              * binary that would reject it is worse than not offering it.
              */
#ifdef OSMGA_MESA_TESTHOOKS
            fprintf(stderr, "glwin: soft | armB | armC | armD | "
                            "<grid> [<scale%%>]\n");
#else
            fprintf(stderr, "glwin: soft | <grid> [<scale%%>]\n");
#endif
            return 2;
        }
        if (argc > 2 && argv[2][0] >= '1' && argv[2][0] <= '9')
            [ctrl setGrid:atoi(argv[2])];
        /* an optional trailing scale, as a percentage so the shell need not
         * carry a decimal point through csh and sh alike */
        if (argc > 3)
            [ctrl setPotScale:atof(argv[3]) / 100.0];
    }
#endif
    [ctrl setup];
    [app activateIgnoringOtherApps:YES];
    [app run];
    [pool release];
    return 0;
}
