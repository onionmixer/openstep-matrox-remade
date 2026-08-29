#!/bin/sh
# Build both demo pairs and stage them as an OVERLAY for the Mesa port's
# Demos package.
#
#   sh .../pkg/build-demos-overlay.sh [matrox-root] [mesa-repo] [outdir]
#
# Runs ON the target.  Produces a directory tree that is copied verbatim into
# the Demos payload:
#
#   <outdir>/Examples/Mesa342/Teapot/
#       openstep-mga-mesa-teapot.c   one source for both binaries
#       build-teapot.csh             rebuilds either, given a Mesa tree
#       README_teapot.md             how to run them and read the report
#       NOTICE                       SGI's grant for the teapot geometry
#       COPYRIGHT                    Mesa's, byte-for-byte
#       teapot_sw                    stock Mesa only, no Matrox code
#       teapot_hybrid                links libGL_mga.a
#
#   <outdir>/Examples/Mesa342/GLWindow/
#       openstep-mga-glwin.m         one source for both binaries
#       build-glwin.csh              rebuilds either, given a Mesa tree
#       README_glwin.md              how to run them and read the title bar
#       NOTICE                       SGI's grant for the teapot geometry
#       COPYRIGHT                    Mesa's own terms
#       glwin_sw                     stock Mesa only, no Matrox code
#       glwin_hybrid                 links libGL_mga.a
#
# Both demos are BUILT HERE with the very scripts that ship beside them, so a
# build script that has drifted from its source fails the packaging rather
# than reaching a user.
#
# WHY THIS IS AN OVERLAY AND NOT A PATCH TO THE MESA BUILDER.  The Mesa port
# is released; its Demos package must keep building from its own repository
# alone, byte-reproducible, with no reference to this project.  So this
# script produces a tree, and the Mesa builder takes it or does not.  When it
# does, it packages under a DIFFERENT .info -- different version, different
# description -- so the two artefacts never share an identity.
set -e
SRC="${1:-/ndrv/openstep-matrox-remade}"
MESA="${2:-/ndrv/opennstep-mesa342}"
OUT="${3:-/tmp/_mgateapot/overlay}"
MESASRC="$MESA/upstream/Mesa-3.4.2"
PREFIX=/tmp/_mgateapot/prefix
T="$OUT/Examples/Mesa342/Teapot"
W="$OUT/Examples/Mesa342/GLWindow"
S="$OUT/Examples/Mesa342/SDLTeapot"
SDLB=/usr/local/nxbuild/SDL20/build/SDL-2.32.10-openstep

if [ "`/usr/bin/arch`" != i386 ]; then
    echo "build-demos-overlay: the binaries are i386; build them on i386" >&2
    exit 1
fi
for f in "$SRC/test/openstep-mga-mesa-teapot.c" "$SRC/examples/build-teapot.csh" \
         "$SRC/examples/README_teapot.md" \
         "$SRC/test/openstep-mga-glwin.m" "$SRC/examples/build-glwin.csh" \
         "$SRC/examples/README_glwin.md" "$SRC/NOTICE" \
         "$SRC/build/mesa/libGL_mga.a" \
         "$MESASRC/docs/COPYRIGHT" "$MESASRC/widgets-mesa/demos/tea.c"; do
    if [ ! -r "$f" ]; then
        echo "build-demos-overlay: missing input: $f" >&2
        exit 1
    fi
done

# A private prefix that looks exactly like an installed one: the Mesa
# Libraries and Headers packages at a destination, plus this project's Accel
# package at the same destination.  Building against it is the only way to
# find out that a header is missing from a package before a user does -- that
# is how OpenStepMGAHW3D.h turned out to be required.
rm -rf "$PREFIX" "$OUT"
/bin/mkdirs "$PREFIX/Libraries" "$PREFIX/Headers/GL" "$T" "$W" "$S"
# The stock archive: from the Mesa tree when it has been built there, and
# otherwise from this project's kept copy -- which is the same lookup
# tools/build-matrox-mesa.csh makes, and which exists because the Mesa tree
# in the repository is source only.
STOCK="$MESASRC/lib/libGL.a"
if [ ! -r "$STOCK" ]; then STOCK="$SRC/build/mesa/libGL.a"; fi
if [ ! -r "$STOCK" ]; then
    echo "build-demos-overlay: no stock libGL.a, in the Mesa tree or kept" >&2
    exit 1
