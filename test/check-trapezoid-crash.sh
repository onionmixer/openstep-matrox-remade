#!/bin/sh
# The M18 crash, as a regression that costs one process.
#
# A legal GL triangle killed the shipped trapezoid tier with SIGFPE: a
# horizontal base of forty pixels lying exactly on a sample row with the apex
# five thousandths of a pixel above it.  The chain was HH = height >> (8-sub)
# going to nought, Q2 = 2*M*HH with it, and osmgaCeilDiv dividing by that.
#
# A crash cannot report itself, so this is a shell check: the exit status is
# the verdict.  136 is 128 + SIGFPE.  It also insists the picture is the forty
# pixels software draws -- refusing the triangle must not become dropping it.
#
#   sh test/check-trapezoid-crash.sh
set -u
cd "$(dirname "$0")/.."
: "${MOUNTPT:=/ndrv}"
D=$MOUNTPT/openstep-matrox-remade

# Written on the HOST, into the shared mount -- the target's /tmp is its
# own, and a heredoc here does not reach it.
mkdir -p scratch-sliver
cat > scratch-sliver/m18.txt <<'TRI'
N 1
F 0 0.005 40 0 0.5  140.5 120.5  180.5 120.5  160.5 120.505
TRI

OUT=$(../tools/nxrun.sh "\
    setenv OSMGA_MESA_ACCEL 1; setenv OSMGA_MESA_WARP 0; \
    cd $D && /tmp/wfam scratch-sliver/m18.txt > /tmp/m18-out.txt; \
    echo RC=\$status; grep -c '^P ' /tmp/m18-out.txt; \
    grep '^T ' /tmp/m18-out.txt" 2>&1)

echo "$OUT" | grep -vE '^setenv|^cd |^nextonion'

if echo "$OUT" | grep -q "RC=136"; then
    echo "FAIL: the trapezoid tier still dies with SIGFPE on it" >&2
    exit 1
fi
if ! echo "$OUT" | grep -q "RC=0"; then
    echo "FAIL: the reproducer did not run cleanly" >&2
    exit 1
fi
# Forty pixels, and by the software path: refusing it must not become
# dropping it, and it must not silently start being drawn on the engine
# either -- that would mean the guard stopped guarding.
# The count on its own line, allowing the carriage return telnet adds.
if ! echo "$OUT" | grep -qE "^40[[:space:]]*$"; then
    echo "FAIL: the reproducer drew a different number of pixels than 40" >&2
    exit 1
fi
if ! echo "$OUT" | grep -q "soft 1"; then
    echo "FAIL: it was not the software path that drew it" >&2
    exit 1
fi
echo "check-trapezoid-crash: PASS (no SIGFPE, refused to software, 40 pixels)"
exit 0
