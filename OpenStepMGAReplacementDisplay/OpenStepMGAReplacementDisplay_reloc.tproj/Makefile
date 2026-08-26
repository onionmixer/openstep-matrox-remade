#
# OpenStepMGAReplacementDisplay_reloc - DriverKit display staging skeleton.
#

NAME = OpenStepMGAReplacementDisplay_reloc

PROJECTVERSION = 0.1
LANGUAGE = English

CLASSES = OpenStepMGAReplacementDisplay.m
#
# CFILES: driver-local sources plus the EDID manual-mode parser (pure C89, no
# libc), copied in from ../../edid at build time by the build driver.  The
# profile/ R2-R3 review modules and the protocol/ mode-transaction+
# bounded-poll modules are a separate design track (used by
# protocol/OpenStepMGAModeTransaction.h and mesa/); the driver's own mode
# programming is direct X.Org-derived register writes in
# OpenStepMGAReplacementDisplay.m and does not link them.
#
CFILES = OpenStepMGAManualConfig.c \
         OpenStepMGAWindowMath.c \
         OpenStepMGAPciSurvey.c \
         OpenStepMGAEDID.c \
         OpenStepMGAWarpUcode.c \
         OpenStepMGAHW3D.c
HFILES = OpenStepMGAReplacementDisplay.h OpenStepMGAManualConfig.h \
         OpenStepMGAWindowMath.h \
         OpenStepMGAPciSurvey.h \
         OpenStepMGAEDID.h \
         OpenStepMGAWarpUcode.h \
         OpenStepMGAHW3D.h
OTHERSRCS = Makefile Makefile.preamble Makefile.postamble\
            Makefile.driver_preamble Load_Commands.sect

MAKEFILEDIR = /NextDeveloper/Makefiles/app
MAKEFILE = tool.make
SOURCEMODE = 444

-include Makefile.preamble
include $(MAKEFILEDIR)/$(MAKEFILE)
-include Makefile.postamble
-include Makefile.dependencies