fi
cp "$STOCK" "$PREFIX/Libraries/libGL.a"
cp "$SRC/build/mesa/libGL_mga.a" "$PREFIX/Libraries/"
cp "$MESASRC/include/GL/gl.h" "$MESASRC/include/GL/glext.h" \
   "$MESASRC/include/GL/osmesa.h" "$PREFIX/Headers/GL/"
cp "$SRC/mesa/OpenStepMGAMesaHook.h" "$SRC/mesa/OpenStepMGAMesaBuffer.h" \
   "$SRC/mesa/OpenStepMGAMesaProbe.h" \
   "$SRC/hw3d/OpenStepMGAHW3D.h" "$PREFIX/Headers/"
ranlib "$PREFIX/Libraries/libGL.a" "$PREFIX/Libraries/libGL_mga.a"

cp "$SRC/test/openstep-mga-mesa-teapot.c" "$SRC/examples/build-teapot.csh" \
   "$SRC/examples/README_teapot.md" "$T/"
cp "$SRC/NOTICE" "$T/NOTICE"
cp "$MESASRC/docs/COPYRIGHT" "$T/COPYRIGHT"
cmp "$MESASRC/docs/COPYRIGHT" "$T/COPYRIGHT"

# build-teapot.csh cuts the geometry out of the Mesa tree itself and refuses
# to compile if the cut produced no control points, so the tree it is given
# is checked rather than trusted.
( cd "$T" && csh -f build-teapot.csh "$PREFIX" "$MESASRC" )

for b in teapot_sw teapot_hybrid; do
    if [ ! -x "$T/$b" ]; then
        echo "build-demos-overlay: $b was not built" >&2
        exit 1
    fi
    # NOT `if ! cmd`: this sh has no command negation, and such a guard
    # answers false every time and never fires.
    if file "$T/$b" | grep i386 > /dev/null; then
        :
    else
        echo "build-demos-overlay: $b is not i386 Mach-O" >&2
        exit 1
    fi
    chmod 555 "$T/$b"
done
chmod 555 "$T/build-teapot.csh"

# teapot_sw must contain NO Matrox code: that is the whole reason the pair
# exists, and it is asserted rather than assumed.  teapot_hybrid must contain
# it, or the overlay is two copies of the same binary.
if nm "$T/teapot_sw" | grep OSMGAMesaHook > /dev/null; then
    echo "build-demos-overlay: teapot_sw carries Matrox symbols" >&2
    exit 1
fi
if nm "$T/teapot_hybrid" | grep OSMGAMesaHook > /dev/null; then
    :
else
    echo "build-demos-overlay: teapot_hybrid carries no Matrox symbols" >&2
    exit 1
fi

# The geometry header is cut fresh at build time and stays out of the tree
# that gets packaged; the binaries above already contain the geometry.
rm -f "$T/teapot-geometry.h"

#
# And the window demo, staged and built the same way and checked the same way.
#
cp "$SRC/test/openstep-mga-glwin.m" "$SRC/examples/build-glwin.csh" \
   "$SRC/examples/README_glwin.md" "$W/"
cp "$SRC/NOTICE" "$W/NOTICE"
cp "$MESASRC/docs/COPYRIGHT" "$W/COPYRIGHT"
cmp "$MESASRC/docs/COPYRIGHT" "$W/COPYRIGHT"

( cd "$W" && csh -f build-glwin.csh "$PREFIX" "$MESASRC" )

for b in glwin_sw glwin_hybrid; do
    if [ ! -x "$W/$b" ]; then
        echo "build-demos-overlay: $b was not built" >&2
        exit 1
    fi
    if file "$W/$b" | grep i386 > /dev/null; then
        :
    else
        echo "build-demos-overlay: $b is not i386 Mach-O" >&2
        exit 1
    fi
    chmod 555 "$W/$b"
done
chmod 555 "$W/build-glwin.csh"

# Same assertion as the teapot pair, and for the same reason: the stock
# binary is the one a user can run without this project installed, so it must
# carry none of it.  The hybrid must carry it, or the two are one binary
# under two names.
if nm "$W/glwin_sw" | grep OSMGAMesaHook > /dev/null; then
    echo "build-demos-overlay: glwin_sw carries Matrox symbols" >&2
    exit 1
