#!/bin/sh
# Ensure the package-level OSMesa fallback smoke remains strictly off-screen.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_file="$script_dir/../test/openstep-mga-osmesa-fallback.c"
runner_file="$script_dir/../test/run-osmesa-fallback-target.csh"

if [ ! -f "$source_file" ] || [ ! -f "$runner_file" ]; then
    echo "OPENSTEP_MGA_OSMESA_FALLBACK_STATIC_GUARD_INPUT=missing" >&2
    exit 2
fi

if grep -n -E '(AppKit|NSApplication|GCD|DriverKit|IOFrameBuffer|IODirectDevice|PCI|FrameBuffer|GetDisplay|SetDisplay|IO(Map|Physical)|pciConfig(Read|Write)|pci_config_(read|write)|in[bwl]|out[bwl]|MGA_(PCI|MMIO|DAC|PLL|CRTC)|DDC|I2C|DMA|interrupt)' \
        "$source_file" "$runner_file"; then
    echo "OPENSTEP_MGA_OSMESA_FALLBACK_STATIC_GUARD_STATUS=fail" >&2
    exit 1
fi

echo "OPENSTEP_MGA_OSMESA_FALLBACK_STATIC_GUARD_STATUS=pass"
