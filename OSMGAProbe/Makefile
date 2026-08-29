#
# OpenStepMGAProbe - read-only PCI inventory probe for the active MGA card.
#

NAME = OpenStepMGAProbe

PROJECTVERSION = 0.1
LANGUAGE = English

LOCAL_RESOURCES = Localizable.strings
GLOBAL_RESOURCES = Default.table
TOOLS = OpenStepMGAProbe_reloc.tproj

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
