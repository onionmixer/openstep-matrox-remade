#!/bin/sh
# What does THIS project touch in a `config=Default` boot?
#
#   sh .../tools/inventory-default-boot-footprint.sh
#
# Read-only.  Run it from the NORMAL configuration (the Default boot has no
# network, so it cannot be run there).
#
# The question behind it: a red rectangle appears low on the screen under
# `config=Default`, where this driver is NOT loaded -- checked three ways,
# all of which this script re-checks so the answer is not taken on trust.
# If nothing of ours is in that boot path, the artefact is not ours and the
# search should move elsewhere.
echo "=== 1. is our driver in the Default configuration at all? ==="
D=/private/Drivers/i386/System.config/Default.table
grep '^"Active Drivers"' $D
grep '^"Boot Drivers"'   $D
echo "  (our driver's name must appear in neither)"

echo
echo "=== 2. the other load path ==="
cat /etc/kern_loader.conf

echo
echo "=== 3. did anything of ours land outside our own bundle? ==="
for p in /etc/rc /etc/rc.boot /etc/rc.local /etc/rc.common; do
    if [ -r "$p" ]; then
        n=`grep -c -i osmga "$p"`
        m=`grep -c -i matrox "$p"`
        echo "  $p: osmga=$n matrox=$m"
    fi
done

echo
echo "=== 4. what the Default boot's display driver actually is ==="
ls -ld /private/Drivers/i386/VGA.config 2>/dev/null
if [ -d /private/Drivers/i386/VGA.config ]; then
    ls -l /private/Drivers/i386/VGA.config
fi

echo
echo "=== 5. when were the system configuration tables last written? ==="
ls -l /private/Drivers/i386/System.config

echo
echo "=== 6. anything of ours installed in a system path ==="
ls -d /usr/local/Documentation/OpenStep-MGA-G450 2>/dev/null
ls -d /private/Drivers/i386/OSMGADisplay.config 2>/dev/null
echo "  (both are expected; neither is in the Default boot path)"
