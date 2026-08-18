/* Pure-C validation of one reviewed P-recovery table record. */

#include "OpenStepMGAG450RecoveryConfig.h"

static int
same_string(const char *first, const char *second)
{
    if (first == 0 || second == 0) {
        return 0;
    }
    while (*first != '\0' && *second != '\0') {
        if (*first != *second) {
            return 0;
        }
        first++;
        second++;
    }
    return *first == *second;
}

int
OSMGAValidateG450RecoveryConfig(const OSMGAG450RecoveryConfig *config,
                                OSMGAG450RecoveryConfigReason *reason)
{
    if (reason == 0) {
        return 0;
    }
    if (config == 0) {
        *reason = OSMGA_G450_RECOVERY_CONFIG_INVALID_ARGUMENT;
        return 0;
    }
    if (!same_string(config->driver_name, "OpenStepMGAReplacementDisplay")) {
        *reason = OSMGA_G450_RECOVERY_CONFIG_DRIVER_NAME;
        return 0;
    }
    if (!same_string(config->location, "Dev:0 Func:0 Bus:4")) {
        *reason = OSMGA_G450_RECOVERY_CONFIG_LOCATION;
        return 0;
    }
    if (!same_string(config->auto_detect_id, "0x0525102B")) {
        *reason = OSMGA_G450_RECOVERY_CONFIG_DEVICE_ID;
        return 0;
    }
    if (!same_string(config->display_mode,
                     "Height: 1200 Width: 1600 Refresh: 60Hz ColorSpace: RGB:888/32")) {
        *reason = OSMGA_G450_RECOVERY_CONFIG_DISPLAY_MODE;
        return 0;
    }
    if (!same_string(config->memory_size_mib, "16")) {
        *reason = OSMGA_G450_RECOVERY_CONFIG_MEMORY_SIZE;
        return 0;
    }
    if (!same_string(config->recovery_profile, "P-recovery")) {
        *reason = OSMGA_G450_RECOVERY_CONFIG_PROFILE;
        return 0;
    }
    *reason = OSMGA_G450_RECOVERY_CONFIG_OK;
    return 1;
}

const char *
OSMGAG450RecoveryConfigReasonString(OSMGAG450RecoveryConfigReason reason)
{
    switch (reason) {
    case OSMGA_G450_RECOVERY_CONFIG_OK:
        return "ok";
    case OSMGA_G450_RECOVERY_CONFIG_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_G450_RECOVERY_CONFIG_DRIVER_NAME:
        return "driver-name";
    case OSMGA_G450_RECOVERY_CONFIG_LOCATION:
        return "location";
    case OSMGA_G450_RECOVERY_CONFIG_DEVICE_ID:
        return "device-id";
    case OSMGA_G450_RECOVERY_CONFIG_DISPLAY_MODE:
        return "display-mode";
    case OSMGA_G450_RECOVERY_CONFIG_MEMORY_SIZE:
        return "memory-size";
    case OSMGA_G450_RECOVERY_CONFIG_PROFILE:
        return "profile";
    }
    return "unknown";
}
