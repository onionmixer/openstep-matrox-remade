# R21 -- a moving teapot in two builds, with the frame rate on screen

Written before any code, against measurements taken on the machine at
1600x1200x32 with nothing else running.

---

## 1. What was asked

A window demo of the spinning teapot that exists in two link-time builds --
stock Mesa and accelerated -- the way `examples/build-teapot.csh` already
builds `teapot_sw` and `teapot_hybrid` from one source, with the frame rate
shown on screen and refreshed twice a second, so the two can be run side by
side and compared by eye.

## 2. The finding that shapes the whole thing

**At the scene the demo draws today, the software renderer is not slower.**

Measured, 800x600, grid 4, nothing else running:

| | |
|---|---|
| `glwin`, accelerated, one teapot + clear + finish + present | **21.00 ms** a frame |
| stock Mesa, offline, two teapots, draw only | **19 ms** -> ~9.5 ms each |

Taking the two pieces of the accelerated frame that are not drawing -- the
engine clear of the whole surface (2.65 ms at the measured 5.53 ns/pixel) and
the present blit (at most 5.30 ms, from `spin`'s 295 fps at 640x480 scaled by
area) -- leaves at most 13.05 ms of drawing for 981 triangles, against the
software renderer's 9.5 ms for the same 981.

The reason is in the geometry, and it was measured rather than guessed:

- The picture was read back and counted: the teapots cover **21.4%** of the
  frame, so a submitted triangle covers about **52 pixels** (culling is off,
  so roughly half of them are hidden and cover none).
- The engine fills at **5.53 ns/pixel**; stock Mesa's spans, backed out of a
  surface-size sweep at fixed tessellation, are of the order of 80 ns per
  covered pixel. So the engine saves roughly 75 ns for every covered pixel.
- Against that, the accelerated path costs more per triangle than the
  software one -- at most 13.3 us against 4.82 us.

Break-even is therefore around **126 pixels per triangle**, and the demo is
sitting at 52.  That is why the two are neck and neck.

### Where that per-triangle cost probably goes -- NOT established

`osmgaMesaMirror` is installed as `Driver.RenderFinish`, so it runs at every
`glBegin`/`glEnd` block, and its first act is `osmgaMesaFlushPending()`.  In
present mode the mirror itself stands down, but the **flush does not** -- and
`glEvalMesh2` gives the teapot 65 blocks at grid 4.  Sixty-five kernel round
trips a frame would account for the whole 13 ms on their own.

**REFUTED, before the review came back.**  The offline demo already takes a
batch limit, and a limit of 1 makes every triangle its own submission without
changing the scene, the tessellation or the 65 mirrors.  Measured at grid 4,
800x600:

| submissions | time |
|---|---|
| 26 (default batching) | 22.828 s |
| 1962 (limit 1) | 22.854 s |

Seventy-five times the round trips for 26 ms more, so **one round trip costs
13.4 us**.  Sixty-five of them is 0.87 ms -- four per cent of `glwin`'s
frame, not the whole of it.  The flush at every block is real and it is
cheap.  The ~13 ms is per-TRIANGLE work, and the driver has no defect here
to fix first.

A second solved figure -- 48.5 us per triangle, from the two offline
tessellations -- is **discarded as unreliable**: it comes out of a 2.6 s
residual on a 137 s measurement, where a 2% error in the mirror rate swings
it by 100%.  The 13.3 us bound above rests on `glwin`'s own frame time, which
has no such problem.

## 3. What follows for the design

A demo that draws today's scene would show the software build winning, which
is the opposite of what it is for.  So the scene is not a detail to be
inherited -- it is the first decision, and it has to be made by measurement.

The lever is pixels per triangle.  Three ways to raise it, all cheap:

1. **Scale the pot up** so it fills the window.  21.4% coverage to ~60% is
   2.8x the pixels for the same triangles.
2. **Lower the tessellation**.  Triangles go as grid squared; grid 3 gives
   0.56x the triangles of grid 4, grid 2 gives 0.25x.
