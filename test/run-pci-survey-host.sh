#!/bin/sh
# C89 host regression for the PCI aperture survey, over a synthetic bus.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
source_root="$project_root/OpenStepMGAReplacementDisplay/OpenStepMGAReplacementDisplay_reloc.tproj"
edid_root="$project_root/edid"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-pci-survey.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$source_root" -I"$edid_root" \
   "$source_root/OpenStepMGAPciSurvey.c" \
   "$source_root/OpenStepMGAWindowMath.c" \
   "$source_root/OpenStepMGAManualConfig.c" \
   "$edid_root/OpenStepMGAEDID.c" \
   "$script_dir/openstep-mga-pci-survey-test.c" \
   -o "$work_dir/openstep-mga-pci-survey-test"
"$work_dir/openstep-mga-pci-survey-test"
echo "OPENSTEP_MGA_PCI_SURVEY_HOST_TEST=pass"
