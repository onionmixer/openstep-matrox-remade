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
GLOBAL_RESOURCES = Default.table

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
