#!/bin/csh -f
#
# Confirm that the R4 display skeleton binary has no mapping, register, DDC,
# or engine interface imports.  This is an import gate only; it never loads
# the kernel server or asks DriverKit to probe the MGA device.
#

if ($#argv != 1) then
    echo "usage: $0 <OpenStepMGAReplacementDisplay_reloc>"
    exit 2
endif

set reloc = "$argv[1]"
if (! -e "$reloc") then
    echo "OPENSTEP_MGA_REPLACEMENT_R4_IMPORT_INPUT=missing"
    exit 2
endif

set unexpected = "/tmp/openstep-mga-r4-imports-$$"
/bin/nm -u "$reloc" | awk '{print $NF}' | \
    egrep -v '^(.objc_class_name_(IODevice|IOFrameBufferDisplay|Object)|_IOLog|_objc_msgSend|_objc_msgSendSuper)$' >! "$unexpected"

/bin/test -s "$unexpected"
if ($status == 0) then
    echo "OPENSTEP_MGA_REPLACEMENT_R4_IMPORT_STATUS=fail"
    echo -n "OPENSTEP_MGA_REPLACEMENT_R4_IMPORT_UNEXPECTED="
    cat "$unexpected"
    /bin/rm -f "$unexpected"
    exit 1
endif

/bin/rm -f "$unexpected"

echo "OPENSTEP_MGA_REPLACEMENT_R4_IMPORT_STATUS=pass"
