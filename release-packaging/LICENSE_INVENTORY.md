# Release license inventory

This inventory is a release gate, not a substitute for the upstream notices.
The packaging scripts must copy the listed original texts into the source
tree and the payload documentation before invoking the OPENSTEP package
utility, and the verifier must compare them byte for byte.  The form is the
SDL project's; the contents are this project's.

| Package | Component | Required source record | Required payload record | Gate |
| --- | --- | --- | --- | --- |
| Driver | original driver, inspector, Mesa-side back end | `LICENSE` — BSD 2-Clause, (c) 2026 Peter Yoo, byte-identical to the sibling projects' | `Documentation/OpenStep-MGA-G450/LICENSE` | present and byte-for-byte with the source copy |
| Driver | **WARP microcode** — MIT, (c) 1999 Matrox Graphics Inc., generated from the X.Org MGA driver's `mga_ucode.h` and COMPILED INTO the driver (`OpenStepMGAReplacementDisplay_reloc.tproj/Makefile:22`) | the notice in `warp/OpenStepMGAWarpUcode.c`, reproduced in full in `NOTICE` | `Documentation/OpenStep-MGA-G450/NOTICE` | the four probe strings of the notice present in both source and payload; its terms require it in every copy |
| Driver | **inspector nib** — derived from Configure.app's stock nib, not new material | the paragraph in `NOTICE` naming `build-inspector-nib.py` and the stock Configure.app nib | same paragraph in the payload `NOTICE` | derivation stated, not implied by silence |
| Accel | Mesa core and OSMesa (`libGL_mga.a` contains Mesa objects) | `../opennstep-mesa342/upstream/Mesa-3.4.2/docs/COPYRIGHT` (5833 bytes) | `Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/COPYRIGHT` | byte-for-byte comparison |
| Accel | the LGPL components `COPYRIGHT` cites -- **none of which are in this archive**; shipped because a licence document that names another should not arrive without it | `../opennstep-mesa342/upstream/Mesa-3.4.2/docs/COPYING` (25493 bytes) | `Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/COPYING` | byte-for-byte comparison |
| Accel | Mesa's disclaimer -- not a licensed OpenGL implementation, OpenGL is an SGI trademark.  It is in upstream `docs/README`, **not** in `COPYRIGHT`, which is what an earlier draft of this table claimed | `../opennstep-mesa342/upstream/Mesa-3.4.2/docs/README` (19076 bytes) | `Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/README.Mesa` | byte-for-byte; the phrase "not a licensed OpenGL implementation" present |
| Accel | this project's additions to Mesa | `Documentation/OpenStep-MGA-Accel/PORT-NOTES.md` | same file | names the Mesa version, states that Mesa is ADDED TO and never modified in place |
| Accel | this project's own code and the WARP notice, shipped again beside the library | `LICENSE`, `NOTICE` | `Documentation/OpenStep-MGA-Accel/LICENSE`, `.../NOTICE` | byte-for-byte with the source copies; a package that ships the library must ship the terms it is under |
| Demos | **Utah teapot geometry** -- (c) 1993 SGI, parts (c) Mark J. Kilgard 1994, compiled into BOTH `teapot_sw` and `teapot_hybrid` from `tea.c` lines 581-730 | the block reproduced in full in `NOTICE` | `Examples/Mesa342/Teapot/NOTICE` and the licence section of `README_teapot.md` | the grant requires the copyright AND permission notices in copies and in supporting documentation; both probe strings present |
| Demos | Mesa, statically linked into both binaries (`teapot_sw` against stock `libGL.a`, `teapot_hybrid` against `libGL_mga.a`) | Mesa's `COPYRIGHT` in the Mesa port repository | `Examples/Mesa342/Teapot/COPYRIGHT` | notice retention only.  Checked member by member: every Mesa object in either binary comes from `src/*`, `src/X86/*` or `src/OSmesa/*`, all under the MIT-style Main Mesa Copyright.  Neither binary links GLU, which is the LGPL part -- `teapot_sw` links `-lGL -lm`, `teapot_hybrid` links `libGL_mga.a -lm` |

## Why BSD 2-Clause is available

Every source that reaches a shipped binary was audited for third-party
licence headers.  Two carry one.  The WARP microcode is MIT, which imposes
only notice retention -- satisfied by `NOTICE`, and compatible with
BSD 2-Clause, so the project's own code can be BSD 2-Clause without
qualification.  The teapot geometry compiled into both demo binaries is
SGI's 1993 permissive grant (`Copyright (c) Mark J. Kilgard, 1994`,
`tea.c` lines 531-743), which requires the copyright and permission notices
in copies and in supporting documentation; `NOTICE` and `README_teapot.md`
carry them.

Nothing GPL or LGPL reaches a payload, and that was established by
enumeration rather than assumption.

- Mesa 3.4.2 is not under one licence.  `COPYRIGHT` records that the core
  library left the GNU LGPL at Mesa 3.1 for the XFree86 (MIT-style) grant,
  while GLU and the 3Dfx, SVGA, DOS and GGI drivers stayed LGPL.
- All 83 object members of `libGL_mga.a` were mapped to their source
  (`ar t` lists 84; the eighty-fourth is `__.SYMDEF`, the archive's own
  symbol table): `src/*`, `src/X86/*`, `src/OSmesa/*`, plus this project's
  `osmgaccel.o`.  All the Mesa ones are under the Main Mesa Copyright.  No
  LGPL component is in it; GLU is `libGLU.a`, which this project neither
  ships nor touches.
- The GPL half of `tea.c` -- lines 1-529, Thorsten Ohl's -- is outside the
  cut, which is lines 581-730.

## What is deliberately absent, and must stay absent

- **The MatroxMGA binary distribution.**  `refs/SOURCES.md` records that it
  is analysis-only: no binary, disassembly, recovered pseudocode or object
  code enters the repository, and the new implementation does not copy its
  code.  Nothing derived from it is in either payload.
- **`teapot-geometry.h` as a FILE.**  It is cut out of the Mesa tree at
  build time rather than committed, so the repository carries no copied
  upstream source.  Release artefacts ARE built from it -- both teapot
  binaries -- which is licensed by SGI's grant and covered by the notice
  rows above.  The file itself still enters no payload.
- **Anything under `refs/`.**

## Before the first release candidate

Walk the payload again and check every file's provenance.  If a header, a
nib resource or a generated table turns out to carry a copyright or licence
notice not in the table above, add the row and copy the text.  The gate is
that the table explains every shipped byte, not that it looks complete.
