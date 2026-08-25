#!/bin/csh -f
#
# Build the teapot demo -- both binaries, or one of them.
#
#   csh -f build-teapot.csh [-sw | -hybrid] [prefix] <mesa-source-root>
#
# ONE SOURCE, TWO BINARIES.
#
#   teapot_sw       linked against the STOCK Mesa library.  Contains no code
#                   from the Matrox project and needs nothing from it.  If
#                   this one draws a teapot, Mesa works.
#   teapot_hybrid   linked against libGL_mga.a from OpenStepMGAMesaAccel.
#                   Draws triangle by triangle on the G450 where it can and
#                   in software where it cannot -- which is what "hybrid"
#                   means -- and reports the split.
#
# teapot_hybrid runs whether or not the driver is there: the library is
# statically linked into it, and if /dev/osmgavram is absent the probe
# answers "no device" and the whole scene goes to Mesa.  Verified on the
# target: with acceleration made unavailable it exits 0, says "NO --
# software only", and writes a file BYTE-IDENTICAL to teapot_sw's.
#
# So the pair is not a workaround for a binary that would otherwise fail.
# It is there so the demo package stays a Mesa demo -- teapot_sw carries no
# Matrox code at all -- and so that running both tells you at once whether a
# problem is Mesa's or this driver's.
#
# THE GEOMETRY IS NOT SHIPPED.  tea.c is GPL as a whole while the teapot
# inside it is Mark Kilgard's under GLUT's own terms, so rather than decide
# what a copied fragment would carry, it is cut out of a Mesa source tree at
# build time and nothing of it is committed or packaged.  The prebuilt
# binaries beside this script need no such tree.
#
set want = both
set prefix = /LocalDeveloper
set mesasrc = ""
set argi = 1
if ($#argv >= 1) then
    if ("$argv[1]" == "-sw") then
        set want = sw
        set argi = 2
    else if ("$argv[1]" == "-hybrid") then
        set want = hybrid
        set argi = 2
    endif
endif
if ($#argv >= $argi) set prefix = "$argv[$argi]"
@ argi = $argi + 1
if ($#argv >= $argi) set mesasrc = "$argv[$argi]"
# csh evaluates both sides of &&, so an unset variable inside a compound
# condition is an error rather than a false.  Test it on its own line.
if ("$mesasrc" == "") then
    if ($?MESASRC) set mesasrc = "$MESASRC"
endif

if ("$mesasrc" == "" || ! -r "$mesasrc/widgets-mesa/demos/tea.c") then
    echo "build-teapot: need a Mesa 3.4.2 source tree for the teapot geometry"
    echo "build-teapot: usage: csh -f build-teapot.csh [-sw|-hybrid] [prefix] <mesa-source-root>"
    echo "build-teapot: (the prebuilt binaries beside this script need none)"
    exit 1
endif

# The same cut the driver's own test build makes: control points, the patch
# table, the texture coordinates and teapot() itself.
sed -n '581,730p' "$mesasrc/widgets-mesa/demos/tea.c" > teapot-geometry.h
if ($status != 0) exit 1
grep cpdata teapot-geometry.h > /dev/null
if ($status != 0) then
    echo "build-teapot: the cut produced no control points -- is this Mesa 3.4.2?"
    exit 1
endif

if ("$want" == "both" || "$want" == "sw") then
    if (! -r $prefix/Libraries/libGL.a) then
        echo "build-teapot: no $prefix/Libraries/libGL.a"
        echo "build-teapot: install OpenStepMesa342Libraries at $prefix"
        exit 1
    endif
    cc -m486 -I$prefix/Headers -DOSMGA_TEAPOT_PLAIN \
        openstep-mga-mesa-teapot.c -L$prefix/Libraries -lGL -lm -o teapot_sw
    if ($status != 0) exit 1
    echo "build-teapot: PASS ./teapot_sw (stock Mesa)"
endif

if ("$want" == "both" || "$want" == "hybrid") then
    if (! -r $prefix/Libraries/libGL_mga.a) then
        echo "build-teapot: no $prefix/Libraries/libGL_mga.a"
        echo "build-teapot: install OpenStepMGAMesaAccel at $prefix, or use -sw"
        exit 1
    endif
    cc -m486 -I$prefix/Headers openstep-mga-mesa-teapot.c \
        $prefix/Libraries/libGL_mga.a -lm -o teapot_hybrid
    if ($status != 0) exit 1
    echo "build-teapot: PASS ./teapot_hybrid (G450 where it can, Mesa where it cannot)"
endif
