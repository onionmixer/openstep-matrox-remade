#
# OpenStepMGAProbe_reloc - kernel-loadable read-only PCI probe.
#

NAME = OpenStepMGAProbe_reloc

PROJECTVERSION = 0.1
LANGUAGE = English

CLASSES = OpenStepMGAProbe.m
HFILES =
OTHERSRCS = Makefile Makefile.preamble Makefile.postamble\
            Makefile.driver_preamble Load_Commands.sect Unload_Commands.sect

MAKEFILEDIR = /NextDeveloper/Makefiles/app
MAKEFILE = tool.make
SOURCEMODE = 444

-include Makefile.preamble
include $(MAKEFILEDIR)/$(MAKEFILE)
-include Makefile.postamble
-include Makefile.dependencies
