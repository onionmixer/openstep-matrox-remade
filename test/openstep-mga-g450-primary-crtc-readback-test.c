#include <stdio.h>

#include "OpenStepMGAG450PrimaryCRTCReadback.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_G450_PRIMARY_CRTC_READBACK_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_matching_snapshot(OSMGAG450PrimaryCRTCImage *expected,
                       OSMGAG450PrimaryCRTCReadback *observed)
{
    unsigned int index;

    for (index = 0U; index < OSMGA_G450_PRIMARY_CRTC_REGISTER_COUNT; index++) {
        expected->crtc[index] = (unsigned char)(index + 3U);
        observed->crtc[index] = expected->crtc[index];
    }
    for (index = 0U; index < OSMGA_G450_PRIMARY_EXT_REGISTER_COUNT; index++) {
        expected->extended[index] = (unsigned char)(0x80U + index);
        observed->extended[index] = expected->extended[index];
    }
    expected->misc_output_or = 0x0cU;
    observed->misc_output = 0xffU;
}

int
main(void)
{
    OSMGAG450PrimaryCRTCImage expected;
    OSMGAG450PrimaryCRTCReadback observed;
    OSMGAG450PrimaryCRTCReadbackReason reason;

    make_matching_snapshot(&expected, &observed);
    expect(OSMGAValidateG450PrimaryCRTCReadback(&expected, &observed,
                                                &reason) == 1,
           "matching-snapshot");
    expect(reason == OSMGA_G450_PRIMARY_CRTC_READBACK_OK, "matching-reason");

    make_matching_snapshot(&expected, &observed);
    observed.crtc[4] = 0U;
    expect(OSMGAValidateG450PrimaryCRTCReadback(&expected, &observed,
                                                &reason) == 0,
           "standard-mismatch-rejected");
    expect(reason == OSMGA_G450_PRIMARY_CRTC_READBACK_STANDARD_MISMATCH,
           "standard-mismatch-reason");

    make_matching_snapshot(&expected, &observed);
    observed.extended[2] = 0U;
    expect(OSMGAValidateG450PrimaryCRTCReadback(&expected, &observed,
                                                &reason) == 0,
           "extended-mismatch-rejected");
    expect(reason == OSMGA_G450_PRIMARY_CRTC_READBACK_EXTENDED_MISMATCH,
           "extended-mismatch-reason");

    make_matching_snapshot(&expected, &observed);
    observed.misc_output = 0x03U;
    expect(OSMGAValidateG450PrimaryCRTCReadback(&expected, &observed,
                                                &reason) == 0,
           "misc-clock-mismatch-rejected");
    expect(reason == OSMGA_G450_PRIMARY_CRTC_READBACK_MISC_CLOCK_MISMATCH,
           "misc-clock-mismatch-reason");

    expect(OSMGAValidateG450PrimaryCRTCReadback(0, &observed, &reason) == 0,
           "invalid-argument-rejected");
    expect(reason == OSMGA_G450_PRIMARY_CRTC_READBACK_INVALID_ARGUMENT,
           "invalid-argument-reason");
    expect(OSMGAG450PrimaryCRTCReadbackReasonString(
               OSMGA_G450_PRIMARY_CRTC_READBACK_OK)[0] == 'o',
           "reason-string");
    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_G450_PRIMARY_CRTC_READBACK_TEST_STATUS=pass\n");
    return 0;
}
