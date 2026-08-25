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

## Before you start

You need **two** things installed, and this demo will tell you politely if
either is missing:

1. **`OpenStepMGAReplacementDisplay`** — the display driver, installed into
   `/private/Drivers/i386` and activated. Without it there is no engine to
   talk to.
2. **`OpenStepMGAMesaAccel`** — the accelerated library, installed at a
   prefix (`/LocalDeveloper` by default). This is the demo's `libGL`.

The driver alone gives you a working 2D display. Acceleration is something a
program opts into by linking the accelerated library and asking for a
video-memory buffer, which is exactly what this demo does — so the demo needs
both packages, while your desktop needs only the first.

You will also want the acceleration switches on. In `Configure.app`, select
the display driver and set **VRAM Mmap** and **Mesa Acceleration** to `Yes`,
then reboot. They ship as `No` because a display-only installation should not
carry acceleration's side effects — in particular, once VRAM Mmap is on the
driver must not be unloaded, because client mappings outlive it.

## Running it

```
./teapot
```

That writes `/tmp/teapot.tiff` and prints a short report. Open the file from
Workspace, or with any TIFF viewer.

The first line of the report is the one that matters:

```
   surface is the engine's : yes
```

`yes` means you are drawing in video memory with the engine. `NO -- software
only` means something in the chain declined — most often the two Configure
switches above, or the driver not being the active display driver.

### Arguments, all optional

```
./teapot [output.tiff] [soft] [grid] [batch-limit] [inject]
```

| Position | Default | What it does |
| --- | --- | --- |
| 1 | `/tmp/teapot.tiff` | where to write the image |
| 2 | — | the literal word `soft` forces the software rasteriser |
| 3 | `12` | evaluator grid: higher means more, smaller triangles |
| 4 | — | trapezoids per hardware submission |
| 5 | — | the literal word `inject` makes the kernel refuse every batch |

The last three are there because this demo doubles as a correctness gate,
and you can use them the same way the project does:

**Is the accelerated picture the same picture?**

```
./teapot /tmp/hw.tiff
./teapot /tmp/sw.tiff soft
```

Compare the two files. They are not required to be byte-identical — hardware
and software rasterisation legitimately differ at some edges — but they must
show the same teapot, lit the same way, in the same place.

**Does batching change anything?**

```
./teapot /tmp/a.tiff "" 12
./teapot /tmp/b.tiff "" 12 1
```

A batch limit of `1` reproduces the one-triangle-per-submission behaviour the
driver had before batching existed. These two files **must** be byte
identical. If they are not, batching has changed what is drawn, which is a
bug worth reporting.

**Does the software fallback still draw the right thing?**

```
./teapot /tmp/sw.tiff soft
./teapot /tmp/replay.tiff "" 12 180 inject
```

`inject` makes the kernel refuse every batch it is given, so the library
falls back and redraws every triangle in software. Those two files must be
byte identical, and the report's `replayed after refusal` count must equal
the number of source triangles.

## Rebuilding it

The prebuilt `teapot` beside this file needs nothing but the two packages.
Rebuilding needs one more thing, and it is worth explaining why.

```
csh -f build-teapot.csh [prefix] <mesa-source-root>
```

The teapot's control points and its evaluator loop come from `tea.c` in the
Mesa source tree — the same code that implements `glutSolidTeapot`. That file
is GPL as a whole, while the teapot inside it is Mark Kilgard's under GLUT's
own terms. Rather than decide what a copied fragment would carry, this
project never copies it: the build cuts the geometry out of a Mesa source
tree at build time, and nothing of it is committed to the repository or
placed in any package.

So to rebuild you need an unpacked **Mesa 3.4.2** source tree, and you point
the script at it:

```
csh -f build-teapot.csh /LocalDeveloper /usr/local/src/Mesa-3.4.2
```

The script checks that `widgets-mesa/demos/tea.c` is there and that the cut
actually produced control points before it compiles anything, so a wrong path
gives you a clear message rather than a confusing compiler error.

If you only want to *run* the demo, ignore all of this — the binary is
already built.

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

## If something goes wrong

**`NO -- software only`** — the driver is not active, or `VRAM Mmap` /
`Mesa Acceleration` are still `No` in Configure. Check the driver is the
active display driver and that you rebooted after changing the switches.

**The image is written but looks wrong** — try `soft` and compare. If the
software image is right and the hardware one is not, that is a driver bug and
the two files are exactly the evidence to report.

**Nothing is written at all** — the program prints why. `no context` means
OSMesa could not create a context; `no room` means the buffer allocation
failed.

**The machine draws the teapot and then the display misbehaves** — the
display driver's recovery path is the boot prompt: type `config=Default` at
`boot:` to come up with the original display driver.
