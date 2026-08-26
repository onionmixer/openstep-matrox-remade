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
#
# The inspector computes the same answer the driver computes, from the same
# sources, so the panel and the kernel cannot come to disagree about the
# arithmetic.  These three are copied in beside it by
# tools/build-matrox-driver.sh -- they live in the reloc subproject and in
# ../edid, and a build product is the only place both targets can see them.
#
# Compiling them twice, once into this bundle and once into the reloc
# executable, is intended: the two are separate link products with separate
# object directories, every exported symbol is OSMGA-prefixed, and the
# alternative -- a second copy of the arithmetic -- is what this arrangement
# exists to prevent.
#
CFILES = OpenStepMGAWindowMath.c \
         OpenStepMGAManualConfig.c \
         OpenStepMGAEDID.c
HFILES = OSMGADisplayInspector.h OpenStepMGAWindowMath.h \
         OpenStepMGAManualConfig.h OpenStepMGAEDID.h
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