3. **Turn back-face culling on.**  It is off today because the flipped
   frustum reverses winding, which is fixable with `glFrontFace`.  It removes
   about half the triangles and helps the accelerated build about 2.7x more
   than the software one, because that is the ratio of their per-triangle
   costs.

None of these is a thumb on the scale.  Filling the window with a solid
object is what hardware rasterisation is for, and drawing hidden faces is
waste in either renderer.

## 4. Proposal

**S1 -- make the scene a parameter first, and measure.**  Give the window
demo `grid`, `scale` and a cull switch on the command line, build both
flavours, and measure a small matrix on the machine.  Fix the demo at a
setting where the accelerated build is clearly ahead **and** the pot still
looks like a teapot.  If no such setting exists, that is the answer and this
plan stops here rather than shipping a demo that argues the wrong way.

**S2 -- one source, two builds.**  `-DOSMGA_GLWIN_PLAIN` selects the stock
build, exactly as `OSMGA_TEAPOT_PLAIN` does for the offline demo.

But the two are not alike, and this is the part with real work in it.  What
`OSMGA_TEAPOT_PLAIN` shims away is **counters and switches** -- the offline
demo's own comment says so, and none of them draws.  The window demo calls
exactly three back-end functions:

```
OSMGAMesaBufferOrigin       (is there an accelerated surface)
OSMGAMesaBufferPresentMode  (twice)
OSMGAMesaBufferPresentRect  (the picture reaching the screen)
```

The last two **are the on-screen delivery**, and they live only in
`libGL_mga.a`.  Shimmed to nothing, the stock build renders into its array
and shows an empty window.  So the stock build needs a delivery of its own.

**S3 -- the stock build delivers with AppKit.**  The hardware-verified
precedent is `openstep-sdl12/spikes/bitmap-presenter/BitmapPresenter.m`.
It uses a caller-owned plane in `NSBitmapImageRep`, created with
`initWithBitmapDataPlanes:...` as non-planar 24-bit calibrated RGB
(8 bits per sample, three samples, no alpha, `bytesPerRow = width * 3`), and
draws it with the inherited `drawInRect:`.  The view is flipped and the
window uses `NSBackingStoreBuffered`.  The image rep neither copies nor frees
a non-NULL caller plane, so that RGB allocation lives until after the rep.

Two conversions are explicit:

- **Byte order.**  `OSMESA_ARGB` is a 32-bit word `0xAARRGGBB` (Mesa's own
  `osmesa.c` sets ashift 24, rshift 16, gshift 8, bshift 0), which on i386 is
  the bytes B,G,R,A.  The stock build writes R,G,B and drops alpha.
- **Row order.**  OPENSTEP 4.2 hardware showed that caller buffer row zero
  appears at the bottom: the bitmap backing is bottom-up.  Glwin's flipped
  frustum makes its source row zero the visual top, so the conversion always
  writes source row zero into destination row `height - 1`, without changing
  projection, winding, or culling.

Conversion, AppKit submission (through the return from `flushWindow`), and
the WindowServer fence are timed and reported separately.  The fence is
`-[NSDPSContext wait]`, declared by OPENSTEP 4.2's `NSDPSContext.h`; it pings
the server and waits for the reply.  Because that wait runs a nested run loop
in the DPS waiting mode, the stock path rejects any timer callback that
re-enters `tick:` while the wait is active.  The arithmetic remains the test:
wall-clock frame period minus the complete timed span is still reported as
`unaccounted`.  Delivery cost is not Mesa rasterisation cost.  The stock
build's window is buffered; the accelerated one stays nonretained, for the
reason already written into that file.

**S4 -- the rate goes in the title bar**, and the same code does it in both
builds.  Updating it happens after the timed span, so its cost is captured by
the wall-clock rate rather than silently charged to a drawing phase.  It does
not alter the rendered frame -- drawing digits into the picture would add
state changes and work to the thing whose time is the whole point.

It shows the split, not one number:

```
hardware wall <fps> fps -- timed <ms> -- clear <ms> draw <ms> present <ms>
stock wall <fps> fps -- timed <ms> -- clear <ms> draw <ms> convert <ms> appkit-submit <ms> server-wait <ms>
```

