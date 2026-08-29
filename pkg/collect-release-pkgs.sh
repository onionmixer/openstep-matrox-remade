#!/bin/sh
# Copy the three finished .pkg directories out of their build locations and
# into the repository, where the host can archive them.
#
#   sh .../pkg/collect-release-pkgs.sh [source-root] [driver-pkgdir] [mesa-dist]
#
# Runs ON the target.  It only copies; it builds nothing and verifies
# nothing, so run the three verifiers first.
#
# The three packages are built in three different places, which is not
# tidiness but a consequence of who owns what: the driver and the
# acceleration package are this project's and are built wherever the operator
# points the build script, while the Demos variant comes out of the Mesa
# port's own staging tree.  Collecting them is therefore a step of its own.
set -e
SRC="${1:-/ndrv/openstep-matrox-remade}"
DRVOUT="${2:-/tmp/pkgout}"
MESADIST="${3:-/usr/local/mesastage/OpenStepMesa342/dist}"
DEST="$SRC/build/release-pkgs"

DRV=OSMGADisplay
ACC=OSMGAMesaAccel
DEM=OpenStepMesa342DemosMGA

for p in "$DRVOUT/$DRV.pkg" "$DRVOUT/$ACC.pkg" "$MESADIST/$DEM.pkg"; do
    if [ ! -d "$p" ]; then
        echo "collect-release-pkgs: missing $p" >&2
        exit 1
    fi
done

rm -rf "$DEST"
/bin/mkdirs "$DEST"
# ONE TAR PER PACKAGE, written here rather than copied file by file.
#
# The destination is an exported directory, and it refuses to create a file
# that is mode 444 and then write into it -- which is exactly what `package`
# leaves its payload, its .info and its install scripts as.  Both a tar pipe
# and cp -R fail on those three files while copying the 644 ones happily,
# which looks like a partial success and is not one.
#
# A tar file is written with the default mode, so only one ordinary file
# crosses the export per package, and the modes inside it -- including the
# executable bit on pre_install, without which an install fails -- are
# carried in the archive rather than applied to the export.
#
# The directory and the basename are carried separately rather than split
# apart with dirname: this shell's PATH has no dirname.
copy_pkg() {
    ( cd "$1" && tar cf - "$2.pkg" ) > "$DEST/$2.pkg.tar"
    if [ ! -s "$DEST/$2.pkg.tar" ]; then
        echo "collect-release-pkgs: $2.pkg.tar is empty" >&2
        exit 1
    fi
}
copy_pkg "$DRVOUT" "$DRV"
copy_pkg "$DRVOUT" "$ACC"
copy_pkg "$MESADIST" "$DEM"

# The Demos variant is the ONLY Demos package that should travel with this
# release; the plain one belongs to the Mesa port's own release.
if tar tf "$DEST/$DEM.pkg.tar" | grep 'OpenStepMesa342Demos\.pkg' > /dev/null; then
    echo "collect-release-pkgs: the plain Demos package was collected" >&2
    exit 1
fi

for n in "$DRV" "$ACC" "$DEM"; do
    # The payload is what an empty archive would be missing, so look for it
    # by name inside each tar rather than trusting the tar's own size.
    if tar tf "$DEST/$n.pkg.tar" | grep "$n.tar.Z" > /dev/null; then
        :
    else
        echo "collect-release-pkgs: $n.tar.Z is not in $n.pkg.tar" >&2
        exit 1
    fi
    echo "  $n.pkg.tar  `wc -c < $DEST/$n.pkg.tar` bytes"
done
echo "collect-release-pkgs: PASS $DEST"
