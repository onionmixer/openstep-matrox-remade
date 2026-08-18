#!/bin/csh -f
# Read-only collection for the current original-driver configuration snapshot.
# No Configure, driverLoader, kl_util mutation, package installation, or device
# access is performed.

echo "OPENSTEP_MGA_R1_DRIVERLOADER_BASELINE=begin"

if (-e /private/Devices) then
    ls -ld /private/Devices
else
    echo "OPENSTEP_MGA_R1_PRIVATE_DEVICES=missing"
endif

if (-e /private/Drivers/i386/MatroxMGA.config/Instance0.table) then
    echo "OPENSTEP_MGA_R1_MATROX_INSTANCE=present"
    egrep '"(Driver Name|Server Name|Default Table|Location|Display Mode)"' \
        /private/Drivers/i386/MatroxMGA.config/Instance0.table
else
    echo "OPENSTEP_MGA_R1_MATROX_INSTANCE=missing"
endif

if (-e /etc/rc) then
    grep 'driverLoader a' /etc/rc
    if ($status != 0) then
        echo "OPENSTEP_MGA_R1_RC_DRIVERLOADER_A=not-found"
    endif
else
    echo "OPENSTEP_MGA_R1_RC=missing"
endif

echo "OPENSTEP_MGA_R1_DRIVERLOADER_REFERENCES=begin"
foreach candidate (/etc/rc /etc/rc.boot /etc/rc.local /etc/rc.common /etc/rc.net \
                   /etc/rc.shutdown /etc/rc.init /etc/rc.sysinit)
    if (-e $candidate) then
        egrep -n 'driverLoader' $candidate
        if ($status == 0) then
            echo "OPENSTEP_MGA_R1_DRIVERLOADER_REFERENCE_SOURCE=$candidate"
        endif
    endif
end
echo "OPENSTEP_MGA_R1_DRIVERLOADER_REFERENCES=end"

if (-e /etc/rc) then
    echo "OPENSTEP_MGA_R1_RC_CONTEXT=begin"
    sed -n '50,57p' /etc/rc
    echo "OPENSTEP_MGA_R1_RC_CONTEXT=end"
endif

echo "OPENSTEP_MGA_R1_DRIVERLOADER_BASELINE=end"
