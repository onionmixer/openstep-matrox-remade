#!/usr/bin/env python3
"""build-inspector-nib.py - build the driver's DisplayInspector.nib.

The base is Configure.app's own DisplayInspector.nib, taken byte for byte,
with only the File's Owner class renamed to OSMGADisplayInspector.  Onto
that we graft two switches for the driver's development flags.

    python3 nib-src/build-inspector-nib.py <nibmaker-dir> <stock-nib-dir> \\
            <switch-template.xml> <radio-template.xml> <out-dir>

The two templates are decoded stock nibs kept in a sibling project:

    switch-template.xml = openstep-spacesaver2ps2/ref/nibtemplates/
                          PS2MouseInspector.xml        (Button 12, label 23)
    radio-template.xml  = openstep-spacesaver2ps2/ref/nibtemplates/
                          radio-template-BusLogicIntrInspector.xml  (Matrix 12)

and the stock nib is Configure.app's own:

    /NextAdmin/Configure.app/English.lproj/DisplayInspector.nib

The stock inspection view (NeXT coordinates, y up):

    49 View  [1,1,399,93]                 content of the inspection window
      48 Box [9,14,382,66]                outlet displayMode
        52 View [0,0,382,66]
          54 CustomView [10,9,362,19]     outlet displayAccessoryHolder
          55 Box [8,9,364,53]
            57 View [2,2,360,35]
              59 TextField                outlet modeText
              61 Button                   outlet selectButton ("Select...")

The switches go inside box 48, because that box is the only thing
Configure actually displays.  Measured on hardware: a first attempt put
them in view 49 as siblings of the box, and the mode UI appeared while
the switches did not -- so IODisplayInspector installs the displayMode
box into the base inspector's accessory area and drops the rest of this
nib's content view.  Box 48 and its content view 52 therefore grow, and
the existing children move up to make room at the bottom.

displayAccessoryHolder is not used: it is a CustomView of class
SwitchView that no data.classes here declares, it is 362x19, and it sits
underneath box 55 where nothing would show it.  What IODisplayInspector's
setAccessoryView: does with it is not documented anywhere we can read.
"""
import os
import subprocess
import sys
import xml.etree.ElementTree as ET

NIBMAKER, STOCK, SWITCH_TEMPLATE, RADIO_TEMPLATE, OUT = sys.argv[1:6]
sys.path.insert(0, os.path.join(NIBMAKER, 'tools'))
from nibgraft import Nib  # noqa: E402

OWNER = '1'          # File's Owner (OSMGADisplayInspector)
WINDOW = '65'        # WindowTemplate of the inspection view
CONTENT = '49'       # its content view
MODE_BOX = '48'      # outlet displayMode -- the only part Configure shows
MODE_VIEW = '52'     # box 48's content view; our switches go here
MODE_INNER = ('54', '55')   # its existing children, moved up to make room

SWITCH_SRC = '12'    # a real NX_SWITCH Button in the template nib
SWITCH_SUPERVIEW = '4'   # its superview there; rewritten by add_subview
SWITCH_FONT = '10'       # Helvetica 12 there
LABEL_SRC = '23'         # a plain borderless TextField label ("Slow")
LABEL_SUPERVIEW = '2'    # its superview there

# Extra height inside box 48, and the layout that fills it.  y counts UP, so
# y=6 is the bottom line and the status rows are at the top.
#
# The headroom is NOT `GROW - top`.  The stock nib puts the box's original
# children at y=9 and this script lifts them to y + GROW, so the gap above our
# rows is (9 + GROW) - top.  Today: 9 + 95 - 81 = 23.  With two status rows
# topping out at 140, GROW = 154 gives 9 + 154 - 140 = 23 again -- the same
# spacing the panel already has, rather than an invented figure.  GROW = 138
# would leave 7, which is not the same panel.
#
# C2 added a third switch row.  The rule above is what decided the numbers
# rather than taste: with the WARP row at y=44 and everything above it lifted
# by one row's 20 px, the topmost edge goes 140 -> 160, so GROW has to go
# 154 -> 174 for (9 + GROW) - top to stay 23.  Computed, not chosen.
#
GROW = 174
SW_W, SW_H = 340, 15
#
# The two switch constants were NAMED the wrong way round: Y_STORM held the
# mmap row's y and Y_MMAP the Storm row's.  The panel was right and the names
# were wrong, which is the sort of thing that puts the next row in the wrong
# place.  Renamed to what they are.
#
Y_CAPTION, Y_MMAP, Y_WARP, Y_STORM, Y_GRAY = 6, 24, 44, 64, 86
Y_VRAM, Y_STATUS_MODE, Y_STATUS_BRIEF = 108, 130, 146
VRAM_LABEL_X, VRAM_LABEL_W = 12, 96
VRAM_MATRIX_X = 112
VRAM_TITLES = ('8', '16', '32')
STATUS_X, STATUS_W, STATUS_H = 12, 340, 14
GRAY_LABEL_X, GRAY_LABEL_W = 12, 96
GRAY_MATRIX_X = 112
GRAY_CELL_W, GRAY_CELL_H, GRAY_GAP = 54, 15, 4
GRAY_TITLES = ('256', '16', '4', '2')

