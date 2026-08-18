#!/bin/csh -f
# Read-only identity record for the known-good original MatroxMGA bundle.
# OPENSTEP 4.2 provides /usr/bin/sum but not cksum/md5 on this target.

set failed = 0

echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT=begin"
foreach candidate (/private/Drivers/i386/MatroxMGA.config/MatroxMGA_reloc \
                   /private/Drivers/i386/MatroxMGA.config/MatroxMGA \
                   /private/Drivers/i386/MatroxMGA.config/Default.table \
                   /private/Drivers/i386/MatroxMGA.config/Instance0.table)
    if (! -e $candidate) then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FILE=missing:$candidate"
        set failed = 1
    else
        ls -l $candidate
        /usr/bin/sum $candidate
    endif
end
echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT=end"

if ($failed != 0) then
    echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_STATUS=fail"
    exit 1
endif
echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_STATUS=pass"
