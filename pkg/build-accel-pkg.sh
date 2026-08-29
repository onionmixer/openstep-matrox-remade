#!/bin/sh
# Build the OPENSTEP Installer package for the Matrox Mesa acceleration.
#
#   sh .../pkg/build-accel-pkg.sh [source-root] [outdir] [mesa-repo]
#
# Runs ON the target: the package tool is OPENSTEP's, and the payload is
# i386 machine code.
#
# This package ADDS libGL_mga.a beside the stock Mesa libraries.  It never
# ships, moves or shadows libGL.a -- see docs/P1_PACKAGING_PRINCIPLE.md.
set -e
SRC="${1:-/ndrv/openstep-matrox-remade}"
# /tmp/pkgout, which is what release-packaging/PAYLOAD_MANIFEST.md passes to
# every script in this directory and what collect-release-pkgs.sh,
# diff-against-installed.sh, check-bom-overlap.sh and verify-driver-pkg.sh
# already default to.  The builders used to default to /tmp on their own, so
# running a builder and then its verifier with no arguments failed with "no
# package" on a package that had just been written somewhere else.  The
# directory is created below rather than assumed.
OUT="${2:-/tmp/pkgout}"
MESA="${3:-/ndrv/opennstep-mesa342}"
NAME=OSMGAMesaAccel
PKGTOOL=/NextAdmin/Installer.app/package
LIB="$SRC/build/mesa/libGL_mga.a"
MESADOCS="$MESA/upstream/Mesa-3.4.2/docs"

if [ ! -x "$PKGTOOL" ]; then
    echo "build-accel-pkg: $PKGTOOL not found (run on OPENSTEP)" >&2
    exit 1
fi
if [ "`/usr/bin/arch`" != i386 ]; then
    echo "build-accel-pkg: the payload is i386; build it on i386" >&2
    exit 1
fi
for f in "$LIB" \
         "$SRC/mesa/OpenStepMGAMesaHook.h" \
         "$SRC/mesa/OpenStepMGAMesaBuffer.h" \
         "$SRC/hw3d/OpenStepMGAHW3D.h" \
         "$MESADOCS/COPYRIGHT" "$MESADOCS/COPYING" "$MESADOCS/README" \
         "$SRC/release-packaging/PORT-NOTES.md" \
         "$SRC/LICENSE" "$SRC/NOTICE" \
         "$SRC/pkg/$NAME.info" "$SRC/pkg/$NAME.pre_install" \
         "$SRC/pkg/$NAME.post_install" \
         "$SRC/packaging/openstep/installer-architecture-marker.c"; do
    if [ ! -r "$f" ]; then
        echo "build-accel-pkg: missing input: $f" >&2
        exit 1
    fi
done

# The archive has to be the ACCELERATED one, and the accelerated one is
# distinguishable from the stock archive by two independent facts: it has an
# osmgaccel.o member the stock archive has not, and its osmesa.o was compiled
# with the hook macro so it exports the hook symbols.  Measured on this
# machine: stock 0 hook symbols and 0 osmgaccel members, accelerated 31 and 1.
# Checking only the filename would package a renamed stock archive.
members=`ar t "$LIB" | grep osmgaccel | wc -l`
if [ "$members" -lt 1 ]; then
    echo "build-accel-pkg: $LIB has no osmgaccel.o member" >&2
    exit 1
fi
hooks=`nm "$LIB" | grep OSMGAMesaHook | wc -l`
if [ "$hooks" -lt 1 ]; then
    echo "build-accel-pkg: $LIB exports no hook symbols" >&2
    exit 1
fi

# And it has to be the RELEASE flavour, which is a different question from
# whether it is accelerated.
#
# build-matrox-mesa.csh writes two libraries.  build/mesa is shippable;
# build/mesa-test carries the mid-batch trapezoid spoiler that exists so a
# harness can reach the revoke-during-flush path on purpose.  They have the
# same member names, the same hook symbols and the same size to within a few
# hundred bytes, so nothing about the file itself says which is which -- only
# the symbol does.  Checked by DEFINED symbol (nm's T), not by the name
# appearing anywhere, so an undefined reference from some other member could
# not fail a clean archive.
#
# OSMGAMesaHookInjectRefusal is deliberately NOT in this list: it is a
# documented feature of the shipped teapot demo and belongs in the release.
#
# The four measurement symbols were added on 2026-08-28.  MeasureArm selects
# arms B/C/D, each of which removes a stage of the submission and so draws
# the wrong thing or nothing; SubmitDry sends an ioctl a shipped driver no
# longer answers at all.  None of it is a feature.
# Three more on 2026-08-29, from the narrowed mirror.  AreaOmit drops a
# source from the dirty rectangle, so a caller loses pixels and is told
# nothing -- the class R20 retired first.  MirrorBox writes the caller's
# array outside any contract; it exists to be timed.  Disagree reads the
# driver's surface against the caller's array, which is internal state that
# would harden into ABI if it shipped.  OSMGAMesaHookNarrowMirror is NOT
# here, for the reason InjectRefusal is not: it is the feature.
for sym in OSMGAMesaHookInjectNamed OSMGAMesaHookInjectedNamed \
           OSMGAMesaHookMeasureArm OSMGAMesaHookDryStatus \
           OSMGAMesaHookDryCount OSMGAMesaProbeSubmitDry \
           OSMGAMesaHookAreaOmit OSMGAMesaBufferMirrorBox \
           OSMGAMesaBufferDisagree; do
    if nm "$LIB" | grep "T _$sym\$" > /dev/null; then
        echo "build-accel-pkg: $LIB defines $sym -- that is the TEST" >&2
        echo "build-accel-pkg: flavour.  Build build/mesa without -test." >&2
        exit 1
    fi
