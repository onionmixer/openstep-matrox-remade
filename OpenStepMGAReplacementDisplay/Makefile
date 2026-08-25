#
# OpenStepMGAReplacementDisplay - driver bundle.
#
# The bundle executable holds the Configure.app inspector
# (OSMGADisplayInspector, old-AppKit code loaded inside Configure); the
# kernel driver is the separate TOOLS subproject below.  common.make's
# recursive rule does not pass CLASSES or OTHER_CFLAGS down to a
# subproject, so adding the inspector cannot affect the kernel build.
#

NAME = OpenStepMGAReplacementDisplay

PROJECTVERSION = 0.1
LANGUAGE = English

LOCAL_RESOURCES = Localizable.strings DisplayInspector.nib
#
# The instance table and the mode list are resources, not build products.
#
# They were left out, and the two sibling drivers in this workspace both list
# their Instance0.table here -- which is why only this bundle could be built
# and installed without one.  On 2026-08-25 that happened: an installer
# removed the installed bundle and copied a freshly built one that had no
# instance table, so the driver loaded, configured no instance, claimed no
# card, and the console came up on VGA at 800x600 in four colours.
#
# With them here the BUILD produces a complete bundle, so a complete bundle
# is what any route installs.
#
GLOBAL_RESOURCES = Default.table Instance0.table Display.modes

CLASSES = OSMGADisplayInspector.m
HFILES = OSMGADisplayInspector.h
TOOLS = OpenStepMGAReplacementDisplay_reloc.tproj

OTHERSRCS = Makefile Makefile.preamble Makefile.postamble README.md compat

MAKEFILEDIR = /NextDeveloper/Makefiles/app
MAKEFILE = bundle.make
SOURCEMODE = 444
BUNDLE_EXTENSION = config

-include Makefile.preamble
include $(MAKEFILEDIR)/$(MAKEFILE)
-include Makefile.postamble
-include Makefile.dependencies
