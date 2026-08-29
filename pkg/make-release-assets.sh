#!/bin/bash
# Turn the collected .pkg tars into the release assets, on the HOST.
#
#   bash .../pkg/make-release-assets.sh [version]
#
# The target side is pkg/collect-release-pkgs.sh, which writes one plain tar
# per package into build/release-pkgs.  This side only compresses and names,
# so it needs no OPENSTEP and touches nothing on the machine.
#
# The names follow the sibling releases already published from this
# repository: OpenStep-<product>-<version>-i486-<part>.pkg.tar.gz, one outer
# archive per Installer directory package.
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version="${1:-1.0}"
src="$root/build/release-pkgs"
dest="$root/release-assets"

# The Demos variant keeps the MESA port's version, not this project's: it is
# the Mesa port's package with a directory added, and calling it 1.0 would
# claim it is ours.  The +mga.1 suffix inside the .info becomes -mga.1 here
# because a '+' in a filename is an invitation to be mangled in transit.
declare -A NAMES=(
  [OSMGADisplay]="OpenStep-MGA-G450-${version}-i486-Display"
  [OSMGAMesaAccel]="OpenStep-MGA-G450-${version}-i486-MesaAccel"
  [OpenStepMesa342DemosMGA]="OpenStep-Mesa-3.4.2-openstep.1-mga.1-i486-Demos"
)

for n in "${!NAMES[@]}"; do
    [[ -f "$src/$n.pkg.tar" ]] || {
        echo "make-release-assets: missing $src/$n.pkg.tar" >&2
        echo "make-release-assets: run pkg/collect-release-pkgs.sh on the target first" >&2
        exit 1
    }
done

rm -rf "$dest"
mkdir -p "$dest"
for n in "${!NAMES[@]}"; do
    out="$dest/${NAMES[$n]}.pkg.tar.gz"
    gzip -9 -c "$src/$n.pkg.tar" > "$out"
    # The listing is taken ONCE into a variable rather than piped into two
    # greps.  Under `set -o pipefail`, `grep -q` exits at its first match and
    # the tar upstream dies of SIGPIPE, which the shell then reports as a
    # failure of the whole check -- the script exited 141 with every asset
    # built correctly.
    listing=$(tar tzvf "$out")
    # A release asset that unpacks to nothing is worse than a missing one.
    grep "$n.pkg/$n.tar.Z" <<<"$listing" > /dev/null
    # The executable bit on pre_install has to survive the round trip, or the
    # Installer runs nothing and reports success.
    grep "$n.pre_install" <<<"$listing" | grep 'r-x' > /dev/null
    echo "  ${NAMES[$n]}.pkg.tar.gz  $(stat -c%s "$out") bytes"
done

( cd "$dest" && sha256sum *.pkg.tar.gz > SHA256SUMS )
echo "make-release-assets: PASS $dest"
