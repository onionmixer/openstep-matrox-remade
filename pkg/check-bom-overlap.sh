#!/bin/sh
# No two packages may claim the same file.
#
#   sh .../pkg/check-bom-overlap.sh [driver-pkgdir] [mesa-dist]
#
# Read-only.  Compares this project's package BOMs against the Mesa port's
# and fails if any FILE is listed in more than one.
#
# This is a release gate because it caught a real one: the accelerated
# library's package used to write Mesa's COPYRIGHT and COPYING into
# Documentation/OpenStep-Mesa-3.4.2/, which OpenStepMesa342Headers.pkg
# already owns.  Both copies were byte-identical, so nothing looked wrong on
# an install -- the hazard is on REMOVAL, and whether this Installer
# reference-counts a shared path is not established anywhere available.
#
# Directories are deliberately NOT compared.  Every package at a shared
# prefix claims Documentation/ and Tools/, and that is normal and
# unavoidable.  Only regular files (BOM mode 100...) are checked.
PKGDIR="${1:-/tmp/pkgout}"
MESADIST="${2:-/usr/local/mesastage/OpenStepMesa342/dist}"
W=/tmp/_bomoverlap
rm -rf "$W"; /bin/mkdirs "$W"

: > "$W/mine"
for p in OSMGADisplay OSMGAMesaAccel; do
    b="$PKGDIR/$p.pkg/$p.bom"
    if [ ! -r "$b" ]; then
        echo "check-bom-overlap: no $b" >&2
        exit 1
    fi
    lsbom "$b" | awk '$2 ~ /^100/ {print $1}' >> "$W/mine"
done

: > "$W/theirs"
for p in OpenStepMesa342Libraries OpenStepMesa342Headers OpenStepMesa342DemosMGA; do
    b="$MESADIST/$p.pkg/$p.bom"
    if [ -r "$b" ]; then
        lsbom "$b" | awk '$2 ~ /^100/ {print $1}' >> "$W/theirs"
    else
        echo "  (skipped, not built: $p)"
    fi
done

sort "$W/mine"   > "$W/mine_s"
sort "$W/theirs" > "$W/theirs_s"
echo "  this project: `wc -l < $W/mine_s` files"
echo "  the Mesa port: `wc -l < $W/theirs_s` files"

# The driver package installs at / and the others at a relocatable prefix, so
# a path shared between THOSE two would be a coincidence of naming rather
# than a real collision.  It is still reported: a coincidence worth knowing.
comm -12 "$W/mine_s" "$W/theirs_s" > "$W/both"
n=`wc -l < "$W/both"`
if [ "$n" -eq 0 ]; then
    echo "CHECK_BOM_OVERLAP=PASS (no file is claimed by two packages)"
else
    echo "CHECK_BOM_OVERLAP=FAIL ($n claimed by two packages)"
    cat "$W/both"
    exit 1
fi
