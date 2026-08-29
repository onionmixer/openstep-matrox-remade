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
