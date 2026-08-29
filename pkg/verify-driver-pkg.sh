#!/bin/sh
# Unpack a built driver package and check what is actually inside it.
#
#   sh .../pkg/verify-driver-pkg.sh [package-dir] [source-root]
#
# Everything here is a thing that has gone wrong somewhere before: a nib
# dropped by the old tar's path limit, a development table shipped with
# diagnostics on, build residue, an architecture marker nobody staged.
set -e
PKGDIR="${1:-/tmp/pkgout}"
SRC="${2:-/ndrv/openstep-matrox-remade}"
NAME=OSMGADisplay
PKG="$PKGDIR/$NAME.pkg"
UNPACK=/tmp/_mgadrvverify
D="$UNPACK/private/Drivers/i386/$NAME.config"
# This shell runs functions in a subshell, so a counter assigned inside one
# does not survive; the empty variable then makes `test` complain rather
# than compare.  Record failures in a file, which does survive.
FAILS=/tmp/_mgadrvverify.fails
rm -f "$FAILS"; : > "$FAILS"

note() { echo "  $1"; }
bad()  { echo "  FAIL: $1"; echo "$1" >> "$FAILS"; }

if [ ! -d "$PKG" ]; then echo "verify: no $PKG" >&2; exit 2; fi
# Unpack the way INSTALLER will, which is not the same thing as the way the
# payload was written.
#
# This used to read the payload back with installer_bigtar, because that is
# what wrote it -- and so the verifier passed on an archive Installer.app
# hangs on.  Written wrong, checked with the same wrong tool, green.  The
# check that matters is that installer_tar can consume it, so that is the
# tool, and a timeout is on it because the failure mode is a hang and not an
# error.
TAR=/NextAdmin/Installer.app/installer_tar
rm -rf "$UNPACK"; /bin/mkdirs "$UNPACK"
#
# A marker file rather than `kill -0` on the pid: this shell's kill writes
# "No such process" where the reader can see it once the job has finished
# normally, which makes a clean run look like a broken one.
#
rm -f /tmp/_drvpkg_done
( cd "$UNPACK" && /usr/ucb/zcat "$PKG/$NAME.tar.Z" | "$TAR" xf - ; \
  echo done > /tmp/_drvpkg_done ) &
waited=0
while [ ! -f /tmp/_drvpkg_done ]; do
    sleep 1
    waited=`expr $waited + 1`
    if [ $waited -gt 60 ]; then
        echo "verify: installer_tar did not finish in 60s -- this is the" >&2
        echo "verify: long-path hang, and Installer.app would do the same" >&2
        exit 1
    fi
done

#
# The .info keys Installer needs before it will even OPEN the package.
#
# DiskName was missing from this package from the first packaging commit,
# and Installer answers "contains no DiskName field" and refuses -- without
# ever reading the payload.  Every other package in this workspace had it;
# this one did not, and nothing checked.  So the check is here, by key name,
# rather than trusting that the file looks right.
#
echo "info keys"
for k in Title Version Description DefaultLocation DiskName; do
    if grep "^$k " "$PKG/$NAME.info" > /dev/null; then
        note "ok   $k"
    else
        bad "$k missing from $NAME.info -- Installer will refuse to open it"
    fi
done

echo "payload"
for f in "${NAME}_reloc" "$NAME" Default.table Instance0.table Display.modes \
         English.lproj/Localizable.strings; do
    if [ -r "$D/$f" ]; then note "ok   $f"; else bad "missing $f"; fi
done
for n in data.classes data.dependency data.nib; do
    if [ -r "$D/English.lproj/DisplayInspector.nib/$n" ]; then
        note "ok   nib/$n"
    else
        bad "nib file dropped: $n (the old tar's 100-char path limit)"
    fi
done

# Display.modes is a payload FILE that the presence check above already saw,
# and that is not enough: a package built from a stale bundle ships an old
# mode list and passes every other check here.  Compare it with the source and
# assert the list is the complete product of the two tables.
echo "the mode list"
if cmp -s "$SRC/OSMGADisplay/Display.modes" "$D/Display.modes"; then
    note "ok   Display.modes is byte-for-byte with the source copy"
else
    bad "Display.modes differs from the source -- rebuild the bundle"
fi
n=`grep -c Height "$D/Display.modes"`
u=`sort "$D/Display.modes" | uniq | grep -c Height`
if [ "$n" -eq 20 ]; then note "ok   20 mode entries"
else bad "$n mode entries, wanted 20"; fi
if [ "$n" -eq "$u" ]; then note "ok   no duplicate entries"
else bad "duplicate mode entries ($n listed, $u distinct)"; fi
# 5 resolutions x 4 formats, and BW:4 must be gone -- it is the old spelling
# and Display.modes is what stops anyone selecting it anew.
for c in "RGB:888/32" "RGB:555/16" "RGB:256/8" "BW:8"; do
    k=`grep "$c" "$D/Display.modes" | wc -l`
    if [ "$k" -eq 5 ]; then note "ok   $c at all 5 resolutions"
    else bad "$c appears $k times, wanted 5"; fi
done
if grep 'BW:4' "$D/Display.modes" > /dev/null; then
    bad "BW:4 is still in the mode list"
else
    note "ok   no BW:4 in the mode list"
fi

echo "exclusions"
if [ -f "$D/.lastBuildTime" -o -d "$D/.lastBuildTime" ]; then
    bad "build residue shipped"
else
    note "ok   no .lastBuildTime"
fi
if [ -f "$UNPACK/private/Drivers/i386/$NAME.config/System.config" ]; then
    bad "a System.config leaked into the payload"
else
    note "ok   no System.config"
fi

echo "the release instance table, not the development one"
for sw in "Raster Test" "VRAM Mmap" "Mesa Acceleration"; do
    if grep "\"$sw\" = \"Yes\"" "$D/Instance0.table" > /dev/null; then
        bad "$sw is Yes -- this is the development table"
    else
        note "ok   $sw is No"
    fi
done

echo "architecture"
if file "$D/${NAME}_reloc" | grep 'Mach-O preloaded' > /dev/null; then
    note "ok   relocatable is a preloaded Mach-O"
else
    bad "relocatable is not a preloaded Mach-O"
fi
if file "$D/$NAME" | grep i386 > /dev/null; then
    note "ok   inspector is i386"
else
    bad "inspector is not i386"
fi

echo "documentation"
# Present AND current.  A package built before the documents were edited
# passes a presence check and ships stale text; INSTALL.md gained the
# per-mode acceleration warning after one such build.
for f in LICENSE NOTICE INSTALL.md; do
    case "$f" in
    INSTALL.md) srcf="$SRC/release-packaging/INSTALL.md" ;;
    *)          srcf="$SRC/$f" ;;
    esac
    D2="$UNPACK/usr/local/Documentation/OpenStep-MGA-G450/$f"
    if [ ! -r "$D2" ]; then
        bad "missing $f"
    elif cmp -s "$srcf" "$D2"; then
        note "ok   $f is byte-for-byte with the source copy"
    else
        bad "$f differs from the source copy -- rebuild the package"
    fi
done
if grep 'Matrox Graphics Inc' "$UNPACK/usr/local/Documentation/OpenStep-MGA-G450/NOTICE" > /dev/null; then
    note "ok   the WARP notice travelled with it"
else
    bad "the WARP notice is not in the shipped NOTICE"
fi

n=`wc -l < "$FAILS"`
if [ "$n" -eq 0 ]; then
    echo "VERIFY_DRIVER_PKG=PASS"
else
    echo "VERIFY_DRIVER_PKG=FAIL ($n)"
    exit 1
fi
