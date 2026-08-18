#!/bin/csh -f
# Build, but never install, a fail-closed G450 recovery-staging package.
# Usage: csh -f build-recovery-staging-package.csh <source-root>

if ($#argv != 1) then
    echo "usage: $0 <openstep-matrox-remade-source-root>"
    exit 2
endif

set sourceRoot = "$argv[1]"
set bundleRoot = "$sourceRoot/OpenStepMGAReplacementDisplay/OpenStepMGAReplacementDisplay.config"
set info = "$sourceRoot/packaging/openstep/OpenStepMGARecoveryStaging.info"
set preinstall = "$sourceRoot/packaging/openstep/OpenStepMGARecoveryStaging.pre_install"
set markerSource = "$sourceRoot/packaging/openstep/installer-architecture-marker.c"
set packageTool = /NextAdmin/Installer.app/package
set workRoot = /tmp/OpenStepMGARecoveryStage
set payloadRoot = "$workRoot/payload"
set distRoot = "$workRoot/dist"
set marker = "$payloadRoot/Tools/OpenStepMGARecoveryStaging-Intel"

foreach file ( "$bundleRoot/OpenStepMGAReplacementDisplay_reloc" \
               "$bundleRoot/OpenStepMGAReplacementDisplay" \
               "$bundleRoot/Default.table" "$info" "$preinstall" "$markerSource" )
    if (! -r "$file") then
        echo "OPENSTEP_MGA_RECOVERY_STAGE_BUILD_INPUT=missing:$file"
        exit 2
    endif
end
if (! -x "$packageTool") then
    echo "OPENSTEP_MGA_RECOVERY_STAGE_BUILD_INPUT=missing-package-tool"
    exit 2
endif

# OPENSTEP's local `file` prints only "Mach-O preloaded file" for a kernel
# relocatable.  The package builder itself runs on the i386 target and emits
# a separate i386 Installer marker below.
/usr/bin/arch | grep '^i386$' > /dev/null
if ($status != 0) then
    echo "OPENSTEP_MGA_RECOVERY_STAGE_BUILD_INPUT=non-i386-target"
    exit 1
endif
file "$bundleRoot/OpenStepMGAReplacementDisplay_reloc" | grep 'Mach-O preloaded' > /dev/null
if ($status != 0) then
    echo "OPENSTEP_MGA_RECOVERY_STAGE_BUILD_INPUT=invalid-reloc"
    exit 1
endif

if (-d "$workRoot") /bin/rm -rf "$workRoot"
/bin/mkdirs "$payloadRoot/DriverStaging"
/bin/mkdirs "$payloadRoot/Documentation/OpenStep-MGA-G450"
/bin/mkdirs "$payloadRoot/Tools"
cp -R "$bundleRoot" "$payloadRoot/DriverStaging/"
if ($status != 0) exit 1
cp "$sourceRoot/docs/reports/R3_G450_16M_DEPLOYMENT_MODE.md" \
   "$sourceRoot/docs/R1_DRIVERLOADER_CONFIGURATION_MODEL.md" \
   "$payloadRoot/Documentation/OpenStep-MGA-G450/"
if ($status != 0) exit 1
cc -m486 -o "$marker" "$markerSource"
if ($status != 0) exit 1
chmod 555 "$marker"

# Invoke the historical csh helper through its interpreter.  Executing that
# script directly from another csh can report a false command lookup failure.
/bin/csh -f "$packageTool" "$payloadRoot" "$info" -d "$distRoot"
if ($status != 0) exit 1
if (! -d "$distRoot/OpenStepMGARecoveryStaging.pkg") then
    echo "OPENSTEP_MGA_RECOVERY_STAGE_BUILD_OUTPUT=missing-package"
    exit 1
endif
cp "$preinstall" "$distRoot/OpenStepMGARecoveryStaging.pkg/OpenStepMGARecoveryStaging.pre_install"
if ($status != 0) exit 1
chmod 555 "$distRoot/OpenStepMGARecoveryStaging.pkg/OpenStepMGARecoveryStaging.pre_install"
echo "OPENSTEP_MGA_RECOVERY_STAGE_BUILD_STATUS=pass PACKAGE=$distRoot/OpenStepMGARecoveryStaging.pkg"
exit 0