The rate is frames divided by elapsed wall-clock seconds in both builds.  The
terminal report also prints its reciprocal as the real frame period, the
timed span, and their explicit difference.  Those numbers stay separate
because the builds do not deliver the same way.  Whether `server-wait`
accounts for the formerly deferred delivery cost is determined by the
measurement, not assumed in advance.

**S5 -- a rolling half-second window, not a running mean.**  The counters in
that file today accumulate from start-up and report every 5 s; a mean over
26,650 frames barely moves.  Count frames and sum milliseconds since the last
report, show, reset.

## 5. What this does not touch

The driver, the kernel, the packaging, and the offline demo.  The flush-per-
block hypothesis in section 2 is recorded, not acted on.

## 6. Open questions for cross-review

1. **Is choosing the scene by measurement honest**, or does it read as
   picking the benchmark that wins?  Section 3 argues it is the former; is
   that argument good enough, and should the demo state the setting on
   screen so a viewer can see what is being compared?
2. **Is AppKit the right delivery for the stock build**, or should it write
   the framebuffer through the driver's device node so both builds deliver
   the same way and only the rasteriser differs?  The second is a fairer
   comparison and a worse demonstration of what a program without this
   driver actually has to do.
3. **Is the 13.3 us per triangle bound sound**, given it is a subtraction of
   two estimates (clear and present) from one measurement?
4. ~~Should the flush-per-block question be answered first?~~  **Answered
   above by measurement: no.**  Left in place so the review can check the
   refutation rather than take it.

---

## 7. Falsified on the screen -- read this before section 2

Section 2 argued, from offline measurements, that at this scene the software
renderer is not slower, that the accelerated path costs more per triangle,
and that the demo's scene would have to be changed before hardware could win.

**On the on-screen path that is wrong, and the margin is not close.**

The demo grew a `soft` argument -- `OSMGAMesaHookForceSoftware(1)`, which
sends every triangle to Mesa's rasteriser and makes the back end decline the
engine clear -- and three clocks, one per phase, where the phases happen.
Both builds then differ in the rasteriser and in nothing else: same surface,
same geometry, same lighting, same evaluators, same clear coverage, same
delivery.  Measured at 800x600, grid 4, one teapot, nothing else running:

| | total | clear | draw | present |
|---|---|---|---|---|
| hardware | 20.82 ms | 3.50 | 13.41 | 3.72 |
| SOFTWARE | 174.60 ms | 54.32 | 116.42 | 3.68 |
| ratio | **8.4x** | 15.5x | 8.7x | **0.99x** |

48.0 fps against 5.7 fps, and the difference is visible across the room.

**The present time is the control.**  3.72 against 3.68 ms: the same
VRAM-to-VRAM blit runs in both, so the whole of the difference sits in the
two phases the switch actually changes.

### Why section 2 got it wrong

It compared the on-screen accelerated path against an OFFLINE stock-Mesa
run, and the two do not write to the same place.  Stock Mesa rasterises into
system memory; the switch above has Mesa rasterising into the surface the
driver substituted, which is video memory.  Section 2's break-even model was
built on the first and applied to the second.

The measurement that would close this properly is the one this project has
still never taken: **system memory to video memory write bandwidth**.  Until
someone takes it, the honest statement of the software figure is "Mesa
rasterising into the surface the driver gave it", which is what the source
comment says.

### What this cancels

**S1 is cancelled.**  Choosing the scene by measuring a matrix was there to
find a setting where the accelerated build could win.  It wins at the scene
the demo already draws, so there is nothing to choose and nothing to
publish -- and the cross-review's objection about benchmark selection goes
with it.

### What replaced the estimates

Cross-review's one surviving objection was that section 2 subtracted
estimates from a measurement, and it was right.  The three clocks replace
all three estimates:

| | estimated | measured |
|---|---|---|
| clear | 2.65 ms, from the engine fill rate | **3.50 ms** -- 32% out |
| present | at most 5.30 ms, scaled from `spin` | **3.72 ms** -- the bound held |
| draw | at least 13.05 ms, by subtraction | **13.41 ms** |

