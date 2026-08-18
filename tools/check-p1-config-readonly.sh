#!/bin/sh
# Ensure the P1/P1.4 probe remains config-header read-only.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_file="$script_dir/../OpenStepMGAProbe/OpenStepMGAProbe_reloc.tproj/OpenStepMGAProbe.m"
sanitized=$(mktemp "${TMPDIR:-/tmp}/openstep-mga-p1-source.XXXXXX")
trap 'rm -f "$sanitized"' EXIT HUP INT TERM

if [ ! -f "$source_file" ]; then
    echo "OPENSTEP_MGA_P1_STATIC_GUARD_INPUT=missing" >&2
    exit 2
fi

awk '
function emit(value) { if (value !~ /^[[:space:]]*$/) print FILENAME ":" FNR ":" value }
{
    value = $0
    for (;;) {
        if (in_comment) { end = index(value, "*/"); if (end == 0) { value = ""; break }; value = substr(value, end + 2); in_comment = 0; continue }
        block = index(value, "/*"); line = index(value, "//")
        if (block == 0 && line == 0) break
        if (line != 0 && (block == 0 || line < block)) { value = substr(value, 1, line - 1); break }
        before = substr(value, 1, block - 1); rest = substr(value, block + 2); end = index(rest, "*/")
        if (end == 0) { value = before; in_comment = 1; break }
        value = before substr(rest, end + 2)
    }
    emit(value)
}
' "$source_file" > "$sanitized"

failure=0
reject() {
    label=$1
    pattern=$2
    if grep -n -E "$pattern" "$sanitized"; then
        echo "OPENSTEP_MGA_P1_STATIC_GUARD_REJECT=$label" >&2
        failure=1
    fi
}

reject "mapping-or-device-api" \
'(IOMapPhysicalIntoIOTask|IOUnmapPhysicalFromIOTask|IOPhysicalFromVirtual|mapFrameBufferAtPhysicalAddress|mapMemoryRange|IODirectDevice|IOPCIDirectDevice)'
reject "device-config-write" \
'(pciConfigWrite|pci_config_write|PCI_CFG_DATA[[:space:]]*,|out[bwl][[:space:]]*\([^\n]*PCI_CFG_DATA)'
reject "engine-or-display-path" \
'(MGA_(MMIO|ENGINE|DAC|PLL|CRTC)|DDC|EDID|I2C|DMA|interruptOccurred)'

if ! grep -F -q 'outl((IOEISAPortAddress)PCI_CFG_ADDR, address);' "$source_file" || \
   ! grep -F -q 'return inl((IOEISAPortAddress)PCI_CFG_DATA);' "$source_file"; then
    echo "OPENSTEP_MGA_P1_STATIC_GUARD_REJECT=config-read-primitive" >&2
    failure=1
fi
if ! grep -F -q 'MGA-PROBE VPD-capability-present no-vpd-data-access' "$source_file" || \
   ! grep -F -q '#define PCI_CAP_MAX_HOPS    48' "$source_file"; then
    echo "OPENSTEP_MGA_P1_STATIC_GUARD_REJECT=capability-boundary" >&2
    failure=1
fi

if [ "$failure" -ne 0 ]; then
    echo "OPENSTEP_MGA_P1_STATIC_GUARD_STATUS=fail" >&2
    exit 1
fi
echo "OPENSTEP_MGA_P1_STATIC_GUARD_STATUS=pass"
