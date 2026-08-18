#!/bin/sh
# Keep the manual configuration parser independent of every MGA hardware path.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_root=$(CDPATH= cd -- "$script_dir/../OpenStepMGAReplacementDisplay/OpenStepMGAReplacementDisplay_reloc.tproj" && pwd)

if [ ! -f "$source_root/OpenStepMGAManualConfig.c" ] || \
   [ ! -f "$source_root/OpenStepMGAManualConfig.h" ]; then
    echo "OPENSTEP_MGA_MANUAL_CONFIG_STATIC_GUARD_INPUT=missing" >&2
    exit 2
fi

if grep -n -E '(IO(Map|Physical|PCI|FrameBuffer|GetDisplay|SetDisplay)|IODirectDevice|pciConfig(Read|Write)|pci_config_(read|write)|in[bwl]|out[bwl]|MGA_(PCI|MMIO|DAC|PLL|CRTC)|DDC|EDID|I2C|DMA|interrupt)' \
        "$source_root/OpenStepMGAManualConfig.c" "$source_root/OpenStepMGAManualConfig.h"; then
    echo "OPENSTEP_MGA_MANUAL_CONFIG_STATIC_GUARD_STATUS=fail" >&2
    exit 1
fi

echo "OPENSTEP_MGA_MANUAL_CONFIG_STATIC_GUARD_STATUS=pass"
