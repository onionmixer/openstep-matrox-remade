#!/bin/csh -f
#
# Build the SDL2 teapot -- both binaries, or one of them.
#
#   csh -f build-sdl-teapot.csh [-sw | -hybrid] [prefix] <mesa-source-root>
#
# WHAT THIS DEMO IS FOR.  It is the same Utah teapot as the demo beside it,
# through SDL2 instead of straight OSMesa, and it exists to show what SDL2
# costs and what removing that cost looks like:
#
#   sdlteapot_sw       stock libGL.a.  No Matrox code at all.
#   sdlteapot_hybrid   libGL_mga.a.  The card draws, and with
#                      OSMGA_SDLTEAPOT_PRESENT=3 the frame goes to the screen
#                      from video memory without entering system memory.
#
# Measured on a G450 at 800x600: 0.54 frames a second through SDL2's ordinary
# delivery, 43.9 with the direct one.  The difference is not the drawing --
# the card draws either way -- it is that the ordinary path walks the surface
# back into the caller's array once per rendering batch.
#
# THIS ONE NEEDS SDL2, which the other demos do not.  Install
# OpenStepSDL2Libraries and OpenStepSDL2Headers (openstep.2 or later) at the
# same prefix.  Earlier SDL2 releases will not do: openstep.2 is where the GL
# backend stopped handing the accelerated surface back at every bind, and
# where SDL_openstepglpresent.h appeared.
#
# THE GEOMETRY FILE IS NOT SHIPPED.  As with the offline teapot, it is cut
# from the Mesa tree at build time; see README_sdlteapot.md and NOTICE for
# whose it is and on what terms.
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
# condition is an error rather than a false.  Test it on its own line, in
# the block form -- see build-teapot.csh for what the one-line form does.
if ("$mesasrc" == "") then
    if ($?MESASRC) then
        set mesasrc = "$MESASRC"
    endif
endif

if ("$mesasrc" == "" || ! -r "$mesasrc/widgets-mesa/demos/tea.c") then
    echo "build-sdl-teapot: need a Mesa 3.4.2 source tree for the teapot geometry"
    echo "build-sdl-teapot: usage: csh -f build-sdl-teapot.csh [-sw|-hybrid] [prefix] <mesa-source-root>"
    exit 1
endif
if (! -r $prefix/Libraries/libSDL2.a || ! -r $prefix/Headers/SDL2/SDL.h) then
    echo "build-sdl-teapot: no SDL2 at $prefix"
    echo "build-sdl-teapot: install OpenStepSDL2Libraries and OpenStepSDL2Headers"
    echo "build-sdl-teapot: (openstep.2 or later -- earlier ones do not accelerate)"
    exit 1
endif

sed -n '581,730p' "$mesasrc/widgets-mesa/demos/tea.c" > teapot-geometry.h
if ($status != 0) exit 1
grep cpdata teapot-geometry.h > /dev/null
if ($status != 0) then
    echo "build-sdl-teapot: the cut produced no control points -- is this Mesa 3.4.2?"
    exit 1
endif

# -m486 and -D__OPENSTEP__ are SDL2's: its public headers do not parse for
# this compiler without the second, and the archive is built with the first.
set sdlflags = "-m486 -D__OPENSTEP__ -I$prefix/Headers/SDL2"
set frameworks = "-framework AppKit -framework Foundation -framework SoundKit"

if ("$want" == "both" || "$want" == "sw") then
    if (! -r $prefix/Libraries/libGL.a) then
        echo "build-sdl-teapot: no $prefix/Libraries/libGL.a"
        echo "build-sdl-teapot: install OpenStepMesa342Libraries at $prefix"
        exit 1
    endif
    cc -O $sdlflags -I$prefix/Headers -DOSMGA_SDLTEAPOT_PLAIN \
        openstep-mga-sdl-teapot.c $prefix/Libraries/libSDL2.a \
        $prefix/Libraries/libGL.a -lm $frameworks -o sdlteapot_sw
    if ($status != 0) exit 1
    echo "build-sdl-teapot: PASS ./sdlteapot_sw (stock Mesa)"
endif

if ("$want" == "both" || "$want" == "hybrid") then
    if (! -r $prefix/Libraries/libGL_mga.a) then
        echo "build-sdl-teapot: no $prefix/Libraries/libGL_mga.a"
        echo "build-sdl-teapot: install OpenStepMGAMesaAccel at $prefix, or use -sw"
        exit 1
    endif
    cc -O $sdlflags -I$prefix/Headers openstep-mga-sdl-teapot.c \
        $prefix/Libraries/libSDL2.a $prefix/Libraries/libGL_mga.a -lm \
        $frameworks -o sdlteapot_hybrid
    if ($status != 0) exit 1
    echo "build-sdl-teapot: PASS ./sdlteapot_hybrid (the card draws)"
endif
