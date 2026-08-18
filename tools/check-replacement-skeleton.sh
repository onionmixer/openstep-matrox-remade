#!/bin/sh
# Keep the R4 replacement display skeleton fail-closed and non-executable.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
bundle_root="$project_root/OpenStepMGAReplacementDisplay"
source_root="$bundle_root/OpenStepMGAReplacementDisplay_reloc.tproj"
source_file="$source_root/OpenStepMGAReplacementDisplay.m"
header_file="$source_root/OpenStepMGAReplacementDisplay.h"
table_file="$bundle_root/Default.table"
load_file="$source_root/Load_Commands.sect"
sanitized=$(mktemp "${TMPDIR:-/tmp}/openstep-mga-replacement-r4.XXXXXX")
trap 'rm -f "$sanitized"' EXIT HUP INT TERM

for required in "$source_file" "$header_file" "$table_file" "$load_file" \
    "$source_root/OpenStepMGAManualConfig.c" "$source_root/OpenStepMGAManualConfig.h" \
    "$bundle_root/Makefile" "$source_root/Makefile"; do
    if [ ! -f "$required" ]; then
        echo "OPENSTEP_MGA_REPLACEMENT_R4_INPUT=missing:$required" >&2
        exit 2
    fi
done

awk '
function emit(value) {
    if (value !~ /^[[:space:]]*$/) print FILENAME ":" FNR ":" value
}
{
    value = $0
    for (;;) {
        if (in_comment) {
            end = index(value, "*/")
            if (end == 0) { value = ""; break }
            value = substr(value, end + 2); in_comment = 0; continue
        }
        block = index(value, "/*"); line = index(value, "//")
        if (block == 0 && line == 0) break
        if (line != 0 && (block == 0 || line < block)) {
            value = substr(value, 1, line - 1); break
        }
        before = substr(value, 1, block - 1); rest = substr(value, block + 2)
        end = index(rest, "*/")
        if (end == 0) { value = before; in_comment = 1; break }
        value = before substr(rest, end + 2)
    }
    emit(value)
}
' "$source_file" "$header_file" > "$sanitized"

failure=0
reject() {
    label=$1
    pattern=$2
    if grep -n -E "$pattern" "$sanitized"; then
        echo "OPENSTEP_MGA_REPLACEMENT_R4_REJECT=$label" >&2
        failure=1
    fi
}

# IOFrameBufferDisplay itself is the declared lifecycle superclass.  Everything
# below would prematurely turn this R4 source into a device or framebuffer path.
reject "physical-or-framebuffer-map" \
'(mapFrameBufferAtPhysicalAddress|mapMemoryRange|unmapMemoryRange|IOMapPhysicalIntoIOTask|IOUnmapPhysicalFromIOTask|IOPhysicalFromVirtual)'
reject "device-or-pci-binding" \
'(IOPCIDirectDevice|IODirectDevice|IOEISADeviceDescription|pciConfig(Read|Write)|pci_config_(read|write)|MGA_(PCI|MMIO))'
reject "direct-port-io" '\<(inb|inw|inl|outb|outw|outl)\>[[:space:]]*\('
reject "display-programming" \
'(selectMode|setPendingDisplayMode|setGammaTable|initializeMode|enableLinearFrameBuffer|resetVGA|DDC|EDID|I2C)'

# Future R6 policy modules are host/offline artifacts.  R4 must not link a
# transaction policy and accidentally imply that a target transition is ready.
if grep -n -E 'OpenStepMGAModeTransaction|OpenStepMGABoundedPoll|OpenStepMGAG450PLL|OpenStepMGAMappingReview|OpenStepMGAOffscreen2D|OpenStepMGAOffscreenAllocator' \
        "$source_file" "$header_file" "$source_root/Makefile"; then
    echo "OPENSTEP_MGA_REPLACEMENT_R4_REJECT=r6-policy-linkage" >&2
    failure=1
fi

if ! grep -q '^"Family" = "Display";$' "$table_file" || \
   ! grep -q '^"Driver Name" = "OpenStepMGAReplacementDisplay";$' "$table_file" || \
   ! grep -q '^"Memory Maps" = "";$' "$table_file" || \
   ! grep -q '^"I/O Ports" = "";$' "$table_file"; then
    echo "OPENSTEP_MGA_REPLACEMENT_R4_REJECT=staging-table" >&2
    failure=1
fi
if grep -n -E '"(Auto Detect IDs|Display Mode|FB Address)"[[:space:]]*=' "$table_file"; then
    echo "OPENSTEP_MGA_REPLACEMENT_R4_REJECT=device-or-mode-table" >&2
    failure=1
fi
if ! grep -q '^"MGA Memory Size" = "";$' "$table_file"; then
    echo "OPENSTEP_MGA_REPLACEMENT_R4_REJECT=manual-memory-key" >&2
    failure=1
fi
if grep -n -E '^[[:space:]]*(START|CALL)[[:space:]]' "$load_file"; then
    echo "OPENSTEP_MGA_REPLACEMENT_R4_REJECT=load-entry" >&2
    failure=1
fi

for signature in \
    '+ (BOOL)probe:deviceDescription' \
    '- initFromDeviceDescription:deviceDescription' \
    '- (void)enterLinearMode' \
    '- (void)revertToVGAMode' \
    '- (unsigned int)displayMemorySize' \
    '- (unsigned int)ramdacSpeed' \
    '- setBrightness:(int)level token:(int)token' \
    '- free'; do
    if ! grep -F -q -- "$signature" "$source_file"; then
        echo "OPENSTEP_MGA_REPLACEMENT_R4_REJECT=missing-lifecycle:$signature" >&2
        failure=1
    fi
done
if ! grep -q 'return NO;' "$source_file" || \
   [ "$(grep -F -c 'return [super free];' "$source_file")" -lt 2 ]; then
    echo "OPENSTEP_MGA_REPLACEMENT_R4_REJECT=not-fail-closed" >&2
    failure=1
fi

if [ "$failure" -ne 0 ]; then
    echo "OPENSTEP_MGA_REPLACEMENT_R4_STATUS=fail" >&2
    exit 1
fi
echo "OPENSTEP_MGA_REPLACEMENT_R4_STATUS=pass"
