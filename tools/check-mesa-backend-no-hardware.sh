#!/bin/sh
# Keep the Mesa dispatch contract independent of target interfaces.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_root=$(CDPATH= cd -- "$script_dir/../mesa" && pwd)

if [ ! -f "$source_root/OpenStepMGAMesaBackend.c" ] || \
   [ ! -f "$source_root/OpenStepMGAMesaBackend.h" ]; then
    echo "OPENSTEP_MGA_MESA_BACKEND_STATIC_GUARD_INPUT=missing" >&2
    exit 2
fi

if grep -n -E '(IO(Map|Physical|PCI|FrameBuffer|GetDisplay|SetDisplay)|IODirectDevice|pciConfig(Read|Write)|pci_config_(read|write)|in[bwl]|out[bwl]|MGA_(PCI|MMIO|DAC|PLL|CRTC)|DDC|I2C|DMA|interrupt)' \
        "$source_root/OpenStepMGAMesaBackend.c" "$source_root/OpenStepMGAMesaBackend.h"; then
    echo "OPENSTEP_MGA_MESA_BACKEND_STATIC_GUARD_STATUS=fail" >&2
    exit 1
fi

echo "OPENSTEP_MGA_MESA_BACKEND_STATIC_GUARD_STATUS=pass"