The subtraction's direction was also stated backwards in section 2: with the
present given as an upper bound, what comes out is a LOWER bound on the
drawing, not an upper one.

---

## 8. Finished, and what the numbers turned out to be

Everything above this section was written before the stock build existed.
This section is written after it, from measurements on the machine.

### The three configurations, all at 800x600, one teapot, nothing else running

| | wall rate | clear | draw | onto the screen | unaccounted |
|---|---|---|---|---|---|
| `glwin` accelerated | **47.6 fps** (21.01 ms) | 3.48 | 13.33 | 3.70 present | 0.30 |
| `glwin soft` | 5.7 fps (174.6 ms) | 54.32 | 116.42 | 3.68 present | -- |
| `glwin_sw` stock Mesa | **12.8 fps** (78.17 ms) | 2.65 | 8.58 | 65.90 total | 1.05 |

The stock delivery breaks down as convert 2.62 + appkit-submit 0.21 +
server-wait 63.07.

### The accelerated build wins, and not for the reason anyone assumed

**Mesa's rasteriser is the fastest of the three at putting pixels somewhere**
-- 8.58 ms against the engine's 13.33.  The accelerated build is 3.7x faster
anyway, and the whole of that margin is in getting the finished picture onto
the screen: 3.70 ms for the driver's video-memory-to-video-memory blit
against 65.90 ms for the AppKit path.  **17.8x.**

So this driver's value, in this demo, is the delivery path and not the fill
rate.  That is the opposite of what section 2 predicted and of what the
`soft` switch appeared to show, and it took three separate mistakes to get
here.

### Three mistakes, in order

**One: comparing against the wrong software.**  Section 2 measured the
offline stock demo and section 7 measured the `soft` switch, and neither is
the software case a user meets.  Stock Mesa rasterises into system memory;
the `soft` switch has Mesa rasterising into the surface the driver
substituted, which is video memory, and that costs 116.42 ms instead of 8.58
-- **13.6x** for the same work.  The 8.4x margin section 7 reported was
largely an artefact of that handicap.

**Two: reading a phase sum as a frame period.**  The terminal line said
`mean 18.39 ms` and I reported 54 fps.  The person watching the screen said
it looked like 12 and they were right: the wall period was 78.12 ms and
**59.73 ms of every frame was outside the timed span entirely.**  The proof
needed no new code -- the report is emitted every 5.0 s and carries a
cumulative frame count, so consecutive lines give the wall rate directly.
They read 64 frames per report, which is 12.8 fps.

**Three: believing a submission time was a completion time.**  `flushWindow`
returns before the window server has done the work, so `appkit 2.07 ms` was
measuring how long it took to ask.  Cross-review had listed exactly this as
a risk and it happened anyway.

### What closed it

`-[NSDPSContext wait]`, at line 111 of
`/NextLibrary/Frameworks/AppKit.framework/Headers/NSDPSContext.h`.  The same
header's instance variables (`syncMode`, "ping after every wrap?", and
`stuffToPing`) say what it does.  Placed after `flushWindow` and timed on its
own, it collapsed the unaccounted column from 59.48 ms to **1.05 ms** and
moved 63.07 ms into a phase called `server-wait`, where it can be seen.

`appkit-submit` then measures 0.21 ms, which is what submitting actually
costs.

Because that wait runs a nested run loop and this demo's timer is registered
in `NSEventTrackingRunLoopMode` as well as the default, `tick:` can re-enter
itself through it.  A flag set immediately before the call and cleared
immediately after guards against timing a frame from inside another frame.

### The reporting rule this leaves behind

Every line now carries the wall-clock rate, the real frame period, the timed
span, the phases, and the difference between the period and the span.  The
last of those is the one that matters: while it was absent, a set of phase
numbers that summed to 18 ms sat next to a window visibly running at 12
frames a second and nothing in the output objected.

### Both cancellations stand

S1 (choose the scene by measuring a matrix) is cancelled: the accelerated
build wins at the scene the demo already draws.  The section 2 break-even
model is cancelled with it -- it was built on offline stock numbers and
applied to the on-screen path, and section 7's own retraction of it was
itself wrong about why.