# ---------------------------------------------------------------- base
stock_xml = os.path.join(OUT, 'stock.xml')
subprocess.check_call([os.path.join(NIBMAKER, 'nib2xml'),
                       os.path.join(STOCK, 'data.nib')],
                      stdout=open(stock_xml, 'w'))
n = Nib(stock_xml)

owners = [o for o in n.find_by_class('CustomObject')
          if n.cstring(o, '*@').get('v') == 'IODisplayInspector']
assert len(owners) == 1, 'expected exactly one IODisplayInspector owner'
sh = n.cstring(owners[0], '*@')
sh.set('v', 'OSMGADisplayInspector')
sh.set('wrap', '1')


def cell_of(control):
    """The cell object defined inside a Control's 'i@s' group."""
    return [c for c in n.group(control, 'i@s')][1]


def set_ints(group, values):
    ints = [i for i in group if i.tag == 'i']
    for i, v in zip(ints, values):
        i.set('v', str(v))
        i.set('w', '1' if abs(int(v)) > 127 else '0')


# ------------------------------------------------------------ make room
win = n.obj(WINDOW)
wx, wy, ww, wh = n.frame(win)
set_ints(n.group(win, 'ffff', 0), (wx, wy, ww, wh + GROW))

cx, cy, cw, ch = n.frame(n.obj(CONTENT))
n.set_frame(n.obj(CONTENT), cx, cy, cw, ch + GROW)

bx, by, bw, bh = n.frame(n.obj(MODE_BOX))
n.set_frame(n.obj(MODE_BOX), bx, by, bw, bh + GROW)

mx, my, mw, mh = n.frame(n.obj(MODE_VIEW))
n.set_frame(n.obj(MODE_VIEW), mx, my, mw, mh + GROW)

# y counts up, so the new space appears at the bottom: lift what was
# there and leave the gap below it for the switches.
for oid in MODE_INNER:
    ix, iy, iw, ih = n.frame(n.obj(oid))
    n.set_frame(n.obj(oid), ix, iy + GROW, iw, ih)

# --------------------------------------------------------- the switches
tmpl = Nib(SWITCH_TEMPLATE)

# The switch subtree is {Button, ButtonCell, and its two NXImages}; it
# reaches outside itself only for its superview and its font.  The
# superview slot is rewritten by add_subview, and Helvetica 12 already
# exists here, so both map onto objects of ours.
OURS_HELVETICA_12 = next(oid for oid, o in n.objs.items()
                         if o.get('cls') == 'Font'
                         and [c.get('v') for c in n.group(o, '%fss')][:2]
                             == ['Helvetica', '12'])
GRAFT_MAP = {SWITCH_SUPERVIEW: MODE_VIEW, SWITCH_FONT: OURS_HELVETICA_12}
LABEL_MAP = {LABEL_SUPERVIEW: MODE_VIEW, SWITCH_FONT: OURS_HELVETICA_12}


def add_switch(label, y, outlet, action):
    sw = n.graft_from(tmpl, SWITCH_SRC, obj_map=dict(GRAFT_MAP))
    n.reindex()
    n.set_cstring(cell_of(sw), label)
    n.add_subview(MODE_VIEW, sw, 12, y, SW_W, SW_H)
    n.reindex()
    n.add_connector('IBOutletConnector', OWNER, sw.get('oid'), outlet)
    n.add_connector('IBControlConnector', sw.get('oid'), OWNER, action)
    n.reindex()
    return sw


# Same rows on screen as before -- only the constant names moved.
add_switch('Run Storm 2D engine self-test at boot', Y_STORM,
           'stormSwitch', 'toggleStorm:')
add_switch('Publish offscreen VRAM as a character device', Y_MMAP,
           'mmapSwitch', 'toggleMmap:')
#
# The label is short because it has to be: the control is 340 px and its
# image and gap take about 20, so the text has about 320 of Helvetica 12.
# The sentence this row deserves -- "faster, but it differs on sliver
# triangles" -- estimates at 420 and would have been clipped exactly where
# the warning is.  The caveat stays, in the words that fit.
#
add_switch('Use WARP 3D (may differ on sliver triangles)', Y_WARP,
           'warpSwitch', 'toggleWarp:')

