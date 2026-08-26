#!/bin/sh
# Check a built OpenStepMesa342DemosMGA.pkg without installing it.
#
#   sh .../pkg/verify-demos-mga-pkg.sh [pkgdir] [source-root] [mesa-repo]
#
# The Mesa port's own verify-package.csh checks the three RELEASED packages
# by name and knows nothing about this variant, which is deliberate -- the
# variant is this project's doing, so its verification lives here.
#
# What has to be true of the variant, and is checked below:
#   1. everything the plain Demos package has is still there;
#   2. the teapot pair is there, with its notices;
#   3. teapot_sw contains NO Matrox code and teapot_hybrid does;
#   4. no LIBRARY is in it -- the variant adds a demo, not a library;
#   5. the cut geometry header did not travel.
PKGDIR="${1:-/usr/local/mesastage/OpenStepMesa342/dist}"
SRC="${2:-/ndrv/openstep-matrox-remade}"
MESA="${3:-/ndrv/opennstep-mesa342}"
NAME=OpenStepMesa342DemosMGA
PKG="$PKGDIR/$NAME.pkg"
UNPACK=/tmp/_demosmgaverify
MESADOCS="$MESA/upstream/Mesa-3.4.2/docs"

FAILS=/tmp/_demosmgaverify.fails
rm -f "$FAILS"; : > "$FAILS"
note() { echo "  $1"; }
bad()  { echo "  FAIL: $1"; echo "$1" >> "$FAILS"; }

# The Mesa builder removes its whole dist directory on every run, so a run
# without MESA_DEMO_OVERLAY leaves no variant behind at all.  Say that once,
# instead of reporting thirty consequences of the same absence.
if [ ! -d "$PKG" ]; then
    echo "verify-demos-mga-pkg: no $PKG" >&2
    echo "verify-demos-mga-pkg: build it with MESA_DEMO_OVERLAY set" >&2
    exit 1
fi

echo "package structure"
for f in "$NAME.tar.Z" "$NAME.bom" "$NAME.info" "$NAME.sizes" \
         "$NAME.pre_install"; do
    if [ -r "$PKG/$f" ]; then note "ok   $f"; else bad "$f missing"; fi
done
# The variant must not answer to the released package's version, or an
# operator cannot tell which artefact they are holding.
if grep 'mga' "$PKG/$NAME.info" > /dev/null; then
    note "ok   the .info version is the variant's"
else
    bad "the .info does not identify itself as the variant"
fi

# The BOM has to OWN the new files, which is a different question from
# whether the tar carries them.  A payload with a perfect GLWindow directory
# and a BOM that does not mention it installs and then cannot be removed
# cleanly -- and every check below reads the TAR, so nothing else here would
# notice.  Checked before unpacking, so a bad BOM fails early.
for f in Examples/Mesa342/GLWindow/openstep-mga-glwin.m \
         Examples/Mesa342/GLWindow/build-glwin.csh \
         Examples/Mesa342/GLWindow/README_glwin.md \
         Examples/Mesa342/GLWindow/glwin_sw \
         Examples/Mesa342/GLWindow/glwin_hybrid \
         Examples/Mesa342/Teapot/teapot_sw \
         Examples/Mesa342/Teapot/teapot_hybrid; do
    if lsbom -s "$PKG/$NAME.bom" | grep "^\./$f\$" > /dev/null; then
        note "ok   the BOM owns $f"
    else
        bad "the BOM does not own $f"
    fi
done

rm -rf "$UNPACK"; /bin/mkdirs "$UNPACK"
( cd "$UNPACK" && /usr/ucb/zcat "$PKG/$NAME.tar.Z" | tar xf - )

echo "the plain Demos payload is still all there"
for f in Examples/Mesa342/OSMesaClear/osmesa-clear \
         Examples/Mesa342/OSMesaClear/osmesa-clear.c \
         Examples/Mesa342/MesaView/MesaView.app/MesaView \
         Tools/OpenStepMesa342Demos-Intel; do
    if [ -r "$UNPACK/$f" ]; then note "ok   $f"; else bad "$f missing"; fi
done

echo "the teapot directory"
for f in openstep-mga-mesa-teapot.c build-teapot.csh README_teapot.md \
         NOTICE COPYRIGHT teapot_sw teapot_hybrid; do
    if [ -r "$UNPACK/Examples/Mesa342/Teapot/$f" ]; then
        note "ok   $f"
    else
        bad "Teapot/$f missing"
    fi
