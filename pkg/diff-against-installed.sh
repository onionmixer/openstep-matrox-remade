#!/bin/sh
# What would installing the driver package actually change on THIS machine?
#
#   sh .../pkg/diff-against-installed.sh [pkgdir]
#
# Read-only.  It unpacks the package into /tmp and compares it, file by
# file, against the bundle that is installed and running.  Nothing is
# written outside /tmp.
#
# The question this answers is the one worth asking before an install
# rehearsal: an installer that reports success tells you it wrote files, not
# which of your files it replaced.
PKGDIR="${1:-/tmp/pkgout}"
NAME=OSMGADisplay
PKG="$PKGDIR/$NAME.pkg"
UNPACK=/tmp/_mgadrvdiff
LIVE=/private/Drivers/i386/$NAME.config
BIGTAR=/NextAdmin/Installer.app/installer_bigtar

if [ ! -d "$PKG" ]; then
    echo "diff-against-installed: no $PKG" >&2
    exit 1
fi
rm -rf "$UNPACK"; /bin/mkdirs "$UNPACK"
( cd "$UNPACK" && /usr/ucb/zcat "$PKG/$NAME.tar.Z" | "$BIGTAR" xf - )
NEW="$UNPACK/private/Drivers/i386/$NAME.config"

echo "the driver bundle"
for f in ${NAME}_reloc $NAME Default.table Instance0.table Display.modes \
         English.lproj/Localizable.strings \
         English.lproj/DisplayInspector.nib/data.classes \
         English.lproj/DisplayInspector.nib/data.dependency \
         English.lproj/DisplayInspector.nib/data.nib; do
    if [ ! -r "$LIVE/$f" ]; then
        echo "  ADDED     $f"
    elif cmp -s "$NEW/$f" "$LIVE/$f"; then
        echo "  same      $f"
    else
        echo "  REPLACED  $f"
    fi
done

echo "files the installed bundle has and the package does not"
found=0
for f in `cd "$LIVE" && find . -type f -print`; do
    if [ ! -r "$NEW/$f" ]; then
        echo "  LEFT ALONE (not in the package)  $f"
        found=1
    fi
done
if [ "$found" -eq 0 ]; then echo "  none"; fi

echo "the switches, which is where a surprise would live"
for k in "Raster Test" "VRAM Mmap" "Mesa Acceleration" "Storm 2D Test" \
         "DMA Ring Test" "WARP Test" "Display Mode"; do
    # Anchored, because the release table's header EXPLAINS each key in a
    # comment and an unanchored match picks the prose up as if it were the
    # value -- which made three unchanged keys report as changed.
    now=`grep "^\"$k\" =" "$LIVE/Instance0.table"`
    new=`grep "^\"$k\" =" "$NEW/Instance0.table"`
    if [ "$now" = "$new" ]; then
        echo "  same      $k"
    else
        echo "  CHANGES   $k"
        echo "      installed: $now"
        echo "      package  : $new"
    fi
done

echo "documentation the package also writes"
for f in LICENSE NOTICE INSTALL.md; do
    d=/usr/local/Documentation/OpenStep-MGA-G450/$f
    if [ -r "$d" ]; then echo "  REPLACED  $d"; else echo "  ADDED     $d"; fi
done
