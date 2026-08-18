#!/bin/sh
# Keep R6 transaction policy independent of target interfaces.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_root=$(CDPATH= cd -- "$script_dir/../protocol" && pwd)

if [ ! -f "$source_root/OpenStepMGAModeTransaction.c" ] || \
   [ ! -f "$source_root/OpenStepMGAModeTransaction.h" ]; then
    echo "OPENSTEP_MGA_MODE_TRANSACTION_STATIC_GUARD_INPUT=missing" >&2
    exit 2
fi

if grep -n -E '(IO(Map|Physical|PCI|FrameBuffer|GetDisplay|SetDisplay)|IODirectDevice|pciConfig(Read|Write)|pci_config_(read|write)|in[bwl]|out[bwl]|MGA_(PCI|MMIO|DAC|PLL|CRTC)|DDC|EDID|I2C|DMA|interrupt|sleep|usleep)' \
        "$source_root/OpenStepMGAModeTransaction.c" "$source_root/OpenStepMGAModeTransaction.h"; then
    echo "OPENSTEP_MGA_MODE_TRANSACTION_STATIC_GUARD_STATUS=fail" >&2
    exit 1
fi

echo "OPENSTEP_MGA_MODE_TRANSACTION_STATIC_GUARD_STATUS=pass"