done
for b in teapot_sw teapot_hybrid; do
    if [ -x "$UNPACK/Examples/Mesa342/Teapot/$b" ]; then
        note "ok   $b is executable"
    else
        bad "$b is not executable"
    fi
    if file "$UNPACK/Examples/Mesa342/Teapot/$b" | grep i386 > /dev/null; then
        note "ok   $b is i386"
    else
        bad "$b is not i386 Mach-O"
    fi
done

echo "one source, two DIFFERENT binaries"
# This is the whole reason the pair exists.  If teapot_sw ever picked up
# Matrox symbols the Demos package would stop having a purely-Mesa demo, and
# if teapot_hybrid lost them the pair would be two copies of one binary.
n=`nm "$UNPACK/Examples/Mesa342/Teapot/teapot_sw" | grep OSMGAMesaHook | wc -l`
if [ "$n" -eq 0 ]; then
    note "ok   teapot_sw carries no Matrox symbols"
else
    bad "teapot_sw carries $n Matrox symbols"
fi
n=`nm "$UNPACK/Examples/Mesa342/Teapot/teapot_hybrid" | grep OSMGAMesaHook | wc -l`
if [ "$n" -ge 20 ]; then
    note "ok   teapot_hybrid carries $n Matrox symbols"
else
    bad "teapot_hybrid carries only $n Matrox symbols"
fi

echo "the window demo directory"
# Added when the window demo joined the payload.  Without this the verifier
# passed a package it had never looked inside: every check above names the
# Teapot directory explicitly, so a GLWindow directory that was empty, or
# missing, or carried two copies of one binary, would have gone through.
for f in openstep-mga-glwin.m build-glwin.csh README_glwin.md \
         NOTICE COPYRIGHT glwin_sw glwin_hybrid; do
    if [ -r "$UNPACK/Examples/Mesa342/GLWindow/$f" ]; then
        note "ok   $f"
    else
        bad "GLWindow/$f missing"
    fi
done
for b in glwin_sw glwin_hybrid; do
    if [ -x "$UNPACK/Examples/Mesa342/GLWindow/$b" ]; then
        note "ok   $b is executable"
    else
        bad "$b is not executable"
    fi
    if file "$UNPACK/Examples/Mesa342/GLWindow/$b" | grep i386 > /dev/null; then
        note "ok   $b is i386"
    else
        bad "$b is not i386 Mach-O"
    fi
done
n=`nm "$UNPACK/Examples/Mesa342/GLWindow/glwin_sw" | grep OSMGAMesaHook | wc -l`
if [ "$n" -eq 0 ]; then
    note "ok   glwin_sw carries no Matrox symbols"
else
    bad "glwin_sw carries $n Matrox symbols"
fi
n=`nm "$UNPACK/Examples/Mesa342/GLWindow/glwin_hybrid" | grep OSMGAMesaHook | wc -l`
if [ "$n" -ge 20 ]; then
    note "ok   glwin_hybrid carries $n Matrox symbols"
else
    bad "glwin_hybrid carries only $n Matrox symbols"
fi
# Renamed teapot binaries would satisfy every test above: two different
# files, one with Matrox symbols and one without, both i386 and executable.
# So each one is asked for a string that only its own build produces.
#
# Written out four times rather than looped over "name pattern" pairs.  The
# looped form needed `cut` to split the pair, `cut` is not on the PATH here,
# and the pattern came out EMPTY -- so `grep ""` matched every line and all
# four checks passed without testing anything.  Four plain lines cannot do
# that.
W="$UNPACK/Examples/Mesa342/GLWindow"
if strings "$W/glwin_sw" | grep 'stock -- measuring' > /dev/null
then note "ok   glwin_sw is the window demo"
else bad "glwin_sw lacks its own title string -- is it the window demo?"; fi
if strings "$W/glwin_sw" | grep 'server-wait' > /dev/null
then note "ok   glwin_sw has the stock delivery phases"
else bad "glwin_sw lacks 'server-wait' -- is it the stock build?"; fi
if strings "$W/glwin_hybrid" | grep 'teapot -- hardware --' > /dev/null
then note "ok   glwin_hybrid is the window demo"
else bad "glwin_hybrid lacks its own title string -- is it the window demo?"; fi
if strings "$W/glwin_hybrid" | grep 'no accelerated surface' > /dev/null
then note "ok   glwin_hybrid has the accelerated delivery path"
else bad "glwin_hybrid lacks its no-driver message -- is it the hybrid build?"; fi

