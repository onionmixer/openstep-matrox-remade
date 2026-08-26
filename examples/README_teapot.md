# The Utah teapot, drawn by the Matrox G450

This is the demo that shows the whole accelerated stack doing something you
can look at, rather than reporting a number. Mesa's evaluators produce the
geometry, Mesa's lighting colours the vertices, this project's back end turns
the triangles into hardware trapezoids, the G450's drawing engine rasterises
them with depth testing, and the finished surface comes back as a TIFF you
can open in Workspace.

It renders offscreen. There is no window: this Mesa is from 2001, the
accelerated path is OSMesa, and OSMesa has no window-system binding. You run
the program, it writes an image, you open the image.

## Two binaries, one source

The package ships the demo twice, built from the same file:

| | needs | what it proves |
| --- | --- | --- |
| **`teapot_sw`** | nothing but Mesa | Mesa works.  It contains no Matrox code at all. |
| **`teapot_hybrid`** | nothing to RUN | the acceleration works — or tells you why it does not |

`teapot_hybrid` draws each triangle on the G450 where the hardware can take
it and in Mesa's software rasteriser where it cannot, which is what "hybrid"
means, and prints the split at the end.

**`teapot_hybrid` runs even with no driver installed.**  The accelerated
library is statically linked into it, and if `/dev/osmgavram` is not there
the probe answers "no device" and the whole scene goes to Mesa.  Measured on
the target: with acceleration unavailable it exits 0, says `NO -- software
only`, and writes a file **byte-identical** to `teapot_sw`'s.  So the second
binary is not a fallback for a broken first one — it is there so this demo
package stays a Mesa demo, and so that running both tells you at once
whether a problem is Mesa's or the driver's.

## Running them

```
./teapot_sw
./teapot_hybrid
```

Each writes `/tmp/teapot.tiff` by default — give a path as the first
argument to keep both. Open the file from Workspace, or with any TIFF
viewer.

`teapot_hybrid` opens its report with the line that matters:

```
   surface is the engine's : yes
```

`yes` means you are drawing in video memory with the engine.  `NO --
software only` means something declined, and the demo still draws the
teapot.  The usual causes, in order:

1. **The display mode is not 32-bit colour.**  This is the most common one
   and the least obvious.  The driver reports the 3D path as ready only at
   `RGB:888/32`; at 16bpp, 8bpp and both greyscale modes it reports "not
   ready" and everything falls back to software, with the driver and the
   switches perfectly healthy.  Measured on a G450 running
   `1600x1200 BW:8`:

   ```
   ENABLED yes  MMAP yes  CMD yes  READY NO
   VERDICT: software (missing 00000008)
   ```

   Switch the display to 32-bit colour in `Configure.app` and reboot.
2. **The display mode is 1600x1200 at 32-bit colour.**  There is no hardware
   3D in that mode either, and no setting turns it on: the visible image
   plus the driver's guard rows already fill the memory the offscreen window
   would need, so the window is never published.  The system log says so:
   `S4a: no usable offscreen window for this mode (start=9322496
   end=7340032), device NOT registered`.  1024x768 at 32-bit colour is the
   mode with the most room left over.
3. **`OpenStepMGAReplacementDisplay` is not the active display driver.**
   Install it, add it to Active Drivers with `Configure.app`, reboot.
4. **`VRAM Mmap` and `Mesa Acceleration` are `No`.**  They ship that way on
   purpose: a display-only installation should not carry acceleration's side
   effects, and once VRAM Mmap is on the driver must never be unloaded,
   because client mappings outlive it.  Set both to `Yes` in Configure and
   reboot.
5. **`OSMGA_MESA_ACCEL=0` is set in your environment.**  That switch exists
   for exactly the comparison below.

### Arguments, all optional

```
./teapot_hybrid [output.tiff] [soft] [grid] [batch-limit] [inject]
```

| Position | Default | What it does |
| --- | --- | --- |
| 1 | `/tmp/teapot.tiff` | where to write the image |
| 2 | — | the literal word `soft` forces the software rasteriser |
| 3 | `12` | evaluator grid: higher means more, smaller triangles |
| 4 | — | trapezoids per hardware submission |
| 5 | — | the literal word `inject` makes the kernel refuse every batch |

`teapot_sw` accepts the same first three; the last two need the accelerated
library and do nothing there.

## Three comparisons worth making

The demo doubles as a correctness gate, and these are the checks the project
itself runs.

**Is the accelerated picture the same picture?**

```
./teapot_hybrid /tmp/hw.tiff
./teapot_sw     /tmp/sw.tiff
```

These are **not** required to be byte-identical: hardware and software
rasterisation legitimately disagree at some edges.  Measured on the
development machine, they differ in 429 bytes of 921740 — 0.05% of the file,
at most 0.14% of the pixels, all of them at triangle edges.  What must be
true is that both show the same teapot, lit the same way, in the same place.

**Does batching change anything?**

```
./teapot_hybrid /tmp/a.tiff "" 12
./teapot_hybrid /tmp/b.tiff "" 12 1
```

A batch limit of `1` reproduces the one-triangle-per-submission behaviour the
driver had before batching existed.  These two **must** be byte-identical.
If they are not, batching changed what is drawn, which is a bug worth
reporting.

