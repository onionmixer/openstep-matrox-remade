#!/bin/sh
# Set one key in the INSTALLED driver instance table, safely.
#
#   sh .../tools/set-config-key.sh "VRAM Mmap" Yes
#
# Runs ON the target.  Rewrites only the named key and leaves every other
# line alone -- including "Location" and "Version", which driverLoader and
# Configure wrote and which nothing else should touch.
#
# Why a script and not sed by hand: this file decides what the machine boots
# into.  A truncated one is how a machine comes up in 800x600 VGA, so the
# replacement is built beside the original, counted, and only then swapped
# in.  The table is installed read-only, hence the chmod dance.
#
# Takes effect at the next boot: the driver reads the table once, in init.
#
# -a adds a key that is not there yet.  It is a separate flag rather than the
# default because inventing a key silently is how a typo becomes a setting
# nobody can find: with -a the caller is saying "yes, this one is new".
# A key that lives only in Default.table is exactly this case -- the instance
# table is the machine's answer, and it does not have to mention every
# default to be complete.
#
set -e
ADD=0
if [ "$1" = "-a" ]; then
    ADD=1
    shift
fi
K="$1"; V="$2"
T=/private/Drivers/i386/OpenStepMGAReplacementDisplay.config/Instance0.table
if [ -z "$K" ] || [ -z "$V" ]; then
    echo "usage: set-config-key.sh [-a] <key> <value>" >&2
    exit 2
fi
if [ ! -r "$T" ]; then
    echo "set-config-key: no $T" >&2
    exit 1
fi
if grep "^\"$K\"" "$T" > /dev/null; then
    PRESENT=1
else
    PRESENT=0
fi
if [ "$PRESENT" -eq 0 ] && [ "$ADD" -eq 0 ]; then
    echo "set-config-key: no key \"$K\" in the table; refusing to invent one" >&2
    echo "                (pass -a if it really is meant to be new)" >&2
    exit 1
fi

# The backup goes to /tmp, NOT beside the table.  A driver bundle should
# hold only what the driver ships; driverLoader reads the whole directory,
# and a stray file there is at best confusing and at worst refused (it
# aborts on any group- or other-writable file in a bundle).
B=/tmp/osmga-Instance0.table.prev
N=/tmp/osmga-Instance0.table.new
cp "$T" "$B"
sed "/^\"$K\"/d" "$B" > "$N"
echo "\"$K\" = \"$V\";" >> "$N"

n=`grep -c . "$N"`
o=`grep -c . "$B"`
if [ "$PRESENT" -eq 1 ]; then
    want="$o"
else
    want=`expr "$o" + 1`
fi
if [ "$n" -ne "$want" ]; then
    echo "set-config-key: line count $o -> $n, wanted $want, refusing" >&2
    rm -f "$N"
    exit 1
fi
chmod 644 "$T"
cp "$N" "$T"
chmod 444 "$T"
rm -f "$N"
grep "^\"$K\"" "$T"
