# Installing the Matrox G450 driver on OPENSTEP 4.2

Three packages, and you do not need all of them.

| Package | What it gives you | Needed for |
| --- | --- | --- |
| `OpenStepMGAReplacementDisplay` | the display driver: 640x480 through 1600x1200, 8/16/32-bit | a working screen on a G450 |
| `OpenStepMGAMesaAccel` | `libGL_mga.a` and its headers, into `/LocalDeveloper` | building programs that draw 3D on the card |
| `OpenStepMesa342DemosMGA` | the Mesa demos plus the teapot pair | seeing it work, and having an example to copy |

The driver is complete on its own.  The other two are for development, and
both of them need the driver installed **and active** before they do
anything.

## Before you start

- **A G450.**  The PCI ID `0x0525102B` is shared with the G400, and the G400
  path is not implemented.  If `Configure.app` offers the driver on a G400 it
  is matching that shared ID, not claiming support.
- **Intel.**  Every package refuses to install on anything but i386.
- **Root.**  The driver package asks for authorisation.
- **A way back in.**  Read the recovery section below BEFORE you activate
  anything.  Activating a display driver is the one step in this that can
  cost you the screen.

## 1. The driver

Open `OpenStepMGAReplacementDisplay.pkg` in `/NextAdmin/Installer.app`,
authorise, install.  It writes one bundle:

```
/private/Drivers/i386/OpenStepMGAReplacementDisplay.config/
```

**Installing does not activate.**  Nothing changes on screen and nothing
changes at the next boot until you do step 2.  That is deliberate: it gives
you a machine that still boots normally with the new driver sitting on disk,
which is the safe state to prepare a recovery from.

### The switches ship OFF

The instance table the package installs has `Raster Test`, `VRAM Mmap` and
`Mesa Acceleration` all set to `No`, and `Display Mode` set to 1024x768 at
60 Hz, 32-bit.  Those are not the values this project develops against; the
development instance turns three diagnostics on, and none of them should
arrive on somebody else's machine.  You turn on what you need in step 3.

## 2. Activating it, and getting back if it goes wrong

### First, save the file that decides

```
cp /private/Drivers/i386/System.config/Instance0.table \
   /private/Drivers/i386/System.config/Instance0.table.before-mga
```

That file holds `"Active Drivers"`, a space-separated list of the drivers the
system loads at boot.  Activating the display driver means its name joins
that list; recovering means the saved copy goes back.  Do the copy from a
shell, not from `Configure.app`, and do it before you touch anything.

**If the machine is on a network, know how to reach it now** -- `telnet` as
root, or another way in that does not involve looking at the screen.  This
project was developed that way throughout, and a shell over the network is
the difference between a five-minute recovery and a rescue.

### Then activate

In `Configure.app`, choose Display, add `OpenStep MGA Replacement Display`,
save, and reboot.  Configure.app rewrites `Instance0.table` for you.

### If the screen comes up black or scrambled

Nothing is damaged; the machine has simply been told to drive the card in a
way it did not like.  In order of preference:

1. **Log in over the network**, restore the saved file, reboot:
   ```
   cp /private/Drivers/i386/System.config/Instance0.table.before-mga \
      /private/Drivers/i386/System.config/Instance0.table
   reboot
   ```
2. **Boot single-user** and do the same.  (This is the standard OPENSTEP
   route rather than something this project provides; it was not exercised
   during development, because the network route always sufficed.)
3. **Boot from install media** and edit the file on the disk.

The bundle itself can stay installed.  It is inert once its name is out of
`"Active Drivers"`.

### Choosing a different resolution

`Display.modes` in the bundle offers 20 combinations -- 640x480, 800x600,
1024x768, 1280x1024 and 1600x1200, each in 32-bit colour, 16-bit colour,
8-bit colour and 8-bit greyscale, all at 60 Hz.  `Configure.app` lists them.

Greyscale additionally takes a **Gray Levels** setting of 256, 16, 4 or 2,
chosen from the radio buttons in the same inspector.  It is not part of the
mode because it is not part of the mode: all four are the same 8bpp scanout
with the same row length, and only the palette differs.  Four levels is the
picture the stock VGA driver gives at `BW:2`.  If a mode does not come up, the way back is the
same as above.

## 3. The acceleration package

Only after the driver is active and the screen is working.

Install `OpenStepMGAMesaAccel.pkg`.  It is **relocatable**: the Installer
asks where to put it, and `/LocalDeveloper` is the default, matching the Mesa
port's own Libraries and Headers packages.  Put it where those went.

