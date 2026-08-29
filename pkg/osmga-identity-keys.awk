# Put the package's identity keys into a machine's instance table.
#
#   awk -f osmga-identity-keys.awk Default.table Instance0.table > candidate
#
# THE RULE THIS SERVES.  The machine owns its configuration and the package
# owns the driver's identity.  C4 preserves the whole instance table byte for
# byte, which kept an operator's Location and switches -- and also kept a
# development "Version" = "0.5" through the 1.0 and 1.1 releases, so
# Configure showed a version this driver never shipped as.  The same rule
# wrote the OLD Driver Name back over a renamed bundle, which would have
# come up with no display driver.
#
# FIVE KEYS, NOT NINE.  Auto Detect IDs and Bus Type look like the package's,
# but doc/driverkit.md is explicit that InstanceN.table records the hardware
# as DETECTED AND CONFIGURED -- its example of a working DEC 21041 carries
# Auto Detect IDs inside the instance table.  Those are the machine's record
# of what matched, not our catalogue.  Title is left alone too: what
# Configure shows is Long Name from Localizable.strings, so overwriting
# Title buys nothing and a change that buys nothing is only a risk.
#
# The values come from Default.table rather than the payload's instance
# table, because by the time this runs the restore has already written the
# machine's table over the payload's -- the shipped instance table is gone.
# Default.table is the package's own and survives.
#
# Everything not selected is emitted byte for byte, in place.  No
# delete-and-append: that loses a key's position and any comment around it.
#
# WRITTEN FOR OLD AWK.  OPENSTEP has /bin/awk and nothing else -- no nawk,
# no gawk -- and it is the v7 language.  Three things it does not have, all
# found by running it:
#
#   user-defined functions   "syntax error near line 34"
#   /dev/stderr              same
#   FNR                      SILENT, and much worse: `NR == FNR` is simply
#                            never true, so the first file falls through to
#                            the second rule and Default.table's whole
#                            comment block was copied into the candidate
#
# The last one is why the files are told apart by FILENAME instead.  A
# missing feature that makes a script fail is cheap; one that makes it
# produce plausible garbage is not.
#
BEGIN {
    want["Driver Name"] = 1; want["Server Name"] = 1; want["Class Names"] = 1
    want["Family"] = 1;      want["Version"] = 1
    bad = 0
}

# pass 1 -- the package's Default.table supplies the values
FILENAME ~ /Default\.table$/ {
    key = ""
    if (substr($0, 1, 1) == "\"") {
        q = index(substr($0, 2), "\"")
        if (q > 1) key = substr($0, 2, q - 1)
    }
    if (key != "" && want[key] == 1) {
        if (val[key] != "") bad = 1
        val[key] = $0
    }
    next
}

# pass 2 -- the machine's table, rewritten in place
{
    key = ""
    if (substr($0, 1, 1) == "\"") {
        q = index(substr($0, 2), "\"")
        if (q > 1) key = substr($0, 2, q - 1)
    }
    if (key != "" && want[key] == 1) {
        if (done[key] == 1) bad = 1
        done[key] = 1
        if (val[key] != "") { print val[key]; next }
    }
    print
}

# A key the machine lacks is appended rather than dropped.  All five are
# required for the driver to load, so their absence is not a setting.
END {
    for (key in val)
        if (done[key] != 1) print val[key]
    if (bad == 1) exit 1
}