if [ -f "$UNPACK/Examples/Mesa342/GLWindow/teapot-geometry.h" ]; then
    bad "teapot-geometry.h is in the GLWindow payload"
else
    note "ok   no teapot-geometry.h in GLWindow"
fi
if cmp -s "$SRC/NOTICE" "$UNPACK/Examples/Mesa342/GLWindow/NOTICE"; then
    note "ok   GLWindow NOTICE is byte-for-byte with the source copy"
else
    bad "GLWindow NOTICE differs from the source copy"
fi
if cmp -s "$MESADOCS/COPYRIGHT" "$UNPACK/Examples/Mesa342/GLWindow/COPYRIGHT"; then
    note "ok   GLWindow COPYRIGHT is byte-for-byte"
else
    bad "GLWindow COPYRIGHT differs from the port's own copy"
fi
if grep 'Silicon Graphics' "$UNPACK/Examples/Mesa342/GLWindow/README_glwin.md" > /dev/null; then
    note "ok   README_glwin.md states the geometry's licence"
else
    bad "README_glwin.md does not state the geometry's licence"
fi

echo "it adds a demo, not a library"
for d in Libraries Headers; do
    if [ -d "$UNPACK/$d" ]; then
        bad "$d is in the Demos payload -- that belongs to the other packages"
    else
        note "ok   no $d"
    fi
done

echo "the cut geometry did not travel as a file"
if [ -f "$UNPACK/Examples/Mesa342/Teapot/teapot-geometry.h" ]; then
    bad "teapot-geometry.h is in the payload"
else
    note "ok   no teapot-geometry.h"
fi

echo "licence gate"
if cmp -s "$MESADOCS/COPYRIGHT" "$UNPACK/Examples/Mesa342/Teapot/COPYRIGHT"; then
    note "ok   Mesa COPYRIGHT is byte-for-byte"
else
    bad "Mesa COPYRIGHT differs from the port's own copy"
fi
if cmp -s "$SRC/NOTICE" "$UNPACK/Examples/Mesa342/Teapot/NOTICE"; then
    note "ok   NOTICE is byte-for-byte with the source copy"
else
    bad "NOTICE differs from the source copy"
fi
# SGI's grant is the one the binaries actually need: the geometry is compiled
# into both of them, and its terms ask for the notice in supporting
# documentation.  Both probe strings, because the grant is two notices.
for probe in 'Silicon Graphics' 'Kilgard' 'Permission to use'; do
    if grep "$probe" "$UNPACK/Examples/Mesa342/Teapot/NOTICE" > /dev/null; then
        note "ok   NOTICE keeps: $probe"
    else
        bad "NOTICE lost: $probe"
    fi
done
if grep 'Silicon Graphics' "$UNPACK/Examples/Mesa342/Teapot/README_teapot.md" > /dev/null; then
    note "ok   README_teapot.md states the geometry's licence"
else
    bad "README_teapot.md does not state the geometry's licence"
fi

echo "the shipped sources are current"
# Same lesson as the other two verifiers: a package built before these were
# edited passes every presence check and ships stale text.  README_teapot.md
# gained the two mode-related causes of "NO -- software only" after one such
# build, and those are the two a reader is most likely to need.
# The directory is part of the pair now, because the two demos ship the same
# three kinds of file and either can go stale on its own.
for pair in "Teapot README_teapot.md" "Teapot build-teapot.csh" \
            "Teapot openstep-mga-mesa-teapot.c" \
            "GLWindow README_glwin.md" "GLWindow build-glwin.csh" \
            "GLWindow openstep-mga-glwin.m"; do
    d=`echo "$pair" | awk '{print $1}'`
    f=`echo "$pair" | awk '{print $2}'`
    case "$f" in
    openstep-mga-mesa-teapot.c|openstep-mga-glwin.m) srcf="$SRC/test/$f" ;;
    *)                                               srcf="$SRC/examples/$f" ;;
    esac
    if cmp -s "$srcf" "$UNPACK/Examples/Mesa342/$d/$f"; then
        note "ok   $d/$f is byte-for-byte with the source copy"
    else
        bad "$d/$f differs from the source copy -- rebuild the overlay and the package"
    fi
done

n=`wc -l < "$FAILS"`
if [ "$n" -eq 0 ]; then
    echo "VERIFY_DEMOS_MGA_PKG=PASS"
else
    echo "VERIFY_DEMOS_MGA_PKG=FAIL ($n)"
    exit 1
fi
