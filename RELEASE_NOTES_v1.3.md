# OPENSTEP Matrox G450 display driver 1.3

The driver, the Mesa 3.4.2 acceleration and the demos, for OPENSTEP 4.2 on
Intel. Install with `Installer.app`; `INSTALL.md` in the repository has the
order and the recovery route.

**This release is about mipmapping.** The WARP path now fetches mip levels
in hardware for all four mip minification filters, and the measured cost
of a full mip chain over the same scene is 0.6%.  Distant textures stop
shimmering; GLQuake runs its world at the same frame time it had without
mipmaps, filtered.

## The display driver changed this time

1.2 shipped the 1.1 driver byte for byte; 1.3 does not. The shared batch
contract moves to version 11 -- five words after the frozen v9 state carry
the chain: a map count and four absolute level origins, each validated
per level for footprint, alignment and reach. Old clients keep working:
a v10 batch falls through to the same refusal-and-replay path every other
version mismatch takes.

## What the hardware turned out to be

The qualification band measured all four engine minify modes against a
constant-green chain, at eighth-grid lambdas so a quarter-quantizing
blender could not hide, and every row of the harvest fits one model:

* lambda is the mantissa-linear log2 approximation, `e + (m - 1)`
* `MM1S` and `MM4S` ROUND the level and never blend between levels
* `MM2S` and `MM8S` blend two levels, the fraction floored to sixteenths

Which means the mode table at 9612-9614 of the June 1999 G400 spec has
`MM2S` and `MM4S` swapped, and the DRI driver's naming -- long dismissed
as the swapped one -- was right. This release maps
`GL_LINEAR_MIPMAP_NEAREST` to `MM4S` (bilinear in the rounded level),
which is what the GL name means.

The blending pair ships as a DOCUMENTED approximation:
`GL_NEAREST_MIPMAP_LINEAR` on `MM2S` and `GL_LINEAR_MIPMAP_LINEAR` on
`MM8S`.  Their fraction sits up to four green codes (f error 1/8) from
Mesa's `frac(lambda)` at the worst point, all of it from the lambda
approximation -- deterministic, monotonic, and the whole deviation table
lives in `docs/M12_WARP_MIPMAP_PLAN.md` section 10-1.  The alternative
was Mesa's software trilinear, which nobody could play.

## Exactness has one client-side condition

The engine clamps its level walk at the declared map count; Mesa clamps
lambda at `min(MaxLevel, P) - BaseLevel`. Hardware and software agree
exactly when those name the same last map, so a client that uploads a
chain past 8x8 should pin `GL_TEXTURE_MAX_LEVEL` to the 8x8 level -- the
GLQuake port does, in its filter audit. States the hardware cannot walk
-- LOD bias, min/max LOD windows that bite, chains deeper than four maps,
levels under 8x8 -- fall back to Mesa's software rasterizer, never to the
trapezoid tier, which fetches no levels at all.

## Two fixes worth confessing

Both were found by measurement in one afternoon. An admission gate
demanded the LOD window's default values be `0` and `1000`; Mesa's
defaults are `-1000` and `+1000`, so every texture in its default state
-- all of them -- went to software, 55% of a frame's triangles. And the
pre-admission probe left the texture format zeroed where the shared rule
checks for TW32, refusing every mip batch it would later have declared
correctly. Both gates now ask the question they meant to ask.

## The qualification band ships with the driver

`M12-C` is part of the opt-in WARP qualification harness ("WARP Depth
Test" = Yes, then the `OSMGAWarpQual` parameter, selector 3). Its report
walks out at a fifth of a second per line because the kernel message ring
keeps about thirteen lines and the block is longer than that.

## Packages

The usual three, from `pkg/`: the driver, the acceleration and the demos.
`SHA256SUMS` beside them.
