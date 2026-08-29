#!/bin/bash
# Rebuild the driver's DisplayInspector.nib on the HOST.
#
#   bash .../tools/rebuild-inspector-nib.sh
#
# nibmaker's nib2xml/xml2nib are host binaries, so this does not run on the
# target.  It needs three inputs, and only one of them lives outside this
# workspace-of-projects:
#
#   stock nib   Configure.app's own DisplayInspector.nib, fetched from the
#               target once into build/stocknib (see below)
#   switch tmpl openstep-spacesaver2ps2/ref/nibtemplates/PS2MouseInspector.xml
#   radio tmpl  openstep-spacesaver2ps2/ref/nibtemplates/
#               radio-template-BusLogicIntrInspector.xml
#
# To refresh the stock nib, ON THE TARGET:
#
#   cd /NextAdmin/Configure.app/English.lproj \
#     && tar cf - DisplayInspector.nib > /ndrv/openstep-matrox-remade/build/stock-nib.tar
#
# then here: rm -rf build/stocknib && mkdir build/stocknib \
#              && tar xf build/stock-nib.tar -C build/stocknib
#
# A tar rather than a copy because the export refuses to create the stock
# nib's mode-444 files and then write into them.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ws=$(CDPATH= cd -- "$root/.." && pwd)
stock="$root/build/stocknib/DisplayInspector.nib"
out="$root/build/nibout"

[[ -r "$stock/data.nib" ]] || {
    echo "rebuild-inspector-nib: no stock nib at $stock" >&2
    echo "rebuild-inspector-nib: see the header of this script for how to fetch it" >&2
    exit 1
}
rm -rf "$out"; mkdir -p "$out"
python3 "$root/OSMGADisplay/nib-src/build-inspector-nib.py" \
    "$ws/openstep-nibmaker" \
    "$stock" \
    "$ws/openstep-spacesaver2ps2/ref/nibtemplates/PS2MouseInspector.xml" \
    "$ws/openstep-spacesaver2ps2/ref/nibtemplates/radio-template-BusLogicIntrInspector.xml" \
    "$out"

# The three files the bundle ships, and only data.nib is rebuilt here.
#
# data.dependency does come from the stock nib unchanged.  data.classes DOES
# NOT, and this comment used to say it did: the stock one declares
# IODisplayInspector and knows nothing of ours, while the shipped one
# declares OSMGADisplayInspector with every outlet and action the grafted
# controls connect to.  It is maintained BY HAND alongside the inspector's
# .h and .m, and re-copying it from the stock nib would silently break every
# connection -- an outlet that fails to connect is nil, and messages to nil
# say nothing.  Checked: `grep -c OSMGADisplayInspector` on the stock copy
# answers 0.
dst="$root/OSMGADisplay/English.lproj/DisplayInspector.nib"
cp "$out/data.nib" "$dst/data.nib"
echo "rebuild-inspector-nib: PASS $(stat -c%s "$dst/data.nib") bytes -> $dst"