fi
if nm "$W/glwin_hybrid" | grep OSMGAMesaHook > /dev/null; then
    :
else
    echo "build-demos-overlay: glwin_hybrid carries no Matrox symbols" >&2
    exit 1
fi

rm -f "$W/teapot-geometry.h"

#
# And the SDL2 teapot -- SOURCE ONLY, unlike the two pairs above.
#
# It is the one demo here that needs a second product: SDL2, at the same
# prefix, openstep.2 or later.  Shipping a prebuilt binary would put a
# statically linked copy of one SDL2 release inside a Matrox package and tie
# the two together at the version, which is exactly the coupling the overlay
# comment above refuses for Mesa.  So the user builds it, and the script
# beside it says plainly what it needs.
#
cp "$SRC/test/openstep-mga-sdl-teapot.c" "$SRC/examples/build-sdl-teapot.csh" \
   "$SRC/examples/README_sdlteapot.md" "$S/"
cp "$SRC/NOTICE" "$S/NOTICE"
cp "$MESASRC/docs/COPYRIGHT" "$S/COPYRIGHT"
cmp "$MESASRC/docs/COPYRIGHT" "$S/COPYRIGHT"
chmod 555 "$S/build-sdl-teapot.csh"

#
# BUILT HERE IF SDL2 IS ON THIS MACHINE, and only as a check.
#
# The two pairs above are built by the very scripts that ship beside them, so
# a script that has drifted from its source fails the packaging rather than
# reaching a user.  This one gets the same protection when it can: if the
# SDL2 project has been built on this machine, its archive and headers are
# borrowed into the temporary prefix, the shipped script is run, and the
# binaries are then DELETED -- they are a test result, not a payload.
#
# When SDL2 is not there the check is skipped LOUDLY.  A silent skip would
# mean a broken build script could ship, which is the failure this whole
# section exists to prevent.
#
if [ -r "$SDLB/libSDL2.a" ] && [ -r "$SDLB/include/SDL.h" ]; then
    /bin/mkdirs "$PREFIX/Headers/SDL2"
    cp "$SDLB/libSDL2.a" "$PREFIX/Libraries/"
    # UNQUOTED on purpose.  This sh does not expand a glob in a word whose
    # other part was quoted -- `cp "$SDLB"/include/*.h` passes the asterisk
    # through literally and cp reports the pattern as a missing file.  The
    # path is a fixed one under /tmp with no spaces in it.
    cp $SDLB/include/*.h "$PREFIX/Headers/SDL2/"
    cp "$SRC/../openstep-sdl20/port/openstep/src/video/openstep/SDL_openstepglpresent.h" \
       "$PREFIX/Headers/SDL2/"
    ranlib "$PREFIX/Libraries/libSDL2.a"
    ( cd "$S" && csh -f build-sdl-teapot.csh "$PREFIX" "$MESASRC" )
    for b in sdlteapot_sw sdlteapot_hybrid; do
        if [ ! -x "$S/$b" ]; then
            echo "build-demos-overlay: $b was not built" >&2
            exit 1
        fi
    done
    if nm "$S/sdlteapot_sw" | grep OSMGAMesaHook > /dev/null; then
        echo "build-demos-overlay: sdlteapot_sw carries Matrox symbols" >&2
        exit 1
    fi
    if nm "$S/sdlteapot_hybrid" | grep OSMGAMesaHook > /dev/null; then
        :
    else
        echo "build-demos-overlay: sdlteapot_hybrid carries no Matrox symbols" >&2
        exit 1
    fi
    rm -f "$S/sdlteapot_sw" "$S/sdlteapot_hybrid"
    echo "build-demos-overlay: the SDL2 teapot builds from its shipped script"
else
    echo "build-demos-overlay: SDL2 NOT PRESENT -- the SDL2 teapot ships"
    echo "build-demos-overlay: unchecked.  Build openstep-sdl20 and run again"
    echo "build-demos-overlay: to have its build script verified."
fi
rm -f "$S/teapot-geometry.h"

echo "build-demos-overlay: PASS $OUT"
