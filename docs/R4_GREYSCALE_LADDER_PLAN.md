# R4 — more grey levels, without inventing pixel formats

Status: **plan, rewritten after cross-review refuted its first version.**
No code changed.

## What was asked, and what the first draft got wrong

The operator asked why the stock VGA driver does `BW:2` and this one does
not, then asked for 16-level and 2-level greyscale as well.

The first draft answered: our `BW:4` is really four greys, the platform calls
four greys `BW:2`, so rename it and add `BW:4` (16) and `BW:1` (2).

**The rename was wrong, and cross-review caught it.**  `BW:N` does not name
the number of visible greys.  It names the **packed framebuffer depth**, and
the stock driver proves it:

```c
/* CirrusLogicGD542X.m:107-121 -- "1024 x 768 x 2 x 60hz" */
1024, 768, 1024,
/*  rowbytes = (pixels/line) * (2 bits/pixel) / 8 = pixel width / 4  */
256, 60, 0, IO_2BitsPerPixel, IO_OneIsBlackColorSpace, "WW", ...
```

`rowBytes` is 256 for a 1024-wide screen -- a quarter of ours -- and the
`pixelEncoding` string is **two characters long, one per bit**.
`TsengLabsET4000.m:226` is the same.  A real `BW:2` is a quarter of the
memory and a quarter of the scanout bandwidth.

Ours is `rowBytes = width`, `IO_8BitsPerPixel`, `"WWWWWWWW"`.  Renaming it
`BW:2` would not correct a label, it would **misdescribe the ABI by a factor
of four**.  So the first draft's central claim -- "same picture, therefore
the platform's name" -- confused a visible effect with a memory format.

The same objection kills `BW:4` and `BW:1` as names for 16 and 2 levels.
All four greyscale choices are **one pixel format with four palette
presets**, and no `BW:N` name can say that.

## What this driver can and cannot do, stated once

The G450's scanout engine has no sub-byte packed format --
`CRTCEXT3 = ((1 << bppShift) - 1) | 0x80` encodes 1, 2 or 4 **bytes** per
pixel and nothing smaller (`:2625`).  The stock VGA driver reaches two bits
only through legacy PLANAR VGA at `0xA0000`, a path this driver does not
use and would have to abandon linear scanout and all acceleration to take.

So: **this driver cannot offer a real `BW:2`.**  What it can offer is 8bpp
with the RAMDAC ramp quantised, which looks the same and costs the same
memory as `BW:8`.  That is worth having, and it is worth not mislabelling.

## The design: one format, a separate level axis

Drop the invented `ColorSpace` string entirely.  `Display.modes` carries only
the five strings OPENSTEP actually uses, and the level count becomes its own
configuration key, because that is what it is -- a palette preset, orthogonal
to the mode.

| what | today | after |
| --- | --- | --- |
| `ColorSpace` strings used | `RGB:888/32`, `RGB:555/16`, `RGB:256/8`, `BW:8`, **`BW:4`** | the same minus `BW:4` -- **every string a platform string** |
| `Display.modes` entries | 25 (5 x 5) | **20** (5 x 4) |
| grey levels | fixed per format | `"Gray Levels"` = `256`, `16`, `4`, `2` |

`"Gray Levels"` applies only when the selected format is `BW:8`; it is
ignored otherwise, and the driver logs which value it took, the way every
other switch does.

What this buys over the first draft:

- every published `IODisplayInfo` stays exactly what it is today -- 8bpp,
  one-is-white, `"WWWWWWWW"`, `rowBytes = width` -- and no name contradicts
  it;
- the operator gets 256, 16, 4 and 2 levels, which is parts B and C in full;
- `Display.modes` gets SHORTER, so the Configure listing question below
  matters less;
- it composes with R3: the inspector is already going to gain a radio
  matrix, and this is a second one built the same way.

### Migration, which the first draft under-played

An existing `ColorSpace: BW:4` must keep producing **four greys**, not
sixteen and not a fallback.  Cross-review was right that "more levels is an
improvement" is not a decision the driver gets to make for somebody who
chose a look.

So `-selectModeFromConfig:` keeps recognising `BW:4` as a legacy spelling
and translates it to `BW:8` + `Gray Levels 4`, logging that it did.  The
string disappears from `Display.modes` so nobody selects it anew, and the
translation can be removed a release later.

## Open gate, before any code

**Does Configure.app list a `Display.modes` row whose ColorSpace it does not
recognise?**  Under this design the answer stops gating the feature -- all
five strings are platform strings -- but it still decides whether the legacy
`BW:4` row was ever reachable through the UI, and therefore how the
migration note should read.

