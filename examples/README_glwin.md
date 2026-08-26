# A spinning teapot in a window, with the frame rate on the title bar

Two programs that draw the same rotating Utah teapot in an 800x600 window and
report, twice a second, how fast they are managing it and where the time
goes. Run one after the other and the difference the Matrox driver makes is
visible without reading anything.

```
glwin_hybrid   the G450 draws, and the finished picture reaches the screen
               by a video-memory-to-video-memory blit
glwin_sw       stock Mesa draws into ordinary memory, and AppKit puts the
               result on the screen
```

Both are built from one source, `openstep-mga-glwin.m`, which ships beside
them. `-DOSMGA_GLWIN_PLAIN` selects the stock form. `build-glwin.csh` builds
either or both.

## The pair is not symmetric

The offline teapot demo's two binaries both run anywhere: `teapot_hybrid`
falls back to Mesa entirely when the driver is absent and writes a file
identical to `teapot_sw`'s.

This pair is different, and the difference is worth understanding before you
run them. `glwin_hybrid` needs the driver for its **delivery**, not only for
its drawing. Its picture is built in video memory and reaches the screen by a
kernel blit. With no driver present it finds no accelerated surface, says so
in its title bar, and shows nothing.

So `glwin_sw` is the one that always runs, and `glwin_hybrid` is the one that
shows what the driver is for.

## Running them

```
./glwin_hybrid
./glwin_sw
```

`glwin_sw` takes no arguments and refuses any. Close the window to stop;
both ignore SIGTERM, so from a terminal use `kill -9`.

`glwin_hybrid` takes one optional argument, `soft`. It leaves the surface,
the geometry and the delivery exactly as they are and changes two things: the
triangles go to Mesa's rasteriser instead of the engine, and the back end
declines the engine clear so that Mesa clears as well. It is a diagnostic for
the driver's own paths, not a software comparison: see the warning at the end
of the next section.

## Reading the title bar

```
hardware  wall 47.6 fps -- timed 20.71 ms -- clear 3.48 draw 13.33 present 3.70
stock     wall 12.8 fps -- timed 77.13 ms -- clear 2.65 draw 8.58 convert 2.62
                                             appkit-submit 0.21 server-wait 63.07
```

**`wall`** is frames divided by real elapsed seconds. It is the only figure
that cannot lie about what you are looking at: the timer's gaps, the event
handling and the title update are all inside it.

**`timed`** is the span the program measures around one frame, and the phases
after it are the pieces of that span. They are printed separately because the
two builds do not deliver the same way, and one combined number would mix the
rasteriser's difference with the delivery's.

The five-second line on the terminal adds `unaccounted`, which is the wall
period minus the timed span. It exists because it was once large and wrong:
the stock build's delivery was being measured as 2 ms when 60 ms of every
frame was happening after `flushWindow` returned. If `unaccounted` is more
than a millisecond or two, the phases are not telling you the whole story.

### What the numbers say

Measured on a G450 at 800x600, one teapot, nothing else running:

| | wall | clear | draw | to the screen |
|---|---|---|---|---|
| `glwin_hybrid` | 47.6 fps | 3.48 ms | 13.33 ms | 3.70 ms |
| `glwin_sw` | 12.8 fps | 2.65 ms | 8.58 ms | 65.90 ms |

The accelerated build is 3.7 times faster, and **the reason is not the
drawing**. The stock build's whole draw phase is the shorter of the two, at
8.58 ms against 13.33. What separates them is getting the finished picture
onto the screen: 3.70 ms for the driver's blit against 65.90 ms for the
AppKit path -- of which 63.07 ms is spent waiting for the window server, and
only 0.21 ms is the application's own submission.

Read the draw figures for what they are. That phase is not rasterisation
alone: it holds the evaluators that tessellate the teapot, the transform and
the lighting, and `glFinish`, and all of those are Mesa's software in **both**
builds. They are the same work on both sides, so the 4.75 ms between the two
does belong to what differs -- rasterising and, in the accelerated build,
handing batches to the kernel. It is not a measurement of a rasteriser on its
own, and nothing here isolates one.

That is worth stating plainly because it is easy to get backwards. This
driver's value in this demo is the delivery path, not the fill rate.

### A warning about `soft`

`glwin_hybrid soft` reports about 5.7 fps, which is far worse than
`glwin_sw`'s 12.8. That is **not** a measurement of Mesa. In that mode the
driver has already substituted the drawing surface, so Mesa is rasterising
into video memory instead of ordinary memory, and it costs 116 ms a frame
instead of 8.58 -- about fourteen times more. It is a useful diagnostic for
the driver's own paths and a misleading comparison for anything else. The
honest software figure is `glwin_sw`.

## Rebuilding them

The prebuilt binaries beside this file need nothing. To rebuild you need the
Mesa 3.4.2 source tree, because the teapot geometry is cut out of it at build
time rather than shipped as a file:

```
csh -f build-glwin.csh /LocalDeveloper /path/to/Mesa-3.4.2
csh -f build-glwin.csh -sw /LocalDeveloper /path/to/Mesa-3.4.2
csh -f build-glwin.csh -hybrid /LocalDeveloper /path/to/Mesa-3.4.2
```

With no flag it builds both. There is no `both` keyword: the script takes
`-sw` or `-hybrid` or neither, and anything else in that position is read as
the prefix.

The prefix is where the Mesa and Matrox packages were installed;
`/LocalDeveloper` is the default. `glwin_sw` needs
`<prefix>/Libraries/libGL.a` from OpenStepMesa342Libraries. `glwin_hybrid`
needs `<prefix>/Libraries/libGL_mga.a` and the three headers
`OpenStepMGAMesaHook.h`, `OpenStepMGAMesaBuffer.h` and `OpenStepMGAHW3D.h`
from `<prefix>/Headers`, all of which OpenStepMGAMesaAccel installs.

## The teapot geometry

`build-glwin.csh` cuts lines 581-730 out of `widgets-mesa/demos/tea.c` in the
Mesa tree. That file has two owners: lines 1-529 are Thorsten Ohl's under GPL
v2, and from line 531 onwards it carries "Copyright (c) Mark J. Kilgard,
1994" under Silicon Graphics' 1993 permissive grant. The cut lies entirely
inside the second block, so what it takes is redistributable provided SGI's
copyright and permission notices travel with it. They are in the `NOTICE`
file in this directory, and they are also why the cut happens at build time:
nothing of `tea.c` is kept in this project's own sources.

## If something goes wrong

**`glwin_hybrid` opens a window and it stays dark, titled "no accelerated
surface".** The driver is not there, or `/dev/osmgavram` is missing. Run
`glwin_sw` instead; that is what it is for.

**The title says "present refused".** The kernel declined the blit and the
number after it is its verdict. This happens if the window is dragged
somewhere the driver will not write; move it back onto the screen.

**The teapot is upside down, or blue instead of orange.** That would be a
fault in the stock build's conversion from Mesa's pixel order to AppKit's,
and it is worth reporting: as shipped, the picture is upright and orange on
both builds.

**Nothing moves while you drag the window.** Expected. The demo stops
presenting between the window server's "will move" and "did move" so that it
does not paint over the place the window has just left.