# ------------------------------------------------------- radio matrices
# The template's matrix has two cells laid out vertically; extra cells are
# cloned from the second and the whole thing re-laid as one row, which is what
# the SpaceSaver2Mouse inspector does with the same template (its build
# script, "middle button").
#
# Written once and called twice.  The grey preset and the VRAM declaration are
# the same object with different titles, and a second hand-written copy is how
# two matrices come to differ in ways nobody meant.
def add_matrix(titles, x, y, cell_w, cell_h, gap, outlet, action):
    radio = Nib(RADIO_TEMPLATE)
    rmatrix = [o for o in radio.objs.values() if o.get('cls') == 'Matrix'][0]
    rsuper = [c for c in radio.groups(rmatrix)[0]][0].get('oid')
    mat = n.graft_from(radio, rmatrix.get('oid'),
                       obj_map={'10': OURS_HELVETICA_12, rsuper: MODE_VIEW})
    n.reindex()
    n.add_subview(MODE_VIEW, mat, x, y,
                  len(titles) * cell_w + (len(titles) - 1) * gap, cell_h)
    n.reindex()

    cells_group = n.group(mat, '@:@iiii')
    cells_list = [c for c in cells_group][0]
    assert cells_list.get('cls') == 'List', 'matrix cell list not where expected'
    first_cells = list(n.list_items(cells_list))
    assert len(first_cells) == 2, 'the template matrix should have two cells'
    cells = first_cells[:]
    for _ in range(len(titles) - len(first_cells)):
        c = n.clone(first_cells[1].get('oid'))
        n.list_append(cells_list, c)
        n.reindex()
        cells.append(c)
    for tag, (c, title) in enumerate(zip(cells, titles)):
        n.set_cstring(c, title)
        set_ints(n.group(c, 'i:'), (tag,))

    # selected cell -> the first, and one row of len(titles) columns
    sel = [c for c in cells_group][2]
    sel.set('oid', cells[0].get('oid'))
    sel.set('ref', '0')
    set_ints(cells_group, (0, 0, 1, len(titles)))
    set_ints(n.group(mat, 'ff', 0), (cell_w, cell_h))
    set_ints(n.group(mat, 'ff', 1), (gap, 0))
    n.add_connector('IBOutletConnector', OWNER, mat.get('oid'), outlet)
    n.add_connector('IBControlConnector', mat.get('oid'), OWNER, action)
    n.reindex()
    return mat


def add_label(text, x, y, w, outlet=None):
    lb = n.graft_from(tmpl, LABEL_SRC, obj_map=dict(LABEL_MAP))
    n.reindex()
    n.set_cstring(cell_of(lb), text)
    n.add_subview(MODE_VIEW, lb, x, y, w, 14)
    n.reindex()
    if outlet:
        n.add_connector('IBOutletConnector', OWNER, lb.get('oid'), outlet)
        n.reindex()
    return lb


add_label('Gray levels:', GRAY_LABEL_X, Y_GRAY, GRAY_LABEL_W)
add_matrix(GRAY_TITLES, GRAY_MATRIX_X, Y_GRAY,
           GRAY_CELL_W, GRAY_CELL_H, GRAY_GAP, 'grayMatrix', 'grayChanged:')

# The declaration.  Three cells: 8 is here for the G400 boards that shipped
# with it -- the driver refuses to program a mode on anything but a G450, so
# this is the memory side of that work arriving early, not G400 support.
# 4 MB is deliberately absent: 4 MiB less the 4 MiB top-of-VRAM margin is a
# zero ceiling, which would mean "never any acceleration".
add_label('VRAM size (MB):', VRAM_LABEL_X, Y_VRAM, VRAM_LABEL_W)
add_matrix(VRAM_TITLES, VRAM_MATRIX_X, Y_VRAM,
           GRAY_CELL_W, GRAY_CELL_H, GRAY_GAP, 'vramMatrix', 'vramChanged:')

# What that mode WOULD get, on two rows.
#
# The driver's one-line answer is 430 px of Helvetica 12 and this field is
# 340, so it cannot be shown.  Dropping the mode to make it fit would be
# worse: the panel reads the mode in -setTable: and nothing tells it when the
# stock resolution picker changes, so the line can be stale -- and a line that
# names its mode is visibly stale where the same line without it is silently
# wrong.  Both strings come from OSMGAAccelVerdict, the function that also
# writes the driver's log line.
add_label('', STATUS_X, Y_STATUS_MODE, STATUS_W, 'statusMode')
add_label('', STATUS_X, Y_STATUS_BRIEF, STATUS_W, 'statusBrief')

# All three are read once, when the driver initialises, so say so: a control
# that looks live but is not would be the worst of the options.
add_label('These take effect after the next reboot.', 14, Y_CAPTION, SW_W)

# ------------------------------------------------------- write & verify
n.fix_class_order()
n.fix_object_order()
xml_out = os.path.join(OUT, 'DisplayInspector.built.xml')
n.write(xml_out)
nib_out = os.path.join(OUT, 'data.nib')
subprocess.check_call([os.path.join(NIBMAKER, 'xml2nib'), '-r',
                       '-o', nib_out, xml_out])
subprocess.check_call([os.path.join(NIBMAKER, 'nibroundtrip'), nib_out])
subprocess.check_call(['python3', 'tools/validate-xml.py',
                       os.path.abspath(xml_out)], cwd=NIBMAKER)
print('built', nib_out)
