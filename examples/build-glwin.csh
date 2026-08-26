#!/bin/csh -f
#
# Build the spinning-teapot window demo -- both binaries, or one of them.
#
#   csh -f build-glwin.csh [-sw | -hybrid] [prefix] <mesa-source-root>
#
# ONE SOURCE, TWO BINARIES, exactly as with the offline teapot demo beside
# this script.
#
#   glwin_sw       linked against the STOCK Mesa library.  Contains no code
#                  from the Matrox project and needs nothing from it.  Mesa
#                  rasterises into ordinary memory and AppKit puts the result
#                  on the screen.  This one runs on any OPENSTEP machine.
#   glwin_hybrid   linked against libGL_mga.a from OpenStepMGAMesaAccel.
#                  Draws on the G450 where it can and in software where it
#                  cannot, and delivers the finished picture to the screen
#                  with a video-memory-to-video-memory blit that never
#                  crosses the bus.
#
# THE PAIR IS NOT SYMMETRIC, AND THAT IS THE POINT.
#
# The offline teapot's two binaries both work everywhere: teapot_hybrid falls
# back to Mesa entirely when the driver is absent and writes a file identical
# to teapot_sw's.  This pair is different.  glwin_hybrid needs the driver for
# its DELIVERY, not merely for its drawing -- the picture lives in video
# memory and reaches the screen by a kernel blit.  With no driver present it
# finds no accelerated surface, says so in its title bar, and shows nothing.
#
# So: glwin_sw is the one that always runs.  glwin_hybrid is the one that
# shows what the driver is for.  Run them one after the other and read the
# two title bars; measured on a G450 at 800x600 the difference is about
# 47.6 frames a second against 12.8.
#
# THE GEOMETRY FILE IS NOT SHIPPED, but the geometry itself is -- inside both
# prebuilt binaries.  tea.c has two owners: lines 1-529 are Thorsten Ohl's
# under GPL v2, and from line 531 to the end the file carries
# "Copyright (c) Mark J. Kilgard, 1994" under SGI's 1993 permissive grant.
# The cut below is 581-730, entirely inside the second block, so what it
# takes is redistributable provided SGI's copyright and permission notices
# travel with it; they are in NOTICE and in README_glwin.md.  The file is cut
# fresh each build only to keep copied upstream source out of the repository.
# The prebuilt binaries beside this script need no Mesa tree.
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
    if ($?MESASRC) then
        set mesasrc = "$MESASRC"
    endif
endif

if ("$mesasrc" == "" || ! -r "$mesasrc/widgets-mesa/demos/tea.c") then
    echo "build-glwin: need a Mesa 3.4.2 source tree for the teapot geometry"
    echo "build-glwin: usage: csh -f build-glwin.csh [-sw|-hybrid] [prefix] <mesa-source-root>"
    echo "build-glwin: (the prebuilt binaries beside this script need none)"
    exit 1
endif

# The same cut the driver's own test build makes: control points, the patch
# table, the texture coordinates and teapot() itself.
sed -n '581,730p' "$mesasrc/widgets-mesa/demos/tea.c" > teapot-geometry.h
if ($status != 0) exit 1
grep cpdata teapot-geometry.h > /dev/null
if ($status != 0) then
    echo "build-glwin: the cut produced no control points -- is this Mesa 3.4.2?"
    exit 1
endif

if ("$want" == "both" || "$want" == "sw") then
    if (! -r $prefix/Libraries/libGL.a) then
        echo "build-glwin: no $prefix/Libraries/libGL.a"
        echo "build-glwin: install OpenStepMesa342Libraries at $prefix"
        exit 1
    endif
    cc -m486 -I$prefix/Headers -DOSMGA_GLWIN_PLAIN \
        openstep-mga-glwin.m -L$prefix/Libraries -lGL -lm \
        -framework AppKit -framework Foundation -o glwin_sw
    if ($status != 0) exit 1
    echo "build-glwin: PASS ./glwin_sw (stock Mesa, AppKit delivery)"
endif

if ("$want" == "both" || "$want" == "hybrid") then
    if (! -r $prefix/Libraries/libGL_mga.a) then
        echo "build-glwin: no $prefix/Libraries/libGL_mga.a"
        echo "build-glwin: install OpenStepMGAMesaAccel at $prefix, or use -sw"
        exit 1
    endif
    # -I$prefix/Headers finds OpenStepMGAMesaHook.h, OpenStepMGAMesaBuffer.h
    # and OpenStepMGAHW3D.h, which the accelerated form includes by bare name
    # and which that package puts there.
    cc -m486 -I$prefix/Headers openstep-mga-glwin.m \
        $prefix/Libraries/libGL_mga.a -lm \
        -framework AppKit -framework Foundation -o glwin_hybrid
    if ($status != 0) exit 1
    echo "build-glwin: PASS ./glwin_hybrid (G450 where it can, Mesa where it cannot)"
endif
