#!/bin/csh -f
# Read-only comparator for the original MatroxMGA bundle.
# Baseline: R0-20260818-A.  OPENSTEP 4.2 has /usr/bin/sum but not cksum/md5.

set bundle = /usr/Devices/MatroxMGA.config
set failed = 0

if (! -d "$bundle") then
    echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=missing-bundle:$bundle"
    echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK_STATUS=fail"
    exit 1
endif

if (! -e "$bundle/MatroxMGA_reloc") then
    echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=missing:MatroxMGA_reloc"
    set failed = 1
else
    set actual_size = ( `wc -c < "$bundle/MatroxMGA_reloc"` )
    set actual_sum = ( `/usr/bin/sum "$bundle/MatroxMGA_reloc"` )
    if ($#actual_size != 1) then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=invalid-size:MatroxMGA_reloc"
        set failed = 1
    else if ("$actual_size[1]" != "104788") then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=size-mismatch:MatroxMGA_reloc:$actual_size[1]:104788"
        set failed = 1
    else if ($#actual_sum != 2) then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=invalid-sum:MatroxMGA_reloc"
        set failed = 1
    else if ("$actual_sum[1]" != "45628" || "$actual_sum[2]" != "103") then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=sum-mismatch:MatroxMGA_reloc:$actual_sum[1]:$actual_sum[2]:45628:103"
        set failed = 1
    else
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=pass:MatroxMGA_reloc"
    endif
endif

if (! -e "$bundle/MatroxMGA") then
    echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=missing:MatroxMGA"
    set failed = 1
else
    set actual_size = ( `wc -c < "$bundle/MatroxMGA"` )
    set actual_sum = ( `/usr/bin/sum "$bundle/MatroxMGA"` )
    if ($#actual_size != 1) then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=invalid-size:MatroxMGA"
        set failed = 1
    else if ("$actual_size[1]" != "1068") then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=size-mismatch:MatroxMGA:$actual_size[1]:1068"
        set failed = 1
    else if ($#actual_sum != 2) then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=invalid-sum:MatroxMGA"
        set failed = 1
    else if ("$actual_sum[1]" != "05204" || "$actual_sum[2]" != "2") then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=sum-mismatch:MatroxMGA:$actual_sum[1]:$actual_sum[2]:05204:2"
        set failed = 1
    else
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=pass:MatroxMGA"
    endif
endif

if (! -e "$bundle/Default.table") then
    echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=missing:Default.table"
    set failed = 1
else
    set actual_size = ( `wc -c < "$bundle/Default.table"` )
    set actual_sum = ( `/usr/bin/sum "$bundle/Default.table"` )
    if ($#actual_size != 1) then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=invalid-size:Default.table"
        set failed = 1
    else if ("$actual_size[1]" != "521") then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=size-mismatch:Default.table:$actual_size[1]:521"
        set failed = 1
    else if ($#actual_sum != 2) then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=invalid-sum:Default.table"
        set failed = 1
    else if ("$actual_sum[1]" != "60079" || "$actual_sum[2]" != "1") then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=sum-mismatch:Default.table:$actual_sum[1]:$actual_sum[2]:60079:1"
        set failed = 1
    else
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=pass:Default.table"
    endif
endif

if (! -e "$bundle/Instance0.table") then
    echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=missing:Instance0.table"
    set failed = 1
else
    set actual_size = ( `wc -c < "$bundle/Instance0.table"` )
    set actual_sum = ( `/usr/bin/sum "$bundle/Instance0.table"` )
    if ($#actual_size != 1) then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=invalid-size:Instance0.table"
        set failed = 1
    else if ("$actual_size[1]" != "617") then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=size-mismatch:Instance0.table:$actual_size[1]:617"
        set failed = 1
    else if ($#actual_sum != 2) then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=invalid-sum:Instance0.table"
        set failed = 1
    else if ("$actual_sum[1]" != "25212" || "$actual_sum[2]" != "1") then
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=sum-mismatch:Instance0.table:$actual_sum[1]:$actual_sum[2]:25212:1"
        set failed = 1
    else
        echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK=pass:Instance0.table"
    endif
endif

if ($failed != 0) then
    echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK_STATUS=fail"
    exit 1
endif

echo "OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK_STATUS=pass"
exit 0
