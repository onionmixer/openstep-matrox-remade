#!/bin/sh
# Check a built OSMGAMesaAccel.pkg without installing it.
#
#   sh .../pkg/verify-accel-pkg.sh [pkgdir] [source-root] [mesa-repo]
#
# Everything here is read-only.  It unpacks the payload into /tmp and looks
# at what an installer would actually place.
PKGDIR="${1:-/tmp/pkgout}"   # the same default as every other script here
SRC="${2:-/ndrv/openstep-matrox-remade}"
MESA="${3:-/ndrv/opennstep-mesa342}"
NAME=OSMGAMesaAccel
PKG="$PKGDIR/$NAME.pkg"
UNPACK=/tmp/_mgaaccelverify
MESADOCS="$MESA/upstream/Mesa-3.4.2/docs"

# This shell runs functions in a subshell, so a counter assigned inside one
# does not survive; the empty variable then makes `test` complain rather
# than compare.  Record failures in a file, which does survive.
FAILS=/tmp/_mgaaccelverify.fails
rm -f "$FAILS"; : > "$FAILS"

note() { echo "  $1"; }
bad()  { echo "  FAIL: $1"; echo "$1" >> "$FAILS"; }

echo "package structure"
for f in "$NAME.tar.Z" "$NAME.bom" "$NAME.info" "$NAME.sizes" \
         "$NAME.pre_install" "$NAME.post_install"; do
    if [ -r "$PKG/$f" ]; then note "ok   $f"; else bad "$f missing"; fi
done
for f in "$NAME.pre_install" "$NAME.post_install"; do
    if [ -x "$PKG/$f" ]; then note "ok   $f is executable"
    else bad "$f is not executable"; fi
done

rm -rf "$UNPACK"; /bin/mkdirs "$UNPACK"
( cd "$UNPACK" && /usr/ucb/zcat "$PKG/$NAME.tar.Z" | tar xf - )

echo "payload"
for f in Libraries/libGL_mga.a \
         Headers/OpenStepMGAMesaHook.h Headers/OpenStepMGAMesaBuffer.h \
         Headers/OpenStepMGAHW3D.h \
         Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/COPYRIGHT \
         Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/COPYING \
         Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/README.Mesa \
         Documentation/OpenStep-MGA-Accel/PORT-NOTES.md \
         Documentation/OpenStep-MGA-Accel/LICENSE \
         Documentation/OpenStep-MGA-Accel/NOTICE \
         Tools/OpenStepMGAAccel-Intel; do
    if [ -r "$UNPACK/$f" ]; then note "ok   $f"; else bad "$f missing"; fi
done

echo "it adds a library, it does not replace one"
for f in Libraries/libGL.a Libraries/libGLU.a; do
    if [ -f "$UNPACK/$f" ]; then bad "$f is in the payload -- that is Mesa's to ship"
    else note "ok   no $f"; fi
done

echo "the archive is the accelerated one"
# The Installer's architecture marker proves only what the Installer will
# offer; it says nothing about the archive, because the Installer does not
# read static-archive members.  So the members are read here directly.
A="$UNPACK/Libraries/libGL_mga.a"
n=`ar t "$A" | grep osmgaccel | wc -l`
if [ "$n" -ge 1 ]; then note "ok   osmgaccel.o is a member"
else bad "no osmgaccel.o member -- this may be a renamed stock libGL.a"; fi
n=`ar t "$A" | grep osmesa | wc -l`
if [ "$n" -ge 1 ]; then note "ok   osmesa.o is a member"
else bad "no osmesa.o member"; fi
n=`nm "$A" | grep OSMGAMesaHook | wc -l`
if [ "$n" -ge 20 ]; then note "ok   $n hook symbols (stock has 0)"
else bad "only $n hook symbols -- osmesa.o was built without the hook macro"; fi

# Duplicated from build-accel-pkg.sh on purpose: this script's whole job is to
# check a .pkg that already exists, including one built somewhere else or by
# an older copy of the builder.  A check that lived only in the builder would
# pass every package it had never seen.
# The four measurement symbols were added on 2026-08-28: MeasureArm selects
# arms B/C/D, each of which removes a stage of the submission and so draws
# the wrong thing or nothing, and SubmitDry sends an ioctl a shipped driver
# no longer answers at all.  None of it is a feature.
for sym in OSMGAMesaHookInjectNamed OSMGAMesaHookInjectedNamed \
           OSMGAMesaHookMeasureArm OSMGAMesaHookDryStatus \
           OSMGAMesaHookDryCount OSMGAMesaProbeSubmitDry; do
    if nm "$A" | grep "T _$sym\$" > /dev/null; then
        bad "$sym is defined -- this is the TEST flavour of the library"
    else
        note "ok   no $sym (test-only injector)"
    fi
