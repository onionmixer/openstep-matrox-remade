#!/bin/sh
# Set the installed instance's display mode and grey preset, for the R4 tests.
#
#   sh .../tools/set-display-mode.sh <width> <height> <ColorSpace> [greys]
#
#   sh set-display-mode.sh 640 480 BW:8 4      <- 640x480 greyscale, 4 greys
#   sh set-display-mode.sh 640 480 BW:8 16
#   sh set-display-mode.sh 1024 768 RGB:888/32 <- greys ignored for RGB
#   sh set-display-mode.sh 640 480 BW:4        <- the legacy spelling, on purpose
#
# Runs ON the target.  Edits ONLY the two keys and leaves every other line
# alone, so the development switches survive.  Takes effect at the next boot:
# the driver reads the mode once, in init.
set -e
W="$1"; H="$2"; CS="$3"; G="${4:-256}"
T=/private/Drivers/i386/OSMGADisplay.config/Instance0.table
if [ -z "$W" ] || [ -z "$H" ] || [ -z "$CS" ]; then
    echo "usage: set-display-mode.sh <width> <height> <ColorSpace> [greys]" >&2
    exit 2
fi
if [ ! -r "$T" ]; then
    echo "set-display-mode: no $T -- is the driver installed?" >&2
    exit 1
fi

# The table is installed read-only, so it is rewritten rather than edited in
# place, and the old one is kept until the new one has been proven non-empty.
# The backup goes to /tmp, NOT beside the table.  A driver bundle should
# hold only what the driver ships; driverLoader reads the whole directory,
# and a stray file there is at best confusing and at worst refused (it
# aborts on any group- or other-writable file in a bundle).
B=/tmp/osmga-Instance0.table.prev
N=/tmp/osmga-Instance0.table.new
cp "$T" "$B"
sed -e "/^\"Display Mode\"/d" -e "/^\"Gray Levels\"/d" "$B" > "$N"
echo "\"Display Mode\" = \"Height: $H Width: $W Refresh: 60Hz ColorSpace: $CS\";" >> "$N"
echo "\"Gray Levels\" = \"$G\";" >> "$N"

# A truncated instance table is how a machine boots into 800x600 VGA, so the
# new file is checked before it replaces the old one.
n=`grep -c . "$N"`
o=`grep -c . "$B"`
if [ "$n" -lt 5 ]; then
    echo "set-display-mode: the new table has only $n lines, refusing" >&2
    rm -f "$N"
    exit 1
fi
chmod 644 "$T"
cp "$N" "$T"
chmod 444 "$T"
rm -f "$N"
echo "set-display-mode: was $o lines, now $n"
grep '^"Display Mode"' "$T"
grep '^"Gray Levels"' "$T"
echo "set-display-mode: takes effect at the next boot"
