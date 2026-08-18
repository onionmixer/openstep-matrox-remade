#!/bin/csh -f
#
# P2.8 target-native binary import gate.
#
# Usage:
#   csh -f check-p2-binary-imports.csh <OpenStepMGAService_reloc>
#
# This only runs /bin/nm -u on an already-built relocatable.  It does not load
# the LKS and does not access a PCI device, BAR, framebuffer, DMA, IRQ or DDC.

if ($#argv != 1) then
    echo "usage: $0 <OpenStepMGAService_reloc>"
    exit 2
endif

set reloc = "$argv[1]"
set imports = /tmp/openstep-mga-p28-imports.$$

if (! -f "$reloc") then
    echo "OPENSTEP_MGA_P28_BINARY_GUARD_INPUT=missing"
    exit 2
endif

/bin/rm -f "$imports"
/bin/nm -u "$reloc" >! "$imports"
if ($status != 0) then
    /bin/rm -f "$imports"
    echo "OPENSTEP_MGA_P28_BINARY_GUARD_NM=fail"
    exit 1
endif

# P2 may import generic Mach/kernserv/IOLog support only.  Any symbol below
# would indicate physical mapping, PCI ownership/configuration, display
# lifecycle, DMA, direct I/O, or a predeclared MGA hardware helper.
egrep '(IOMapPhysicalIntoIOTask|IOUnmapPhysicalFromIOTask|IOPhysicalFromVirtual|IOPCIDirectDevice|IODirectDevice|IOFrameBufferDisplay|IOGetDisplayInfo|IOSetDisplay|pciConfig(Read|Write)|pci_config_(read|write)|_?(inb|inw|inl|outb|outw|outl)|MGA_(PCI|MMIO))' "$imports"
set prohibited = $status

if ($prohibited == 0) then
    /bin/rm -f "$imports"
    echo "OPENSTEP_MGA_P28_BINARY_GUARD_STATUS=fail"
    exit 1
endif
if ($prohibited != 1) then
    /bin/rm -f "$imports"
    echo "OPENSTEP_MGA_P28_BINARY_GUARD_GREP=fail"
    exit 1
endif

# The P2.8 baseline is intentionally closed.  Even a non-prohibited new
# undefined import needs review because it can conceal an indirect hardware
# path.  The list was obtained from the target's actual P2 relocatable.
egrep -v '^(\.objc_class_name_IODevice|\.objc_class_name_Object|_IOLog|_kern_serv_kernel_task_port|_kern_serv_notify|_kern_serv_notify_port|_kern_serv_port_gone|_port_deallocate_EXTERNAL)$' "$imports"
set unexpected = $status
if ($unexpected == 0) then
    /bin/rm -f "$imports"
    echo "OPENSTEP_MGA_P28_BINARY_GUARD_UNEXPECTED_IMPORT=fail"
    echo "OPENSTEP_MGA_P28_BINARY_GUARD_STATUS=fail"
    exit 1
endif
if ($unexpected != 1) then
    /bin/rm -f "$imports"
    echo "OPENSTEP_MGA_P28_BINARY_GUARD_GREP=fail"
    exit 1
endif

/bin/rm -f "$imports"
echo "OPENSTEP_MGA_P28_BINARY_GUARD_STATUS=pass"
exit 0