done
# The other injector is expected: the shipped teapot's documented `inject`.
if nm "$A" | grep "T _OSMGAMesaHookInjectRefusal\$" > /dev/null
then note "ok   OSMGAMesaHookInjectRefusal present (the demo needs it)"
else bad "OSMGAMesaHookInjectRefusal is missing -- the demo's inject dies"; fi

echo "architecture"
X="$UNPACK/_mgaverify_members"
rm -rf "$X"; /bin/mkdirs "$X"
( cd "$X" && ar x "$A" osmesa.o osmgaccel.o )
for o in osmesa.o osmgaccel.o; do
    if [ ! -r "$X/$o" ]; then
        bad "could not extract $o"
    elif file "$X/$o" | grep i386 > /dev/null; then
        note "ok   $o is i386"
    else
        bad "$o is not i386: `file $X/$o`"
    fi
done
if file "$UNPACK/Tools/OpenStepMGAAccel-Intel" | grep i386 > /dev/null; then
    note "ok   the Installer architecture marker is i386"
else
    bad "the architecture marker is not i386"
fi
if grep m68k "$PKG/$NAME.bom" > /dev/null; then
    bad "the BOM mentions m68k"
else
    note "ok   the BOM does not mention m68k"
fi

echo "licence gate"
if cmp -s "$MESADOCS/COPYRIGHT" "$UNPACK/Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/COPYRIGHT"; then
    note "ok   Mesa COPYRIGHT is byte-for-byte"
else
    bad "Mesa COPYRIGHT differs from the port's own copy"
fi
if cmp -s "$MESADOCS/COPYING" "$UNPACK/Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/COPYING"; then
    note "ok   Mesa COPYING is byte-for-byte"
else
    bad "Mesa COPYING differs from the port's own copy"
fi
if cmp -s "$MESADOCS/README" "$UNPACK/Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/README.Mesa"; then
    note "ok   Mesa README.Mesa is byte-for-byte"
else
    bad "Mesa README.Mesa differs from the port's own copy"
fi
# The disclaimer lives in README, not in COPYRIGHT.  An earlier draft of the
# licence inventory said COPYRIGHT and was wrong; the probe is on the file
# that actually carries the sentence.
if grep 'not a licensed OpenGL' "$UNPACK/Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/README.Mesa" > /dev/null; then
    note "ok   Mesa's not-a-licensed-OpenGL statement travelled"
else
    bad "Mesa's not-a-licensed-OpenGL statement is missing"
fi
if grep 'Brian Paul' "$UNPACK/Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/COPYRIGHT" > /dev/null; then
    note "ok   the Main Mesa Copyright is in COPYRIGHT"
else
    bad "COPYRIGHT does not carry the Main Mesa Copyright"
fi
for probe in 'Matrox Graphics Inc' 'Silicon Graphics' 'Kilgard'; do
    if grep "$probe" "$UNPACK/Documentation/OpenStep-MGA-Accel/NOTICE" > /dev/null; then
        note "ok   NOTICE keeps: $probe"
    else
        bad "NOTICE lost: $probe"
    fi
done
# Every document this package ships is compared against its source copy, not
# just the licence.  A package built before the docs were edited passes every
# other check in this file -- the payload is all there and all correct -- and
# ships stale text.  That happened: PORT-NOTES.md gained the per-mode
# acceleration table after the package was built.
for f in LICENSE NOTICE PORT-NOTES.md; do
    case "$f" in
    PORT-NOTES.md) srcf="$SRC/release-packaging/PORT-NOTES.md" ;;
    *)             srcf="$SRC/$f" ;;
    esac
    if cmp -s "$srcf" "$UNPACK/Documentation/OpenStep-MGA-Accel/$f"; then
        note "ok   $f is byte-for-byte with the source copy"
    else
        bad "$f differs from the source copy -- rebuild the package"
    fi
done

n=`wc -l < "$FAILS"`
if [ "$n" -eq 0 ]; then
    echo "VERIFY_ACCEL_PKG=PASS"
else
    echo "VERIFY_ACCEL_PKG=FAIL ($n)"
    exit 1
fi
