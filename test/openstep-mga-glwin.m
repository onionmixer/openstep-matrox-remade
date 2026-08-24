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
#import <Foundation/Foundation.h>
#include <math.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "../mesa/OpenStepMGAMesaHook.h"
#include "../mesa/OpenStepMGAMesaBuffer.h"
#include "OpenStepMGAHW3D.h"

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
    unsigned long frames, skips;
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
     * row and a blit cannot flip, so the projection does. */
    glOrtho(0.0, (double)GLW, (double)GLH, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);
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
}

- (void)tick:(NSTimer *)t
{
    NSPoint p;
    long dstX, dstY;
    double t0, t1, ms, nowT;
    unsigned long verdict = 0UL;
    double cx = GLW / 2.0, cy = GLH / 2.0, r = 180.0;

    if (!presenting)
        return;
    if (![win isVisible] || [win isMiniaturized] || [NSApp isHidden])
        return;

    p = [view convertPoint:NSMakePoint(0, 0) toView:nil];
    p = [win convertBaseToScreen:p];
    dstX = (long)p.x;
    dstY = (long)(screenH - (p.y + (float)GLH));
    if (dstX < 0 || dstY < 0 ||
        dstX + GLW > (long)screenW || dstY + GLH > (long)screenH) {
        skips++;
        return;                 /* partly off-screen: skip, do not clip */
    }

    t0 = [NSDate timeIntervalSinceReferenceDate];
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
      glColor3f(1.0f, 0.25f, 0.2f);
      glVertex2d(cx + r * cos(angle), cy + r * sin(angle));
      glColor3f(0.2f, 1.0f, 0.35f);
      glVertex2d(cx + r * cos(angle + 2.0944),
                 cy + r * sin(angle + 2.0944));
      glColor3f(0.25f, 0.4f, 1.0f);
      glVertex2d(cx + r * cos(angle + 4.1888),
                 cy + r * sin(angle + 4.1888));
    glEnd();
    glFinish();

    if (OSMGAMesaBufferPresent(dstX, dstY, &verdict) != 0) {
        presenting = 0;
        [win setTitle:[NSString stringWithFormat:
                          @"OpenGL: present refused (%lu)", verdict]];
        return;
    }
    t1 = [NSDate timeIntervalSinceReferenceDate];

    angle += 0.0523598;
    ms = (t1 - t0) * 1000.0;
    frames++;
    sumMs += ms;
    if (ms < minMs) minMs = ms;
    if (ms > maxMs) maxMs = ms;

    nowT = t1;
    if (nowT - lastReport >= 5.0) {
        printf("glwin: %lu frames, mean %.2f ms (min %.2f max %.2f), "
               "%lu skipped\n",
               frames, sumMs / (double)frames, minMs, maxMs, skips);
        fflush(stdout);
        lastReport = nowT;
    }
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
