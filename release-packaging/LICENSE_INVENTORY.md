# Release license inventory

This inventory is a release gate, not a substitute for the upstream notices.
The packaging scripts must copy the listed original texts into the source
tree and the payload documentation before invoking the OPENSTEP package
utility, and the verifier must compare them byte for byte.  The form is the
SDL project's; the contents are this project's.

| Package | Component | Required source record | Required payload record | Gate |
| --- | --- | --- | --- | --- |
| Driver | original driver, inspector, Mesa-side back end | `LICENSE` at project root | `Documentation/OpenStep-MGA-G450/LICENSE` | present and names the project |
| Driver | **WARP microcode** — MIT-style Matrox/X.Org notice, generated from the X.Org MGA driver's `mga_ucode.h` and COMPILED INTO the driver (`OpenStepMGAReplacementDisplay_reloc.tproj/Makefile:22`) | the notice reproduced in `warp/OpenStepMGAWarpUcode.c:1-24` | same notice in `Documentation/OpenStep-MGA-G450/NOTICE` | notice text present verbatim; its terms require it in copies |
| Driver | **inspector nib** — derived from Configure.app's stock nib, not new material | provenance paragraph in `NOTICE` naming the derivation | same paragraph in the payload `NOTICE` | derivation stated, not implied |
| Accel | Mesa core and OSMesa (`libGL_mga.a` contains Mesa objects) | `../opennstep-mesa342/upstream/Mesa-3.4.2/docs/COPYRIGHT` (5833 bytes) | `Documentation/OpenStep-Mesa-3.4.2/COPYRIGHT` | byte-for-byte comparison |
| Accel | Mesa GLU and the rest of the archive | `../opennstep-mesa342/upstream/Mesa-3.4.2/docs/COPYING` (25493 bytes) | `Documentation/OpenStep-Mesa-3.4.2/COPYING` | byte-for-byte comparison |
| Accel | Mesa's own disclaimer | the statement in `COPYRIGHT` that this is not a licensed OpenGL implementation | retained in the payload copy | must not be edited away |
| Accel | this project's additions to Mesa | `Documentation/OpenStep-MGA-Accel/PORT-NOTES.md` | same file | names the Mesa version, states that Mesa is ADDED TO and never modified in place |

## What is deliberately absent, and must stay absent

- **The MatroxMGA binary distribution.**  `refs/SOURCES.md` records that it
  is analysis-only: no binary, disassembly, recovered pseudocode or object
  code enters the repository, and the new implementation does not copy its
  code.  Nothing derived from it is in either payload.
- **The teapot geometry.**  It is cut out of the Mesa tree at build time
  rather than committed, precisely so that neither the repository nor a
  payload carries it.  No release artefact is built from it.
- **Anything under `refs/`.**

## Before the first release candidate

Walk the payload again and check every file's provenance.  If a header, a
nib resource or a generated table turns out to carry a copyright or licence
notice not in the table above, add the row and copy the text.  The gate is
that the table explains every shipped byte, not that it looks complete.