**Does the software fallback still draw the right thing?**

```
./teapot_hybrid /tmp/sw2.tiff soft
./teapot_hybrid /tmp/replay.tiff "" 12 180 inject
```

`inject` makes the kernel refuse every batch, so the library falls back and
redraws every triangle in software.  Those two must be byte-identical, and
`replayed after refusal` must equal the number of source triangles.

For reference, three paths that must all agree byte for byte, and do:
`teapot_sw`, `teapot_hybrid` with acceleration unavailable, and
`teapot_hybrid soft`.

## What the report tells you

```
   surface is the engine's : yes
   surface walked back     : 1 times
   source triangles drawn  : 1928
   submissions             : 32   (batching: 60 sources per submission)
   triangles left to Mesa  : 0
   refused as unsupported  : 0
   replayed after refusal  : 0
   share drawn by the card : 100%
```

- **surface is the engine's** — whether you got a video-memory buffer at all.
  This is the acceleration switch, in one line.
- **surface walked back** — how many times the whole surface was copied back
  to ordinary memory. Copying the surface is expensive, so a small number
  here is a good sign.
- **source triangles drawn** — how many of your triangles the engine drew.
- **submissions** — how many times the driver handed the engine a command
  list, and how many triangles rode in each. Fewer, larger submissions are
  faster.
- **triangles left to Mesa** — drawn correctly, but in software. A few is
  normal: some geometry is outside what the engine can express.
- **refused as unsupported** — geometry the back end declined before it ever
  reached the kernel. Also normal in small numbers.
- **replayed after refusal** — triangles the kernel refused and the library
  redrew in software. Should be `0` or very small; with `inject` it should
  equal the total.
- **share drawn by the card** — the headline: what fraction of the scene the
  hardware did.

`teapot_sw` prints none of these.  It says so, rather than printing zeros:

```
   built against           : the stock Mesa library, so this is Mesa's own rasteriser
   counters                : none -- the stock library has no hook to count with
```

Reporting "0 triangles drawn by the card" would be the kind of lie a report
exists to prevent — there is no card in that build to count.

## Rebuilding them

The prebuilt binaries beside this file need nothing but the packages they
were built against. Rebuilding needs one more thing, and it is worth
explaining why.

```
csh -f build-teapot.csh [-sw | -hybrid] [prefix] <mesa-source-root>
```

With no flag it builds both. `-sw` needs only the Mesa Libraries package at
the prefix; `-hybrid` needs `OpenStepMGAMesaAccel` there as well. `MESASRC`
in the environment works instead of the last argument.

The teapot's control points and its evaluator loop come from `tea.c` in the
Mesa source tree — the same code that implements `glutSolidTeapot`. That file
has two owners, and which one applies depends on where in it you look. Lines
1–529 are Thorsten Ohl's MesaWS widget demo, under GPL v2. From line 531 to
the end of the file it reads `Copyright (c) Mark J. Kilgard, 1994` followed
by `(c) Copyright 1993, Silicon Graphics, Inc.` and that company's grant —
permission to use, copy, modify and distribute for any purpose and without
fee, provided the copyright notice appears in all copies and both the
copyright and permission notices appear in supporting documentation, and
that the name of Silicon Graphics, Inc. is not used in advertising or
publicity about distribution without written permission.

The cut is lines **581–730**: `patchdata`, `cpdata` and `teapot()`, and
nothing else. It is inside the second block, so the GPL half is never
touched, and the excerpt — and both binaries that embody it — may be
redistributed under SGI's terms. The full notice is in the `NOTICE` file
beside this one, which is what those terms require.

`teapot-geometry.h` itself is still not committed and not packaged. That is
housekeeping, not a licence bar: it keeps copied upstream source out of the
repository, and it is why rebuilding needs a Mesa source tree while running
the shipped binaries does not.

So to rebuild you need an unpacked **Mesa 3.4.2** source tree:

```
csh -f build-teapot.csh /LocalDeveloper /usr/local/src/Mesa-3.4.2
```

The script checks that `widgets-mesa/demos/tea.c` is there and that the cut
actually produced control points before it compiles anything, so a wrong path
gives a clear message rather than a confusing compiler error.

If you only want to *run* the demos, ignore all of this — both binaries are
already built.

## If something goes wrong

**`NO -- software only`** — see the three causes listed under "Running
them". The demo still draws the teapot; only the report changes.

**The image is written but looks wrong** — run `teapot_sw` and compare. If
`teapot_sw` is right and `teapot_hybrid` is not, that is a driver bug, and
those two files are exactly the evidence to report. If BOTH are wrong, the
problem is above the driver.

**`teapot_hybrid` is much slower than `teapot_sw`** — check `share drawn by
the card`. If it is low the hardware is declining most of the scene, and
`refused as unsupported` says how much of that was geometry the back end
would not take.

**Nothing is written at all** — the program prints why. `no context` means
OSMesa could not create a context; `no room` means the buffer allocation
failed.

**The machine draws the teapot and then the display misbehaves** — the
display driver's recovery path is the boot prompt: type `config=Default` at
`boot:` to come up with the original display driver.
