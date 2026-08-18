#
# OpenStepMGAReplacementDisplay_reloc - DriverKit display staging skeleton.
#

NAME = OpenStepMGAReplacementDisplay_reloc

PROJECTVERSION = 0.1
LANGUAGE = English

CLASSES = OpenStepMGAReplacementDisplay.m
#
# CFILES: driver-local sources plus the reviewed data-only R6 policy/generator
# modules (pure C89, no libc), copied in from ../../profile and ../../protocol
# at build time by the build driver.  They implement the mode transaction the
# driver runs in enterLinearMode.
#
CFILES = OpenStepMGAManualConfig.c \
         OpenStepMGAEDID.c \
         OpenStepMGAProfile.c \
         OpenStepMGATimingReview.c \
         OpenStepMGAModeReview.c \
         OpenStepMGAMappingReview.c \
         OpenStepMGARecoveryMatrix.c \
         OpenStepMGAG450PLL.c \
         OpenStepMGAG450PLLEncoding.c \
         OpenStepMGAG450CRTCPlan.c \
         OpenStepMGAG450PrimaryCRTCImage.c \
         OpenStepMGAG450PrimaryCRTCReadback.c \
         OpenStepMGAG450RangePlan.c \
         OpenStepMGABoundedPoll.c \
         OpenStepMGAModeTransaction.c
HFILES = OpenStepMGAReplacementDisplay.h OpenStepMGAManualConfig.h \
         OpenStepMGAEDID.h OpenStepMGAProfile.h OpenStepMGATimingReview.h \
         OpenStepMGAModeReview.h OpenStepMGAMappingReview.h \
         OpenStepMGARecoveryMatrix.h OpenStepMGAG450PLL.h \
         OpenStepMGAG450PLLEncoding.h OpenStepMGAG450CRTCPlan.h \
         OpenStepMGAG450PrimaryCRTCImage.h OpenStepMGAG450PrimaryCRTCReadback.h \
         OpenStepMGAG450RangePlan.h OpenStepMGABoundedPoll.h \
         OpenStepMGAModeTransaction.h
OTHERSRCS = Makefile Makefile.preamble Makefile.postamble\
            Makefile.driver_preamble Load_Commands.sect

MAKEFILEDIR = /NextDeveloper/Makefiles/app
MAKEFILE = tool.make
SOURCEMODE = 444

-include Makefile.preamble
include $(MAKEFILEDIR)/$(MAKEFILE)
-include Makefile.postamble
-include Makefile.dependencies
