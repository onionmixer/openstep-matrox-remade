/* Offline admission for the single reviewed P-recovery configuration. */

#ifndef OPENSTEP_MGA_G450_RECOVERY_CONFIG_H
#define OPENSTEP_MGA_G450_RECOVERY_CONFIG_H

typedef struct {
    const char *driver_name;
    const char *location;
    const char *auto_detect_id;
    const char *display_mode;
    const char *memory_size_mib;
    const char *recovery_profile;
} OSMGAG450RecoveryConfig;

typedef enum {
    OSMGA_G450_RECOVERY_CONFIG_OK = 0,
    OSMGA_G450_RECOVERY_CONFIG_INVALID_ARGUMENT,
    OSMGA_G450_RECOVERY_CONFIG_DRIVER_NAME,
    OSMGA_G450_RECOVERY_CONFIG_LOCATION,
    OSMGA_G450_RECOVERY_CONFIG_DEVICE_ID,
    OSMGA_G450_RECOVERY_CONFIG_DISPLAY_MODE,
    OSMGA_G450_RECOVERY_CONFIG_MEMORY_SIZE,
    OSMGA_G450_RECOVERY_CONFIG_PROFILE
} OSMGAG450RecoveryConfigReason;

/* Validate extracted table values only; this neither queries nor changes a system. */
int OSMGAValidateG450RecoveryConfig(const OSMGAG450RecoveryConfig *config,
                                    OSMGAG450RecoveryConfigReason *reason);

const char *OSMGAG450RecoveryConfigReasonString(
    OSMGAG450RecoveryConfigReason reason);

#endif /* OPENSTEP_MGA_G450_RECOVERY_CONFIG_H */
