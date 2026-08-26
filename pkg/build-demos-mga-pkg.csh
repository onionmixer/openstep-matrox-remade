#!/bin/csh -f
#
# Build the Mesa Demos VARIANT that carries both demo pairs.
#
#   csh -f .../pkg/build-demos-mga-pkg.csh [stage-parent] [overlay]
#
# Runs ON the target.  This is a wrapper around the Mesa port's own
# build-split-packages.csh and it exists for one reason: both of the
# variables that script reads have defaults that are wrong here, and getting
# either wrong fails in a way that does not look like a failure.
#
#   MESA_STAGE_PARENT  defaults to /tmp, where no staged tree lives on this
#                      machine.  Every input then fails to open at once and
#                      the script used to `exit 2` in SILENCE -- no output,
#                      status 2, nothing to read.  (That silence is fixed in
#                      the Mesa repository now, but the default is still
#                      wrong for this machine.)
#   MESA_DEMO_OVERLAY  unset means "build the RELEASED Demos package", which
#                      is correct behaviour and not what this script is for.
#
# Run pkg/build-demos-overlay.sh first: this consumes what it produces.
#
if ($#argv >= 1) then
    setenv MESA_STAGE_PARENT "$argv[1]"
else
    setenv MESA_STAGE_PARENT /usr/local/mesastage
endif
if ($#argv >= 2) then
    setenv MESA_DEMO_OVERLAY "$argv[2]"
else
    setenv MESA_DEMO_OVERLAY /tmp/_mgateapot/overlay
endif

if (! -d "$MESA_STAGE_PARENT/OpenStepMesa342/src") then
    echo "build-demos-mga-pkg: no staged Mesa tree at $MESA_STAGE_PARENT"
    echo "build-demos-mga-pkg: run opennstep-mesa342/build/stage-openstep-mesa342.csh first"
    exit 2
endif
if (! -d "$MESA_DEMO_OVERLAY/Examples") then
    echo "build-demos-mga-pkg: no overlay at $MESA_DEMO_OVERLAY"
    echo "build-demos-mga-pkg: run pkg/build-demos-overlay.sh first"
    exit 2
endif

csh -f "$MESA_STAGE_PARENT/OpenStepMesa342/src/packaging/openstep/build-split-packages.csh"
set rc = $status
if ($rc != 0) then
    echo "build-demos-mga-pkg: the Mesa split builder failed ($rc)"
    exit $rc
endif
echo "build-demos-mga-pkg: PASS $MESA_STAGE_PARENT/OpenStepMesa342/dist"
exit 0
