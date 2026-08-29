# The Utah teapot through SDL2

The same teapot as the demo beside it, drawn through SDL2 instead of straight
OSMesa. It is here to answer one question with numbers: **what does SDL2's
delivery cost on this card, and what happens when you stop paying it?**

## Building

This demo needs SDL2, which the others do not.

```
csh -f build-sdl-teapot.csh /LocalDeveloper /path/to/Mesa-3.4.2
```

Required at the prefix:

| package | why |
| --- | --- |
| `OpenStepSDL2Libraries` and `OpenStepSDL2Headers` | **openstep.2 or later** |
| `OpenStepMesa342Libraries` | `sdlteapot_sw` |
| `OpenStepMGAMesaAccel` | `sdlteapot_hybrid` |

Earlier SDL2 releases will build but will not accelerate: openstep.2 is where
SDL2's GL backend stopped handing the accelerated surface back at every bind.

The Mesa source tree is needed only for the teapot's control points, which
are cut out at build time rather than kept in this repository. `NOTICE` says
whose they are.

## Running

```
./sdlteapot_hybrid                       800x600, SDL2's ordinary delivery
OSMGA_SDLTEAPOT_PRESENT=3 ./sdlteapot_hybrid       the direct one
./sdlteapot_hybrid 320 240               any size
OSMGA_SDLTEAPOT_FRAMES=2000 ./sdlteapot_hybrid     long enough to watch
```

`OSMGA_MESA_WARP=1` asks the driver for its setup-engine path; without it the
Configure setting decides.

## Reading the report

```
   surface is the engine's : yes
   drawn by the card       : 2252800   (batches 70400)
   of those, WARP took     : 2252800   (trapezoids 0)
   left to Mesa / refused  : 0 / 0
   surface read back       : 0 copies
```

**"It ran the hybrid binary" proves nothing.** The driver falls back whenever
the device, the mode, the mapping or the surface admission is unavailable, and
a fallback still draws a correct teapot at a software speed. A hardware frame
needs the surface claimed and the batches submitted; `warp == drawn` is what
says the card's setup engine did it rather than the trapezoid path.

`surface read back` is the number this demo was written for.

## What the numbers were

On a G450 at 800x600, 1800 frames:

| delivery | wall | fps | readbacks |
| --- | --- | --- | --- |
| SDL2's ordinary swap | 1847 ms\* | 0.54 | 32 a frame |
| the direct present | 22.77 ms | 43.91 | none |

\* measured at 320x240; at 800x600 that path is about eleven seconds a frame.

The card draws the same triangles either way. The difference is entirely that
the ordinary path walks the surface back into the caller's array — at 746 ns
a pixel, once per rendering *batch* rather than once per frame.

## What the direct present is, and is not

It is a video-memory-to-video-memory blit onto the visible screen, asked for
by the driver's kernel side. Nothing crosses the bus.

It is **not compositing**. The window server does not know those pixels
exist, so SDL2 stamps only while the window plainly has the user, and hands
the rectangle back the moment it does not — hidden, minimised, unfocused,
moved this frame, or refused. A menu or a panel can still cover the rectangle
without taking focus, and SDL2 cannot see that.

Mode 3 is the interesting one: the demo registers three driver functions with
`SDL_SetWindowData` and then runs an ordinary `SDL_GL_SwapWindow` loop. Modes
1 and 2 exist for comparison and do the presenting themselves.
