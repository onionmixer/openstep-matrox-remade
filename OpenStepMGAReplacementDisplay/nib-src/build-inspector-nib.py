#!/usr/bin/env python3
"""build-inspector-nib.py - build the driver's DisplayInspector.nib.

The base is Configure.app's own DisplayInspector.nib, taken byte for byte,
with only the File's Owner class renamed to OSMGADisplayInspector.  Onto
that we graft two switches for the driver's development flags.

    python3 nib-src/build-inspector-nib.py <nibmaker-dir> <stock-nib-dir> \\
            <switch-template.xml> <out-dir>

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

NIBMAKER, STOCK, SWITCH_TEMPLATE, OUT = sys.argv[1:5]
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

GROW = 70            # extra height for our two switches and the caption
SW_W, SW_H = 340, 15

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


add_switch('Run Storm 2D engine self-test at boot', 44,
           'stormSwitch', 'toggleStorm:')
add_switch('Publish offscreen VRAM as a character device', 24,
           'mmapSwitch', 'toggleMmap:')

# Both flags are read once, when the driver initialises, so say so: a
# switch that looks live but is not would be the worst of the three.
lbl = n.graft_from(tmpl, LABEL_SRC, obj_map=dict(LABEL_MAP))
n.reindex()
n.set_cstring(cell_of(lbl), 'Both take effect after the next reboot.')
n.add_subview(MODE_VIEW, lbl, 14, 6, SW_W, 14)
n.reindex()

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
