#
# OSMGAProbe - read-only PCI inventory probe for the active MGA card.
#
# The bundle is OSMGAProbe; the class inside it is still OpenStepMGAProbe,
# which is what Class Names has to name.  Renamed with the display bundle
# on 2026-08-29 so one prefix is used throughout -- see docs/C3.
#

NAME = OSMGAProbe

PROJECTVERSION = 0.1
LANGUAGE = English

LOCAL_RESOURCES = Localizable.strings
GLOBAL_RESOURCES = Default.table
TOOLS = OSMGAProbe_reloc.tproj

OTHERSRCS = Makefile Makefile.preamble Makefile.postamble

MAKEFILEDIR = /NextDeveloper/Makefiles/app
MAKEFILE = bundle.make
INSTALLDIR = /private/Devices
INSTALLFLAGS = -c -s -m 755
SOURCEMODE = 444
BUNDLE_EXTENSION = config

-include Makefile.preamble
include $(MAKEFILEDIR)/$(MAKEFILE)
-include Makefile.postamble
-include Makefile.dependencies
