#include <stdio.h>

#include "OpenStepMGAG450RecoveryConfig.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_G450_RECOVERY_CONFIG_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_config(OSMGAG450RecoveryConfig *config)
{
    config->driver_name = "OpenStepMGAReplacementDisplay";
    config->location = "Dev:0 Func:0 Bus:4";
    config->auto_detect_id = "0x0525102B";
    config->display_mode =
        "Height: 1200 Width: 1600 Refresh: 60Hz ColorSpace: RGB:888/32";
    config->memory_size_mib = "16";
    config->recovery_profile = "P-recovery";
}

int
main(void)
{
    OSMGAG450RecoveryConfig config;
    OSMGAG450RecoveryConfigReason reason;

    make_config(&config);
    expect(OSMGAValidateG450RecoveryConfig(&config, &reason) == 1,
           "approved-config");
    expect(reason == OSMGA_G450_RECOVERY_CONFIG_OK, "approved-reason");

    make_config(&config);
    config.location = "Dev:0 Func:1 Bus:4";
    expect(OSMGAValidateG450RecoveryConfig(&config, &reason) == 0,
           "wrong-function-rejected");
    expect(reason == OSMGA_G450_RECOVERY_CONFIG_LOCATION,
           "wrong-function-reason");

    make_config(&config);
    config.memory_size_mib = "32";
    expect(OSMGAValidateG450RecoveryConfig(&config, &reason) == 0,
           "memory-expansion-rejected");
    expect(reason == OSMGA_G450_RECOVERY_CONFIG_MEMORY_SIZE,
           "memory-expansion-reason");

    make_config(&config);
    config.recovery_profile = "P-original";
    expect(OSMGAValidateG450RecoveryConfig(&config, &reason) == 0,
           "wrong-profile-rejected");
    expect(reason == OSMGA_G450_RECOVERY_CONFIG_PROFILE,
           "wrong-profile-reason");

    expect(OSMGAValidateG450RecoveryConfig(0, &reason) == 0,
           "null-config-rejected");
    expect(reason == OSMGA_G450_RECOVERY_CONFIG_INVALID_ARGUMENT,
           "null-config-reason");
    expect(OSMGAG450RecoveryConfigReasonString(
               OSMGA_G450_RECOVERY_CONFIG_OK)[0] == 'o', "reason-string");
    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_G450_RECOVERY_CONFIG_TEST_STATUS=pass\n");
    return 0;
}
