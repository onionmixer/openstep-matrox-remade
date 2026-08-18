#!/bin/csh -f
#
# Build and run the no-hardware P2.0~P2.8 regression set on OPENSTEP.
#
# Usage:
#   csh -f /ndrv/openstep-matrox-remade/test/run-p2-control-regression.csh \
#       /tmp/OpenStepMGAService
#
# The argument must name a target-native, already-built temporary service
# directory.  This runner only loads/unloads that service by its exact name;
# it never installs anything under /private/Devices and never accesses MGA
# PCI/BAR/VRAM/MMIO/DMA/IRQ/DDC/display resources.

if ($#argv != 1) then
    echo "usage: $0 <temporary-OpenStepMGAService-directory>"
    exit 2
endif

set serviceRoot = "$argv[1]"
set projectRoot = "$serviceRoot/OpenStepMGAService_reloc.tproj"
set reloc = "$serviceRoot/OpenStepMGAService.config/OpenStepMGAService_reloc"
set testRoot = /ndrv/openstep-matrox-remade/test
set failure = 0

if (! -d "$projectRoot" || ! -f "$reloc") then
    echo "OPENSTEP_MGA_P26_SUITE_INPUT_STATUS=1"
    exit 2
endif

csh -f $testRoot/check-p2-binary-imports.csh "$reloc"
if ($status != 0) then
    echo "OPENSTEP_MGA_P28_BINARY_GUARD=fail"
    exit 1
endif
echo "OPENSTEP_MGA_P28_BINARY_GUARD=pass"

cd "$projectRoot"
foreach client (openstep-mga-protocol-smoke openstep-mga-lease-loop openstep-mga-lease-abandon openstep-mga-lease-recovery-probe openstep-mga-raw-mig-negative openstep-mga-lease-contend)
    cc -O -Wall -I. -I$testRoot -o /tmp/$client $testRoot/$client.c OpenStepMGAUser.c
    if ($status != 0) then
        echo "OPENSTEP_MGA_P26_BUILD_$client=1"
        @ failure = $failure + 1
    else
        echo "OPENSTEP_MGA_P26_BUILD_$client=0"
    endif
end
if ($failure != 0) then
    echo "OPENSTEP_MGA_P26_SUITE_STATUS=$failure"
    exit 1
endif

/usr/etc/kl_util -u OpenStepMGAService
/usr/etc/kl_util -d OpenStepMGAService
/usr/etc/kl_util -a "$reloc"
if ($status != 0) then
    echo "OPENSTEP_MGA_P26_SERVICE_LOAD=1"
    /usr/etc/kl_util -u OpenStepMGAService
    /usr/etc/kl_util -d OpenStepMGAService
    exit 1
endif
echo "OPENSTEP_MGA_P26_SERVICE_LOAD=0"

/tmp/openstep-mga-protocol-smoke
if ($status != 0) @ failure = $failure + 1

/tmp/openstep-mga-lease-loop 1000
if ($status != 0) @ failure = $failure + 1

/tmp/openstep-mga-lease-abandon
if ($status != 0) @ failure = $failure + 1
sleep 1
/tmp/openstep-mga-lease-recovery-probe
if ($status != 0) @ failure = $failure + 1

/tmp/openstep-mga-raw-mig-negative
if ($status != 0) @ failure = $failure + 1

/bin/sh $testRoot/run-p2-lease-contend.sh /tmp/openstep-mga-lease-contend 1000
if ($status != 0) @ failure = $failure + 1

/usr/etc/kl_util -u OpenStepMGAService
/usr/etc/kl_util -d OpenStepMGAService
/usr/etc/kl_util -s MatroxMGA | grep "STATUS: Loaded"
if ($status != 0) @ failure = $failure + 1

sync
sync
echo "OPENSTEP_MGA_P26_SUITE_STATUS=$failure"
if ($failure != 0) exit 1
exit 0
