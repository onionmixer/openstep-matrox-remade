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
 *  - The window is NONRETAINED, and from birth: we repaint its interior
 *    every frame ourselves, a buffered backing would let the window server
 *    repaint stale darkness over the live picture, and AppKit does not allow
 *    a nonretained window to change its backing later.
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
#import <AppKit/psopsNeXT.h>
#import <Foundation/Foundation.h>
#include <math.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"
#include "../mesa/OpenStepMGAMesaBuffer.h"
#include "OpenStepMGAHW3D.h"

/* The Utah teapot, cut from the Mesa tree at build time -- same arrangement
 * as the teapot renderer, same licence reasoning (nothing committed). */
#include "teapot-geometry.h"

#define GLW 640
#define GLH 480

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

@interface GLWinController : NSObject
{
    NSWindow *win;
    GLDarkView *view;
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
    double sumMs, minMs, maxMs, lastReport;
    int presenting;
}
- (void)setup;
- (void)tick:(NSTimer *)t;
@end

@implementation GLWinController

- (void)setup
{
    NSRect wr = NSMakeRect(192, 140, GLW, GLH);
    NSRect sf = [[NSScreen mainScreen] frame];

    screenW = sf.size.width;
    screenH = sf.size.height;

    win = [[NSWindow alloc]
              initWithContentRect:wr
                        styleMask:(NSTitledWindowMask |
                                   NSClosableWindowMask |
                                   NSMiniaturizableWindowMask)
                          backing:NSBackingStoreNonretained
                            defer:NO];
    [win setTitle:@"OpenGL"];
    [win setDelegate:self];
    view = [[GLDarkView alloc] initWithFrame:
               NSMakeRect(0, 0, GLW, GLH)];
    [view setAutoresizingMask:NSViewNotSizable];
    [win setContentView:view];

    buf = (unsigned long *)malloc((unsigned)(GLW * GLH) * sizeof *buf);
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (buf == 0 || ctx == 0 ||
        !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, GLW, GLH) ||
        OSMGAMesaBufferOrigin() == 0UL) {
        [win setTitle:@"OpenGL: no accelerated surface"];
        presenting = 0;
    } else {
        OSMGAMesaBufferPresentMode(1);
        presenting = 1;
    }

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

    minMs = 1e9; maxMs = 0.0; sumMs = 0.0;
    lastReport = [NSDate timeIntervalSinceReferenceDate];

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
- (NSPoint)serverOrigin
{
    float bx, by, bw, bh;
    PScurrentwindowbounds([win windowNumber], &bx, &by, &bw, &bh);
    return NSMakePoint(bx + srvOffX, by + srvOffY);
}

- (void)tick:(NSTimer *)t
{
    NSPoint p;
    long dstX, dstY;
    double t0, t1, ms, nowT;
    unsigned long verdict = 0UL;
    unsigned long srcX, srcY;
    long pw, ph;
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

    t0 = [NSDate timeIntervalSinceReferenceDate];
    glClear((GLbitfield)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
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
    teapot(4, 1.0, GL_FILL);
    glPopMatrix();
    glFinish();

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

    if (OSMGAMesaBufferPresentRect(srcX, srcY, (unsigned long)pw,
                                   (unsigned long)ph,
                                   dstX, dstY, &verdict) != 0) {
        presenting = 0;
        [win setTitle:[NSString stringWithFormat:
                          @"OpenGL: present refused (%lu)", verdict]];
        return;
    }
    t1 = [NSDate timeIntervalSinceReferenceDate];

    angle += 0.0261799;              /* one and a half degrees */
    ms = (t1 - t0) * 1000.0;
    frames++;
    sumMs += ms;
    if (ms < minMs) minMs = ms;
    if (ms > maxMs) maxMs = ms;

    nowT = t1;
    if (nowT - lastReport >= 5.0) {
        printf("glwin: %lu frames, mean %.2f ms (min %.2f max %.2f), "
               "%lu offscreen, %lu while moving, %lu queue-peek\n",
               frames, sumMs / (double)frames, minMs, maxMs, skips,
               moveSkips, evSkips);
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
    moving = 1;
    stillTicks = 0UL;
    haveMv = 0;
}

- (void)windowDidMove:(NSNotification *)n
{
    moving = 0;
    havePos = 0;                /* re-learn the position before presenting */
}

- (void)windowWillClose:(NSNotification *)n
{
    if (timer) { [timer invalidate]; timer = nil; }
    presenting = 0;
    OSMGAMesaBufferPresentMode(0);
    if (ctx) { OSMesaDestroyContext(ctx); ctx = 0; }
    printf("glwin: closed after %lu frames (%lu skipped)\n", frames, skips);
    [NSApp terminate:nil];
}

@end

int
main(int argc, const char *argv[])
{
    NSAutoreleasePool *pool;
    NSApplication *app;
    GLWinController *ctrl;

    (void)argc; (void)argv;
    pool = [[NSAutoreleasePool alloc] init];
    app = [NSApplication sharedApplication];
    ctrl = [[GLWinController alloc] init];
    [ctrl setup];
    [app activateIgnoringOtherApps:YES];
    [app run];
    [pool release];
    return 0;
}
