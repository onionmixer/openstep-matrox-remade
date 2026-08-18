#include <stdio.h>
#include "OpenStepMGAManualConfig.h"

static int failures;

static void
expect(int condition, const char *label)
{
    if (!condition) {
        printf("OPENSTEP_MGA_MANUAL_CONFIG_TEST=fail:%s\n", label);
        failures++;
    }
}

static void
expect_value(const char *input, int expected_ok, unsigned int expected_bytes,
             OSMGAManualMemoryStatus expected_status, const char *label)
{
    unsigned int bytes;
    OSMGAManualMemoryStatus status;
    int ok;

    bytes = 0xaaaaaaaaU;
    status = OSMGA_MANUAL_MEMORY_OK;
    ok = OSMGAParseManualMemoryMB(input, &bytes, &status);
    expect(ok == expected_ok, label);
    expect(bytes == expected_bytes, label);
    expect(status == expected_status, label);
}

int
main(void)
{
    expect_value(0, 0, 0U, OSMGA_MANUAL_MEMORY_MISSING, "null");
    expect_value("", 0, 0U, OSMGA_MANUAL_MEMORY_MISSING, "empty");
    expect_value("  16\t", 1, 16U * 1024U * 1024U,
                 OSMGA_MANUAL_MEMORY_OK, "16-mib");
    expect_value("32", 1, 32U * 1024U * 1024U,
                 OSMGA_MANUAL_MEMORY_OK, "32-mib");
    expect_value("3", 1, 3U * 1024U * 1024U,
                 OSMGA_MANUAL_MEMORY_OK, "original-minimum-mib");
    expect_value("63", 1, 63U * 1024U * 1024U,
                 OSMGA_MANUAL_MEMORY_OK, "original-maximum-mib");
    expect_value("2", 0, 0U, OSMGA_MANUAL_MEMORY_UNSUPPORTED,
                 "below-original-minimum");
    expect_value("64", 0, 0U, OSMGA_MANUAL_MEMORY_UNSUPPORTED,
                 "above-original-maximum");
    expect_value("8", 1, 8U * 1024U * 1024U,
                 OSMGA_MANUAL_MEMORY_OK, "8-mib");
    expect_value("15", 1, 15U * 1024U * 1024U,
                 OSMGA_MANUAL_MEMORY_OK, "15-original-range");
    expect_value("16MB", 0, 0U, OSMGA_MANUAL_MEMORY_INVALID, "suffix-invalid");
    expect_value("999999999999", 0, 0U, OSMGA_MANUAL_MEMORY_INVALID,
                 "overflow-invalid");
    expect(OSMGAManualMemoryStatusString(OSMGA_MANUAL_MEMORY_OK)[0] == 'o',
           "status-string-ok");
    expect(OSMGAManualMemoryStatusString(OSMGA_MANUAL_MEMORY_UNSUPPORTED)[0] == 'u',
           "status-string-unsupported");
    if (failures != 0)
        return 1;
    printf("OPENSTEP_MGA_MANUAL_CONFIG_TEST=pass\n");
    return 0;
}
