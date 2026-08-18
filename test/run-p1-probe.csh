#!/bin/csh -f
#
# Manual P1 procedure.  Do not run until P0 gate is signed off and NFS kernel
# logging is active.  This script does not register an Instance table and does
# not invoke driverLoader.
#

if ($#argv > 1) then
    echo "usage: $0 [<probe-config-bundle>]"
    exit 2
endif

set bundle = /tmp/OpenStepMGAProbe/OpenStepMGAProbe.config
if ($#argv == 1) then
    set bundle = $argv[1]
endif
set reloc = $bundle/OpenStepMGAProbe_reloc

if (! -e $reloc) then
    echo "P1: missing $reloc"
    exit 1
endif

# A previous interrupted test may have left a server registration behind.
# This probe owns no persistent kernel references, so an unload is safe.
/usr/etc/kl_util -u OpenStepMGAProbe >& /dev/null
/usr/etc/kl_util -d OpenStepMGAProbe >& /dev/null
/usr/etc/kl_util -a $reloc
if ($status != 0) then
    echo "P1: kl_util add failed"
    exit 1
endif

/usr/etc/kl_util -l OpenStepMGAProbe
if ($status != 0) then
    echo "P1: kl_util load failed"
    /usr/etc/kl_util -d OpenStepMGAProbe
    exit 1
endif

/usr/etc/kl_util -s OpenStepMGAProbe
echo "P1: inspect MGA-PROBE lines in /usr/adm/messages and NFS kernel log"

/usr/etc/kl_util -u OpenStepMGAProbe
/usr/etc/kl_util -d OpenStepMGAProbe
echo "P1: unloaded and deregistered"
