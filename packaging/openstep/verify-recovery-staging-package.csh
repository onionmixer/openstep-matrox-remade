#!/bin/csh -f
# Inspect the generated archive without installing it.
# Usage: csh -f verify-recovery-staging-package.csh <source-root>

if ($#argv != 1) then
    echo "usage: $0 <openstep-matrox-remade-source-root>"
    exit 2
endif

set sourceRoot = "$argv[1]"
set packageRoot = /tmp/OpenStepMGARecoveryStage/dist/OpenStepMGARecoveryStaging.pkg
set unpackRoot = /tmp/OpenStepMGARecoveryStage/verify-unpack
set installerTar = /NextAdmin/Installer.app/installer_tar

foreach file ( "$packageRoot/OpenStepMGARecoveryStaging.tar.Z" \
               "$packageRoot/OpenStepMGARecoveryStaging.bom" \
               "$packageRoot/OpenStepMGARecoveryStaging.info" \
               "$packageRoot/OpenStepMGARecoveryStaging.pre_install" )
    if (! -r "$file") then
        echo "OPENSTEP_MGA_RECOVERY_STAGE_VERIFY_INPUT=missing:$file"
        exit 2
    endif
end
if (! -x "$installerTar") then
    echo "OPENSTEP_MGA_RECOVERY_STAGE_VERIFY_INPUT=missing-installer-tar"
    exit 2
endif

if (-d "$unpackRoot") /bin/rm -rf "$unpackRoot"
mkdir "$unpackRoot"
# This old csh treats a quoted command-variable as a pathname lookup rather
# than an executable command in a pipeline.  All paths are fixed/no-space.
(cd $unpackRoot; /usr/ucb/zcat $packageRoot/OpenStepMGARecoveryStaging.tar.Z | $installerTar xf -)
if ($status != 0) exit 1

if (! -r "$unpackRoot/DriverStaging/OSMGADisplay.config/OSMGADisplay_reloc" || \
    ! -r "$unpackRoot/DriverStaging/OSMGADisplay.config/Default.table" || \
    ! -r "$unpackRoot/Tools/OpenStepMGARecoveryStaging-Intel") then
    echo "OPENSTEP_MGA_RECOVERY_STAGE_VERIFY_PAYLOAD=incomplete"
    exit 1
endif
if (-e "$unpackRoot/OSMGADisplay.config") then
    echo "OPENSTEP_MGA_RECOVERY_STAGE_VERIFY_PAYLOAD=production-root-leak"
    exit 1
endif
grep 'Auto Detect IDs' "$unpackRoot/DriverStaging/OSMGADisplay.config/Default.table" > /dev/null
if ($status == 0) then
    echo "OPENSTEP_MGA_RECOVERY_STAGE_VERIFY_PAYLOAD=matching-leak"
    exit 1
endif
# `file` deliberately omits the architecture subtype for preloaded files;
# i386 classification is enforced by the payload marker/BOM below.
file "$unpackRoot/DriverStaging/OSMGADisplay.config/OSMGADisplay_reloc" | grep 'Mach-O preloaded' > /dev/null
if ($status != 0) then
    echo "OPENSTEP_MGA_RECOVERY_STAGE_VERIFY_PAYLOAD=non-i386-reloc"
    exit 1
endif
/usr/etc/lsbom -arch i386 -s "$packageRoot/OpenStepMGARecoveryStaging.bom" | grep OpenStepMGARecoveryStaging-Intel > /dev/null
if ($status != 0) then
    echo "OPENSTEP_MGA_RECOVERY_STAGE_VERIFY_BOM=i386-marker-missing"
    exit 1
endif
/usr/etc/lsbom -arch m68k -s "$packageRoot/OpenStepMGARecoveryStaging.bom" | grep OpenStepMGARecoveryStaging-Intel > /dev/null
if ($status == 0) then
    echo "OPENSTEP_MGA_RECOVERY_STAGE_VERIFY_BOM=m68k-marker-leak"
    exit 1
endif
/bin/rm -rf "$unpackRoot"
echo "OPENSTEP_MGA_RECOVERY_STAGE_VERIFY_STATUS=pass"
exit 0