```
/LocalDeveloper/Libraries/libGL_mga.a
/LocalDeveloper/Headers/OpenStepMGAMesaHook.h
/LocalDeveloper/Headers/OpenStepMGAMesaBuffer.h
/LocalDeveloper/Headers/OpenStepMGAHW3D.h
/LocalDeveloper/Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/{COPYRIGHT,COPYING,README.Mesa}
/LocalDeveloper/Documentation/OpenStep-MGA-Accel/{PORT-NOTES.md,LICENSE,NOTICE}
```

It adds a library; it replaces none.  `libGL.a` and `libGLU.a` from the Mesa
port are untouched, and every program you have already built keeps behaving
exactly as it does today, because these are static archives.

Then turn on the two switches in `Configure.app`, on the display's instance:

- `VRAM Mmap` = `Yes` -- publishes the offscreen video memory as a character
  device, which is how a program gets a buffer on the card.
- `Mesa Acceleration` = `Yes` -- lets the driver report the capability.

**`VRAM Mmap` carries one commitment**: once it is on and a program has
mapped the window, the driver must not be unloaded, because those mappings
outlive it.  In practice this means: do not `kl_util` the driver out from
under a running 3D program.  Rebooting is fine.

Reboot for the switches to take.

### The display mode decides whether acceleration is possible at all

This catches people out, so it is worth saying before you spend an evening on
it: **at 1600x1200 in 32-bit colour there is no hardware 3D**, and no setting
turns it on. The visible image plus the driver's guard rows already fill the
memory the offscreen window would need, so the window is never published and
every OpenGL program renders in software.

The same is true of every mode that is not 32-bit colour — 16bpp, 8bpp and
both greyscale modes report the 3D path as unavailable by design.

At 32-bit colour:

| Display mode | Hardware 3D | Largest accelerated buffer |
| --- | --- | --- |
| 640x480 | yes | 1280x1024 |
| 800x600 | yes | 1280x1024 |
| **1024x768** | yes | 1280x1024 — **most room for textures** |
| 1280x1024 | yes | 1024x768 |
| 1600x1200 | **no** | — |

If you want the driver's own answer rather than this table, build and run
`openstep-mga-caps-client.c` from the driver sources: it prints the four
capability bits and names the one that is missing.

## 4. The demos

`OpenStepMesa342DemosMGA.pkg` is the Mesa port's Demos package with two
directories added, `Examples/Mesa342/Teapot` and `Examples/Mesa342/GLWindow`.
Install it to the same prefix.
It is a **variant** of `OpenStepMesa342Demos` -- install one or the other,
not both.

```
cd /LocalDeveloper/Examples/Mesa342/Teapot
./teapot_sw
./teapot_hybrid
```

`teapot_sw` contains no Matrox code at all and runs anywhere Mesa runs; if it
draws a teapot, Mesa works.  `teapot_hybrid` uses the card where it can and
Mesa where it cannot, and prints the split.  Running both is the fastest way
to tell a Mesa problem from a driver problem.  `README_teapot.md` beside them
explains the report line by line.

`teapot_hybrid` runs whether or not any of this is installed -- with no
driver it says so and renders entirely in software.  You do not need to
uninstall anything to compare.

The second directory is the same idea on the screen instead of in a file:

```
cd /LocalDeveloper/Examples/Mesa342/GLWindow
./glwin_hybrid
./glwin_sw
```

A teapot spins in an 800x600 window and the title bar reports, twice a
second, the frame rate and where the time went.  Measured on a G450 at that
size: 47.6 frames a second for `glwin_hybrid` against 12.8 for `glwin_sw`.

**This pair is not like the teapot pair.**  `glwin_hybrid` needs the driver
for its DELIVERY and not only for its drawing -- its picture is built in
video memory and reaches the screen by a kernel blit.  With no driver it
opens the window, puts "no accelerated surface" in the title, and stays
empty.  `glwin_sw` is the one that runs anywhere.  `README_glwin.md` beside
them explains the title bar and says what the numbers do and do not prove.

## Uninstalling

`Installer.app` removes packages.  Take the driver's name out of
`"Active Drivers"` and reboot BEFORE removing the driver package; removing
the bundle from under an active driver leaves the system booting toward
something that is not there.

Removing `OpenStepMGAMesaAccel` removes only the extra archive, its headers
and its documentation.  Stock Mesa and the driver are untouched, and nothing
needs restoring, because nothing was displaced.