done

STAGEPARENT=/tmp/_mgaaccelpkg
STAGE="$STAGEPARENT/p"
rm -rf "$STAGEPARENT" "$OUT/$NAME.pkg"
# Mesa's texts go under THIS package's directory, not Mesa's.  Measured:
# OpenStepMesa342Headers.pkg's BOM owns
# ./Documentation/OpenStep-Mesa-3.4.2/{COPYRIGHT,COPYING}, and writing the
# same two paths made them the only files claimed by two BOMs (11 files here,
# 53 in the three Mesa packages, exactly 2 in common).  Whether this
# Installer reference-counts a shared path on removal is not established by
# anything here or in ref/ -- and a package that cannot be removed without
# possibly taking another package's licence texts with it is not one to ship
# on the strength of an assumption.  The subdirectory names the upstream
# version so the provenance is in the path.
/bin/mkdirs "$STAGE/Libraries" "$STAGE/Headers" "$STAGE/Tools" \
            "$STAGE/Documentation/OpenStep-MGA-Accel/Mesa-3.4.2" \
            "$STAGE/Documentation/OpenStep-MGA-Accel"

cp "$LIB" "$STAGE/Libraries/libGL_mga.a"
cp "$SRC/mesa/OpenStepMGAMesaHook.h" "$SRC/mesa/OpenStepMGAMesaBuffer.h" \
   "$SRC/hw3d/OpenStepMGAHW3D.h" "$STAGE/Headers/"
# OpenStepMGAHW3D.h is not optional: OpenStepMGAMesaHook.h includes it for
# OSMGAHW3DTri, and a prefix with only the first two headers fails to build.

# Mesa's terms travel with Mesa's code, byte for byte from the port's own
# upstream tree -- copied and then compared, so a truncated copy cannot pass.
# COPYRIGHT is the licence; COPYING is the LGPL text COPYRIGHT cites for the
# components that are still LGPL (none of them are in this archive, but a
# licence that names another should not arrive without it); README carries
# Mesa's disclaimer that it is not a licensed OpenGL implementation, which is
# NOT in COPYRIGHT -- it is renamed README.Mesa so it cannot be mistaken for
# this package's own.
M="$STAGE/Documentation/OpenStep-MGA-Accel/Mesa-3.4.2"
cp "$MESADOCS/COPYRIGHT" "$MESADOCS/COPYING" "$M/"
cp "$MESADOCS/README" "$M/README.Mesa"
cmp "$MESADOCS/COPYRIGHT" "$M/COPYRIGHT"
cmp "$MESADOCS/COPYING"   "$M/COPYING"
cmp "$MESADOCS/README"    "$M/README.Mesa"

cp "$SRC/release-packaging/PORT-NOTES.md" "$SRC/LICENSE" "$SRC/NOTICE" \
   "$STAGE/Documentation/OpenStep-MGA-Accel/"

# The Installer decides a package's architecture from Mach-O files in the
# payload, and it does not look inside static archives -- so an all-archive
# payload would be architecture-neutral and would offer itself on a 68k
# machine.  This marker is the smallest i386 executable that settles it.
cc -m486 -o "$STAGE/Tools/OpenStepMGAAccel-Intel" \
   "$SRC/packaging/openstep/installer-architecture-marker.c"
chmod 555 "$STAGE/Tools/OpenStepMGAAccel-Intel"

if [ -r "$STAGE/Libraries/libGL.a" ]; then
    echo "build-accel-pkg: stock libGL.a leaked into the payload" >&2
    exit 1
fi

# installer_tar refuses paths over 100 characters, silently: it prints
# "file name too long" and leaves the file out, which cost the driver
# package three nib files.  This payload's longest path is well under, and
# that is checked rather than assumed, so `package`'s own archive can stand
# and this package keeps the same on-disk format as the released Mesa ones.
long=`( cd "$STAGE" && find . -print ) | awk 'length($0) >= 100' | wc -l`
if [ "$long" -gt 0 ]; then
    echo "build-accel-pkg: $long payload paths reach the 100-char limit" >&2
    ( cd "$STAGE" && find . -print ) | awk 'length($0) >= 100' >&2
    exit 1
fi

test -d "$OUT" || /bin/mkdirs "$OUT"
"$PKGTOOL" "$STAGE" "$SRC/pkg/$NAME.info" -d "$OUT" < /dev/null

cp "$SRC/pkg/$NAME.pre_install"  "$OUT/$NAME.pkg/$NAME.pre_install"
cp "$SRC/pkg/$NAME.post_install" "$OUT/$NAME.pkg/$NAME.post_install"
chmod 555 "$OUT/$NAME.pkg/$NAME.pre_install" "$OUT/$NAME.pkg/$NAME.post_install"
echo "build-accel-pkg: PASS $OUT/$NAME.pkg"
