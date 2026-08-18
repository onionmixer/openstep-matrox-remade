#!/bin/sh
#
# Refuse P2 control-plane sources that acquire or address MGA hardware.
#
# This is a host-side source guard, not a security boundary or a substitute
# for target review.  It intentionally checks only the P2 service bundle;
# P1's read-only PCI probe and future, separately admitted P3 code are out
# of scope.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
service_root="$project_root/OpenStepMGAService"
source_root="$service_root/OpenStepMGAService_reloc.tproj"
sanitized=$(mktemp "${TMPDIR:-/tmp}/openstep-mga-p2-source.XXXXXX")
trap 'rm -f "$sanitized"' EXIT HUP INT TERM

if [ ! -f "$source_root/OpenStepMGAService.m" ] || \
   [ ! -f "$source_root/OpenStepMGA.defs" ] || \
   [ ! -f "$service_root/Default.table" ] || \
   [ ! -f "$source_root/Load_Commands.sect" ]; then
    echo "OPENSTEP_MGA_P2_STATIC_GUARD_INPUT=missing" >&2
    exit 2
fi

# Remove C and C++ comments before looking for actual imports/calls.  The
# P2 sources deliberately describe prohibited APIs in comments, so scanning
# raw text would make the guard both noisy and ineffective.
awk '
function emit(value) {
    if (value !~ /^[[:space:]]*$/) {
        print FILENAME ":" FNR ":" value
    }
}
{
    value = $0
    for (;;) {
        if (in_comment) {
            end = index(value, "*/")
            if (end == 0) {
                value = ""
                break
            }
            value = substr(value, end + 2)
            in_comment = 0
            continue
        }

        block = index(value, "/*")
        line = index(value, "//")
        if (block == 0 && line == 0) {
            break
        }
        if (line != 0 && (block == 0 || line < block)) {
            value = substr(value, 1, line - 1)
            break
        }

        before = substr(value, 1, block - 1)
        rest = substr(value, block + 2)
        end = index(rest, "*/")
        if (end == 0) {
            value = before
            in_comment = 1
            break
        }
        value = before substr(rest, end + 2)
    }
    emit(value)
}
' "$source_root/OpenStepMGAService.m" "$source_root/OpenStepMGA.defs" \
    "$source_root/OpenStepMGAProtocol.h" > "$sanitized"

failure=0

reject_source() {
    label=$1
    pattern=$2
    if grep -n -E "$pattern" "$sanitized"; then
        echo "OPENSTEP_MGA_P2_STATIC_GUARD_REJECT=$label" >&2
        failure=1
    fi
}

# These APIs/types would establish a device, framebuffer, physical mapping,
# or DMA path.  P2 has no legitimate use for any of them.
reject_source "driverkit-hardware-api" \
'(IOMapPhysicalIntoIOTask|IOUnmapPhysicalFromIOTask|IOPhysicalFromVirtual|IOPCIDirectDevice|IODirectDevice|IOFrameBufferDisplay|IOGetDisplayInfo|IOSetDisplay[A-Za-z0-9_]*)'
reject_source "port-io" '\<(inb|inw|inl|outb|outw|outl)\>[[:space:]]*\('
reject_source "pci-config-access" '\<(pciConfig(Read|Write)|pci_config_(read|write)|MGA_(PCI|MMIO))\>'

# P2 has a deliberately closed MiG surface: capability query plus software
# lease acquire/release.  A new routine must receive its own P3 admission
# review rather than silently becoming part of the control-plane ABI.
routine_names=$(sed -n 's/^[[:space:]]*routine[[:space:]]*\([A-Za-z_][A-Za-z_]*\).*/\1/p' \
    "$source_root/OpenStepMGA.defs" | tr '\n' ' ')
if [ "$routine_names" != "protocol_info query_capabilities acquire release " ]; then
    echo "OPENSTEP_MGA_P2_STATIC_GUARD_REJECT=mig-surface" >&2
    exit 1
fi

# A P2 bundle must remain unbound to the MGA PCI device and without device
# resources.  Port-death is a Mach lifecycle callback, not an IRQ path.
if grep -n -E '"(Auto Detect IDs|Location|FB Address)"[[:space:]]*=[[:space:]]*"[^"].*"' \
        "$service_root/Default.table"; then
    echo "OPENSTEP_MGA_P2_STATIC_GUARD_REJECT=device-binding-table" >&2
    failure=1
fi
if ! grep -q '^"IRQ Levels" = "";$' "$service_root/Default.table" || \
   ! grep -q '^"DMA Channels" = "";$' "$service_root/Default.table" || \
   ! grep -q '^"Memory Maps" = "";$' "$service_root/Default.table" || \
   ! grep -q '^"I/O Ports" = "";$' "$service_root/Default.table"; then
    echo "OPENSTEP_MGA_P2_STATIC_GUARD_REJECT=resource-table" >&2
    failure=1
fi
if grep -n -E '^[[:space:]]*(START|WIRE)[[:space:]]' \
        "$source_root/Load_Commands.sect"; then
    echo "OPENSTEP_MGA_P2_STATIC_GUARD_REJECT=load-command-hardware-path" >&2
    failure=1
fi

if [ "$failure" -ne 0 ]; then
    echo "OPENSTEP_MGA_P2_STATIC_GUARD_STATUS=fail" >&2
    exit 1
fi

echo "OPENSTEP_MGA_P2_STATIC_GUARD_STATUS=pass"