Evidence gathered, and it is not conclusive:

- `IODisplayInspector.h:8` stores `colorSpace` as an unrestricted
  `char[256]`, not an enum, and the picker is a generic table column -- so
  an arbitrary string probably lists;
- but `Configure.app`'s binary holds 1815 strings and none of them are
  `ColorSpace`, `Display Mode` or `DisplayInspector`, so the parser is not
  in it and was not located anywhere on the machine;
- every stock `.modes` file uses only the five known strings, so there is no
  precedent either way.

**Settle it by looking**, and it costs a minute: the machine is booted at
640x480 `BW:4` right now.  Open Configure.app, choose the display, press
`Select...`, and see whether a `BW:4` row is listed.

## The work

1. `osmgaFmt[]` (`:996`) -- five entries become four; the `BW:4` row goes.
2. A `grayLevels` runtime value, defaulting to 0 (the linear 256 ramp), set
   from `"Gray Levels"` and from the legacy `BW:4` translation.  The ramp at
   `:3382` already branches on it and needs no edit.
3. `Display.modes` -- 25 lines become 20.
4. `Default.table` and `pkg/Instance0.release.table` gain
   `"Gray Levels" = "256"` so the shipped default is explicit.
5. The inspector gains a four-way radio matrix, built the way R3 describes.
6. Stale prose in the driver itself: `:967` ("fixed linear ramp"), `:987`
   (the `BW:4`-means-four note), `:1031` ("all twenty-five combinations"),
   `:5555` (transfer-table prose naming only `BW:8`).
7. Package metadata that counts formats:
   `pkg/OpenStepMGAReplacementDisplay.info` says "five pixel formats";
   `release-packaging/PAYLOAD_MANIFEST.md:18` says "five geometries x five
   formats" and records `Display.modes` as 1435 bytes.
8. `README.md`, `PORT-NOTES.md`, `INSTALL.md`, `RELEASE_NOTES_v1.0.md`.

## Verifier work, because the current one would not notice

`pkg/verify-driver-pkg.sh:34` only checks that `Display.modes` is PRESENT.
A package built from a stale bundle would pass with 25 entries.  Add:

- `cmp` of the packaged `Display.modes` against the source copy -- the same
  staleness rule already applied to the documents;
- an exact entry count, and that the entries are the full Cartesian product
  of the resolution and format tables with no duplicates.

## Honesty note the plan must carry

`IO_DISPLAY_HAS_TRANSFER_TABLE` is advertised for every format (`:2891`),
but for greyscale `-setTransferTable:count:` only CACHES the server's 256
entries and never pushes them to the DAC (`:5555`) -- the quantised ramp
stays live.  That predates this work and is not changed by it, but with four
selectable presets it becomes the visible contract: **the grey preset
deliberately overrides the window server's transfer table.**  Say so in
`PORT-NOTES.md` rather than leaving it to be discovered.

## Test plan

Gate first: the Configure listing check above.

Then, four boots, changing only the instance table between them:

1. `640x480 BW:8` with `Gray Levels 256` -- the smooth greyscale of today.
2. `Gray Levels 4` -- must be the picture the machine shows today at
   `BW:4`.  Same picture, different spelling, is the whole claim.
3. `Gray Levels 16` -- visibly smoother than 2.
4. `Gray Levels 2` -- pure black and white.
5. Legacy: put `ColorSpace: BW:4` back and confirm the log says it
   translated to `BW:8` + 4 levels, and the picture matches step 2.
6. Regression: `1024x768 RGB:888/32`, every driver log line unchanged.
7. `Display.modes` offers 20 rows in Configure.

Each boot also runs `openstep-mga-caps-client` and checks the published
`IODisplayInfo`: `IO_8BitsPerPixel`, one-is-white, eight `W`s,
`rowBytes == width`.  A preset that changed any of those would be the bug
this design exists to prevent.

## Corpus note

The first draft counted `ColorSpace:` strings as 80/74/63/34/6.  That is the
whole of `ref/openstep` (257 strings).  Counting only
`ref/openstep/examples` gives 31/28/19/12/2 (92 strings), which is what
cross-review measured.  Both are right for their corpus and the plan should
name which; the conclusion -- exactly five distinct strings, no `BW:4`, no
`BW:1` -- holds in both.

## Version

The 1.0 release assets are staged locally and have not been published, so
rebuilding them under 1.0 is not a re-release.  **If they have been
published anywhere, this needs a version bump instead** -- that is the
operator's to confirm before the assets are remade.
