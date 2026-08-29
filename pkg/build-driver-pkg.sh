#!/bin/sh
# Build the OPENSTEP Installer package for the display driver.
#
#   sh .../pkg/build-driver-pkg.sh [source-root] [outdir] [build-root]
#
# Runs ON the target: the package tool is OPENSTEP's.
#
# The BUNDLE comes from the build tree, not the source tree.  The source
# tree holds the tables and the nib sources; the compiled relocatable, the
# inspector and the generated Localizable.strings only exist where the build
# put them (tools/build-matrox-driver.sh writes /usr/local/nxbuild).  Taking
# the bundle from the source tree would package whatever happened to be
# lying there, which is how a stale artefact was once installed.
set -e
SRC="${1:-/ndrv/openstep-matrox-remade}"
# /tmp/pkgout, which is what release-packaging/PAYLOAD_MANIFEST.md passes to
# every script in this directory and what collect-release-pkgs.sh,
# diff-against-installed.sh, check-bom-overlap.sh and verify-driver-pkg.sh
# already default to.  The builders used to default to /tmp on their own, so
# running a builder and then its verifier with no arguments failed with "no
# package" on a package that had just been written somewhere else.  The
# directory is created below rather than assumed.
OUT="${2:-/tmp/pkgout}"
BUILDROOT="${3:-/usr/local/nxbuild}"
NAME=OSMGADisplay
PKGTOOL=/NextAdmin/Installer.app/package
BUNDLE="$BUILDROOT/$NAME/$NAME.config"

if [ ! -x "$PKGTOOL" ]; then
    echo "build-driver-pkg: $PKGTOOL not found (run on OPENSTEP)" >&2
    exit 1
fi
if [ "`/usr/bin/arch`" != i386 ]; then
    echo "build-driver-pkg: the payload is i386; build it on i386" >&2
    exit 1
fi
for f in "$BUNDLE/${NAME}_reloc" "$BUNDLE/$NAME" \
         "$BUNDLE/Default.table" "$BUNDLE/Display.modes" \
         "$BUNDLE/English.lproj/Localizable.strings" \
         "$SRC/pkg/Instance0.release.table" "$SRC/pkg/$NAME.info" \
         "$SRC/pkg/$NAME.pre_install" "$SRC/LICENSE" "$SRC/NOTICE"; do
    if [ ! -r "$f" ]; then
        echo "build-driver-pkg: missing input: $f" >&2
        exit 1
    fi
done
# NOT `if ! cmd`: this sh has no command negation, and a guard written that
# way answers false every time and never fires.
if file "$BUNDLE/${NAME}_reloc" | grep 'Mach-O preloaded' > /dev/null; then
    :
else
    echo "build-driver-pkg: ${NAME}_reloc is not a preloaded relocatable" >&2
    exit 1
fi

# `package` builds the BOM from the stage's PARENT, so the stage needs a
# private empty parent or every file beside it lands in the BOM.
#
# THE 100-CHARACTER PATH LIMIT, and what it really cost.
#
# `package` writes the payload with installer_tar, which refuses any path
# over 100 characters -- it says "file name too long" and carries on, so the
# archive comes out short of files and nothing fails.  With the bundle named
# OpenStepMGAReplacementDisplay the longest was 110:
#   ./private/Drivers/i386/<29-char name>.config/English.lproj/DisplayInspector.nib/data.dependency
#
# This script used to answer that by REBUILDING the payload with the other
# archiver Installer.app ships, installer_bigtar, on the reasoning that it
# "writes the long-name format the same Installer reads back".  That reasoning
# was never tested and it is wrong.  Measured on the machine: bigtar reads its
# own archive back, the system tar reports a checksum error and lists nothing,
# and INSTALLER_TAR HANGS ON IT -- 180 seconds, twice.  The operator's
# Installer hung on exactly that, and the verifier had been reading the
# payload back with bigtar too, so the packaging only ever agreed with itself.
#
# So the payload is `package`'s own again, and the bundle was shortened to
# OSMGADisplay instead: 93 characters at the longest, seven to spare.  The
# check below is a RULE rather than a list of the three paths that happened to
# be long, because the next long one will not be one of those three.
STAGEPARENT=/tmp/_mgadrvpkg
STAGE="$STAGEPARENT/p"
rm -rf "$STAGEPARENT" "$OUT/$NAME.pkg" "$OUT/$NAME.pkg.tar"
/bin/mkdirs "$STAGE/private/Drivers/i386/$NAME.config/English.lproj" \
            "$STAGE/usr/local/Documentation/OpenStep-MGA-G450"

D="$STAGE/private/Drivers/i386/$NAME.config"
cp "$BUNDLE/${NAME}_reloc" "$BUNDLE/$NAME" "$BUNDLE/Default.table" \
   "$BUNDLE/Display.modes" "$D/"
# The RELEASE instance table, not the development one: the development
# instance turns on Raster Test, VRAM Mmap and Mesa Acceleration, and none of
# those should be inherited by somebody else's machine.
cp "$SRC/pkg/Instance0.release.table" "$D/Instance0.table"
cp "$BUNDLE/English.lproj/Localizable.strings" "$D/English.lproj/"
( cd "$BUNDLE/English.lproj" && tar cf - DisplayInspector.nib ) \
    | ( cd "$D/English.lproj" && tar xf - )
rm -f "$D/.lastBuildTime"
cp "$SRC/LICENSE" "$SRC/NOTICE" \
   "$STAGE/usr/local/Documentation/OpenStep-MGA-G450/"
if [ -r "$SRC/release-packaging/INSTALL.md" ]; then
    cp "$SRC/release-packaging/INSTALL.md" \
       "$STAGE/usr/local/Documentation/OpenStep-MGA-G450/"
fi

# Prove the staged instance table is the release one before it is sealed in.
if grep '"Raster Test" = "Yes"' "$D/Instance0.table" > /dev/null; then
    echo "build-driver-pkg: the DEVELOPMENT instance table got staged" >&2
    exit 1
fi
for n in data.classes data.dependency data.nib; do
    if [ ! -r "$D/English.lproj/DisplayInspector.nib/$n" ]; then
        echo "build-driver-pkg: nib file missing from stage: $n" >&2
        exit 1
    fi
done

test -d "$OUT" || /bin/mkdirs "$OUT"
"$PKGTOOL" "$STAGE" "$SRC/pkg/$NAME.info" -d "$OUT" < /dev/null

# NOTHING may exceed installer_tar's limit, or `package` drops it in silence.
# Checked against the stage rather than against a remembered list.
long=`find "$STAGE" -print | sed "s|^$STAGE|.|" | awk 'length($0) >= 100'`
if [ -n "$long" ]; then
    echo "build-driver-pkg: these paths are 100 characters or more and" >&2
    echo "build-driver-pkg: installer_tar would drop them silently:" >&2
    echo "$long" >&2
    exit 1
fi

cp "$SRC/pkg/$NAME.pre_install" "$OUT/$NAME.pkg/$NAME.pre_install"
chmod 555 "$OUT/$NAME.pkg/$NAME.pre_install"
echo "build-driver-pkg: PASS $OUT/$NAME.pkg (payload by package/installer_tar)"
