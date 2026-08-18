#!/bin/sh
# Keep the D0 EDID policy module pure C and independent of target hardware.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
source_root="$project_root/edid"
sanitized=$(mktemp "${TMPDIR:-/tmp}/openstep-mga-d0-source.XXXXXX")
trap 'rm -f "$sanitized"' EXIT HUP INT TERM

if [ ! -f "$source_root/OpenStepMGAEDID.c" ] || \
   [ ! -f "$source_root/OpenStepMGAEDID.h" ]; then
    echo "OPENSTEP_MGA_D0_STATIC_GUARD_INPUT=missing" >&2
    exit 2
fi

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
' "$source_root/OpenStepMGAEDID.c" "$source_root/OpenStepMGAEDID.h" > "$sanitized"

failure=0
reject() {
    label=$1
    pattern=$2
    if grep -n -E "$pattern" "$sanitized"; then
        echo "OPENSTEP_MGA_D0_STATIC_GUARD_REJECT=$label" >&2
        failure=1
    fi
}

reject "system-header" '^[^:]+:[0-9]+:#[[:space:]]*(include|import)[[:space:]]*<'
reject "driver-or-mach-api" \
'(IO(Map|Physical|PCI|FrameBuffer|GetDisplay|SetDisplay)|IODirectDevice|port_(allocate|deallocate)|task_self|kern_serv_)'
reject "direct-hardware-io" '\<(inb|inw|inl|outb|outw|outl|pciConfig(Read|Write)|pci_config_(read|write)|MGA_(PCI|MMIO))\>'

if [ "$failure" -ne 0 ]; then
    echo "OPENSTEP_MGA_D0_STATIC_GUARD_STATUS=fail" >&2
    exit 1
fi

echo "OPENSTEP_MGA_D0_STATIC_GUARD_STATUS=pass"
