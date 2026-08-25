#!/bin/csh -f
#
# Build the accelerated teapot demo.
#
# Unlike the stock Mesa demos, this one links libGL_mga.a -- the accelerated
# library from the OpenStepMGAMesaAccel package -- and includes two headers
# from it, so it needs that package installed at $prefix rather than the
# stock Mesa Libraries package.
#
# It also needs the teapot GEOMETRY, which is deliberately not redistributed
# here: tea.c is GPL as a whole while the teapot inside it is Mark Kilgard's
# under GLUT's own terms, so rather than decide what a copied fragment would
# carry, the geometry is cut out of the Mesa source tree at build time and
# nothing of it is committed or packaged.  Point $MESASRC at an unpacked
# Mesa 3.4.2 source tree; the prebuilt binary beside this script is there so
# that running the demo needs no such tree.
#
#   csh -f build-teapot.csh [prefix] [mesa-source-root]
#
set prefix = /LocalDeveloper
set mesasrc = ""
if ($#argv >= 1) set prefix = $argv[1]
if ($#argv >= 2) set mesasrc = $argv[2]
if ("$mesasrc" == "") then
    if ($?MESASRC) set mesasrc = "$MESASRC"
endif

if (! -r $prefix/Libraries/libGL_mga.a) then
    echo "build-teapot: no $prefix/Libraries/libGL_mga.a"
    echo "build-teapot: install OpenStepMGAMesaAccel at $prefix first"
    exit 1
endif
if ("$mesasrc" == "" || ! -r "$mesasrc/widgets-mesa/demos/tea.c") then
    echo "build-teapot: need a Mesa 3.4.2 source tree for the teapot geometry"
    echo "build-teapot: usage: csh -f build-teapot.csh [prefix] <mesa-source-root>"
    echo "build-teapot: (the prebuilt ./teapot beside this script needs none)"
    exit 1
endif

# The same cut the driver's own test build makes: the control points, the
# patch table, the texture coordinates and teapot() itself.
sed -n '581,730p' "$mesasrc/widgets-mesa/demos/tea.c" > teapot-geometry.h
if ($status != 0) exit 1
grep 'cpdata' teapot-geometry.h > /dev/null
if ($status != 0) then
    echo "build-teapot: the cut produced no control points -- is this Mesa 3.4.2?"
    exit 1
endif

cc -m486 -I$prefix/Headers openstep-mga-mesa-teapot.c \
    $prefix/Libraries/libGL_mga.a -lm -o teapot
if ($status != 0) exit 1
echo "build-teapot: PASS ./teapot"
