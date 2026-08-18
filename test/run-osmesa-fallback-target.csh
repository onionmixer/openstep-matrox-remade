#!/bin/csh -f
# Build and run the installed Mesa 3.4.2 OSMesa fallback smoke test.
#
# This is deliberately an off-screen `/tmp` client.  It neither opens a
# window nor uses a machine interface.
# Usage: csh -f run-osmesa-fallback-target.csh <source-root> <prefix>

if ($#argv != 2) then
    echo "usage: $0 <openstep-matrox-remade-source-root> <mesa-prefix>"
    exit 2
endif

set sourceRoot = "$argv[1]"
set mesaPrefix = "$argv[2]"
set testSource = "$sourceRoot/test/openstep-mga-osmesa-fallback.c"
set headerRoot = "$mesaPrefix/Headers"
set libraryRoot = "$mesaPrefix/Libraries"
set tempRoot = /tmp/OSMGAMOSMesaFallback
set testBinary = "$tempRoot/test"

if (! -f "$testSource" || ! -f "$headerRoot/GL/osmesa.h" || \
    ! -f "$libraryRoot/libGL.a" || ! -f "$libraryRoot/libGLU.a") then
    echo "OPENSTEP_MGA_OSMESA_FALLBACK_INPUT=missing"
    exit 2
endif

sync
sync
/bin/rm -rf "$tempRoot"
mkdir "$tempRoot"
if ($status != 0) then
    echo "OPENSTEP_MGA_OSMESA_FALLBACK_TEMP=fail"
    exit 1
endif

# This deliberately matches the installed Mesa package consumer invocation.
# Target `-O` output has a historical loader-dispatch failure for this client.
cc -m486 -I"$headerRoot" "$testSource" -L"$libraryRoot" \
    -lGLU -lGL -lm -o "$testBinary"
if ($status != 0) then
    /bin/rm -rf "$tempRoot"
    echo "OPENSTEP_MGA_OSMESA_FALLBACK_BUILD=fail"
    exit 1
endif
echo "OPENSTEP_MGA_OSMESA_FALLBACK_BUILD=pass"

# OPENSTEP's parent csh may retain a failed command lookup made before the
# compiler created this temporary pathname.  A fresh shell performs the
# ordinary executable lookup without changing the test or its environment.
/bin/sh -c "$testBinary"
set testStatus = $status
/bin/rm -rf "$tempRoot"
if ($testStatus != 0) then
    echo "OPENSTEP_MGA_OSMESA_FALLBACK_TEST=fail"
    exit 1
endif
echo "OPENSTEP_MGA_OSMESA_FALLBACK_TEST=pass"
exit 0
