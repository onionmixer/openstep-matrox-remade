#!/bin/sh
# Do the Installer hooks actually preserve the machine's configuration?
#
#   sh .../test/check-install-hooks.sh
#
# Runs ON the target, against a SANDBOX rather than the live bundle: the
# hooks take their destination as argv[2], so pointing them at a throwaway
# tree exercises the real scripts without putting the machine's display
# configuration at risk.
#
# The sequence is the one Installer performs -- pre_install, extract,
# post_install -- with the extraction faked by overwriting the table the way
# the payload would.
#
# It refuses to pass vacuously: the arms that must LOSE the value are run
# too, so a hook that did nothing at all would fail them.
SRC=${1:-/ndrv/openstep-matrox-remade}
R=/tmp/_hooktest
CFG=$R/private/Drivers/i386/OSMGADisplay.config
bad=0
note() { echo "  ok   $1"; }
fail() { echo "  FAIL $1"; bad=`expr $bad + 1`; }

setup() {
    rm -rf $R
    /bin/mkdirs $CFG
    echo '"Location" = "Dev:0 Func:0 Bus:4";' >  $CFG/Instance0.table
    echo '"WARP 3D" = "Yes";'                 >> $CFG/Instance0.table
    chmod 444 $CFG/Instance0.table
    # the two files post_install reads out of the installed layout: the
    # package's own table, and the transform that puts its identity keys back
    cp $SRC/OSMGADisplay/Default.table $CFG/Default.table
    cp $SRC/pkg/osmga-identity-keys.awk $R/private/Drivers/i386/
}
extract() {   # what the payload does: its own table lands on top
    chmod 644 $CFG/Instance0.table 2> /dev/null
    echo '"Location" = "";'      >  $CFG/Instance0.table
    echo '"WARP 3D" = "No";'     >> $CFG/Instance0.table
}

echo "an upgrade keeps the machine's table"
setup
csh -f $SRC/pkg/OSMGADisplay.pre_install one $R > /dev/null
extract
csh -f $SRC/pkg/OSMGADisplay.post_install one $R > /dev/null
if grep 'Dev:0 Func:0 Bus:4' $CFG/Instance0.table > /dev/null; then
    note "Location survived the install"
else
    fail "Location was lost -- this is the defect the hooks exist for"
fi
if grep '"WARP 3D" = "Yes"' $CFG/Instance0.table > /dev/null; then
    note "the operator's switch survived"
else
    fail "the switch was reset to the shipped value"
fi
if [ -d $R/private/Drivers/i386/OSMGADisplay.instances ]; then
    fail "the stash was left behind after a successful restore"
else
    note "the stash is gone after a clean restore"
fi

echo "the package's identity keys are updated, the machine's settings are not"
setup
# a machine that has an OLD driver name and an OLD version, as a machine
# upgraded across the 2026-08-29 rename would
chmod 644 $CFG/Instance0.table
echo '"Driver Name" = "OpenStepMGAReplacementDisplay";' >> $CFG/Instance0.table
echo '"Version" = "0.5";'                               >> $CFG/Instance0.table
chmod 444 $CFG/Instance0.table
csh -f $SRC/pkg/OSMGADisplay.pre_install one $R > /dev/null
extract
csh -f $SRC/pkg/OSMGADisplay.post_install one $R > /dev/null
if grep '"Driver Name" = "OSMGADisplay";' $CFG/Instance0.table > /dev/null; then
    note "the driver name follows the package"
else
    fail "the driver name is still the old one -- Active Drivers would call"
    fail "for a bundle whose table claims to be something else"
fi
if grep '"Version" = "1.1";' $CFG/Instance0.table > /dev/null; then
    note "the version follows the package"
else
    fail "the version is still the machine's stale one"
fi
if grep 'Dev:0 Func:0 Bus:4' $CFG/Instance0.table > /dev/null; then
    note "and Location -- the machine's -- is untouched"
else
    fail "a machine key moved; only the five identity keys may"
fi
if grep '"WARP 3D" = "Yes"' $CFG/Instance0.table > /dev/null; then
    note "and so is the operator's switch"
else
    fail "the operator's switch moved"
fi

echo "without the hooks the value IS lost -- so the test is not vacuous"
setup
extract
if grep 'Dev:0 Func:0 Bus:4' $CFG/Instance0.table > /dev/null; then
    fail "the fake extraction did not overwrite anything; this test proves nothing"
else
    note "extraction alone loses Location, as it did on the machine"
fi

echo "a first install keeps the shipped table"
rm -rf $R
/bin/mkdirs $CFG
csh -f $SRC/pkg/OSMGADisplay.pre_install one $R > /dev/null
extract
csh -f $SRC/pkg/OSMGADisplay.post_install one $R > /dev/null
if grep '"Location" = "";' $CFG/Instance0.table > /dev/null; then
    note "nothing to preserve, so the package's own table stays"
else
    fail "a first install did not leave the shipped table"
fi

echo "an interrupted install leaves the configuration recoverable"
setup
csh -f $SRC/pkg/OSMGADisplay.pre_install one $R > /dev/null
extract
# post_install never runs -- the install was cancelled
if [ -f $R/private/Drivers/i386/OSMGADisplay.instances/Instance0.table ]; then
    note "the stash still holds the machine's table"
else
    fail "an interrupted install left no copy of the configuration"
fi
# and the next install must not throw that record away
csh -f $SRC/pkg/OSMGADisplay.pre_install one $R > /dev/null
if grep 'Dev:0 Func:0 Bus:4' \
     $R/private/Drivers/i386/OSMGADisplay.instances/Instance0.table > /dev/null; then
    note "a later install keeps it rather than overwriting it"
else
    fail "the second pre_install destroyed the only copy"
fi

rm -rf $R
echo
if [ $bad -eq 0 ]; then echo "CHECK_INSTALL_HOOKS=PASS"; else
    echo "CHECK_INSTALL_HOOKS=FAIL ($bad)"; exit 1; fi
