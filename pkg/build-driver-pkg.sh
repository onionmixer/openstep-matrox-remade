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
NAME=OpenStepMGAReplacementDisplay
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
# THE 100-CHARACTER PATH LIMIT.  `package` writes the payload with
# installer_tar, which refuses any path over 100 characters -- it says "file
# name too long" and carries on, so the archive is short three files and
# nothing fails.  Our longest is 110:
#   ./private/Drivers/i386/OpenStepMGAReplacementDisplay.config/English.lproj/DisplayInspector.nib/data.dependency
# and nothing in it can be shortened: the install path is OPENSTEP's, the
# lproj is the localisation convention, the nib is loaded by name, and the
# bundle basename is the driver's own name.  (The sibling keyboard package
# clears the limit at 91 only because its bundle name is 13 characters
# shorter.)
#
# The answer is not to shorten anything.  Installer.app ships a SECOND
# archiver, installer_bigtar, which writes the long-name format the same
# Installer reads back; the SMInputKor project hit this with four nib files
# and fixed it the same way.  So the payload is REBUILT with bigtar after
# `package` has produced the .bom, the .info and the .sizes.
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

# Rebuild the payload with the long-name archiver, replacing the one
# installer_tar wrote with three files missing.  The .bom, .info and .sizes
# `package` produced are kept: only the archive was wrong.
BIGTAR=/NextAdmin/Installer.app/installer_bigtar
if [ ! -x "$BIGTAR" ]; then
    echo "build-driver-pkg: $BIGTAR not found; the nib would be dropped" >&2
    exit 1
fi
rm -f "$OUT/$NAME.pkg/$NAME.tar" "$OUT/$NAME.pkg/$NAME.tar.Z"
( cd "$STAGE" && "$BIGTAR" cf "$OUT/$NAME.pkg/$NAME.tar" . )
nibs=`"$BIGTAR" tf "$OUT/$NAME.pkg/$NAME.tar" | grep 'DisplayInspector.nib/' | wc -l`
if [ "$nibs" -lt 3 ]; then
    echo "build-driver-pkg: bigtar carried $nibs nib files, wanted 3" >&2
    exit 1
fi
/usr/ucb/compress "$OUT/$NAME.pkg/$NAME.tar"
# `package` leaves its own outputs read-only; the payload we rebuilt came out
# 644, so put it back the way the rest of the package directory looks.
chmod 444 "$OUT/$NAME.pkg/$NAME.tar.Z"

cp "$SRC/pkg/$NAME.pre_install" "$OUT/$NAME.pkg/$NAME.pre_install"
chmod 555 "$OUT/$NAME.pkg/$NAME.pre_install"
echo "build-driver-pkg: PASS $OUT/$NAME.pkg (payload by installer_bigtar)"
