# OSMGAService - P2 message-only control service.
#
# The bundle is OSMGAService; the class inside it is still
# OpenStepMGAService, which is what Class Names has to name.

NAME = OSMGAService

PROJECTVERSION = 0.2
LANGUAGE = English

LOCAL_RESOURCES = Localizable.strings
GLOBAL_RESOURCES = Default.table
TOOLS = OSMGAService_reloc.tproj

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
