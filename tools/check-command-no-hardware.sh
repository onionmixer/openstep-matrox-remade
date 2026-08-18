#!/bin/sh
# Command validation must remain a geometry-only, no-hardware boundary.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_root=$(CDPATH= cd -- "$script_dir/../protocol" && pwd)
sanitized=$(mktemp "${TMPDIR:-/tmp}/openstep-mga-command.XXXXXX")
trap 'rm -f "$sanitized"' EXIT HUP INT TERM

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
' "$source_root/OpenStepMGACommand.c" "$source_root/OpenStepMGACommand.h" > "$sanitized"

if grep -n -E '^[^:]+:[0-9]+:#[[:space:]]*(include|import)[[:space:]]*<|(IO(Map|Physical|PCI|FrameBuffer|GetDisplay|SetDisplay)|IODirectDevice|task_self|port_(allocate|deallocate)|kern_serv_|inb|inw|inl|outb|outw|outl|pciConfig(Read|Write)|pci_config_(read|write)|MGA_(PCI|MMIO))' "$sanitized"; then
    echo "OPENSTEP_MGA_COMMAND_STATIC_GUARD_STATUS=fail" >&2
    exit 1
fi
echo "OPENSTEP_MGA_COMMAND_STATIC_GUARD_STATUS=pass"
