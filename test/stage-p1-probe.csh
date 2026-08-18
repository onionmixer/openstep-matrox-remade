#!/bin/csh -f
# Copy a built P1 probe bundle into a new /tmp staging directory without
# touching an earlier probe artifact.

if ($#argv != 2) then
    echo "usage: $0 <source-config-bundle> <new-staging-root>"
    exit 2
endif

set source_bundle = "$argv[1]"
set staging_root = "$argv[2]"
set staged_bundle = "$staging_root/OpenStepMGAProbe.config"

if (! -d "$source_bundle") then
    echo "OPENSTEP_MGA_P1_STAGE=missing-source"
    exit 1
endif
if (-e "$staging_root") then
    echo "OPENSTEP_MGA_P1_STAGE=refuse-existing:$staging_root"
    exit 1
endif

mkdir "$staging_root"
if ($status != 0) then
    echo "OPENSTEP_MGA_P1_STAGE=mkdir-failed"
    exit 1
endif
cp -r "$source_bundle" "$staging_root"
if ($status != 0) then
    echo "OPENSTEP_MGA_P1_STAGE=copy-failed"
    exit 1
endif
if (! -e "$staged_bundle/OpenStepMGAProbe_reloc") then
    echo "OPENSTEP_MGA_P1_STAGE=missing-reloc"
    exit 1
endif

echo "OPENSTEP_MGA_P1_STAGE=pass:$staged_bundle"
