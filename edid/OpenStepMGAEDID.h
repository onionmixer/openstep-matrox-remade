/*
 * OpenStepMGAEDID.h - pure-C EDID base-block parser and fixed-mode policy.
 *
 * This module has no DriverKit, PCI, MMIO, DDC, or display-driver dependency.
 * It is intended for a future replacement display driver's D0 policy stage.
 */

#ifndef OPENSTEP_MGA_EDID_H
#define OPENSTEP_MGA_EDID_H

#define OSMGA_EDID_BLOCK_SIZE 128
#define OSMGA_EDID_REFRESH_TOLERANCE_MILLIHZ 1000UL

typedef enum {
    OSMGA_EDID_REASON_NONE = 0,
    OSMGA_EDID_REASON_NO_EDID,
    OSMGA_EDID_REASON_INVALID_HEADER,
    OSMGA_EDID_REASON_INVALID_CHECKSUM,
    OSMGA_EDID_REASON_INVALID_MANUFACTURER,
    OSMGA_EDID_REASON_UNSUPPORTED_VERSION,
    OSMGA_EDID_REASON_INVALID_MANUAL_MODE,
    OSMGA_EDID_REASON_NO_PREFERRED_TIMING,
    OSMGA_EDID_REASON_INTERLACED_PREFERRED,
    OSMGA_EDID_REASON_UNSUPPORTED_PREFERRED
} OSMGAEDIDReason;

typedef struct {
    unsigned short width;
    unsigned short height;
    unsigned long refresh_millihz;
} OSMGAMode;

typedef struct {
    int valid;
    char manufacturer[4];
    unsigned char version;
    unsigned char revision;
    unsigned char extension_count;
    unsigned short product_code;
    unsigned long serial_number;
    int has_preferred_mode;
    int preferred_interlaced;
    OSMGAMode preferred_mode;
    OSMGAEDIDReason reason;
} OSMGAEDIDInfo;

typedef enum {
    OSMGA_MODE_DECISION_MANUAL = 0,
    OSMGA_MODE_DECISION_EDID_PREFERRED,
    OSMGA_MODE_DECISION_FALLBACK
} OSMGAModeDecision;

/* Pure arithmetic result; available_bytes must come from a separately proved
 * board profile, never from an unverified current-display configuration. */
typedef enum {
    OSMGA_MODE_MEMORY_OK = 0,
    OSMGA_MODE_MEMORY_INVALID_ARGUMENT,
    OSMGA_MODE_MEMORY_UNSUPPORTED_FORMAT,
    OSMGA_MODE_MEMORY_PITCH_TOO_SMALL,
    OSMGA_MODE_MEMORY_OVERFLOW,
    OSMGA_MODE_MEMORY_INSUFFICIENT
} OSMGAModeMemoryReason;

/*
 * P3 admission is an evidence policy, not a hardware probe.  Each nonzero
 * field records that the named condition was independently established in a
 * reviewed record; configuration profiles and PCI board-name catalogues do
 * not satisfy the VRAM fields.
 */
typedef struct {
    int pci_inventory_verified;
    int physical_vram_size_verified;
    int physical_vram_type_verified;
    int existing_owner_offscreen_range_verified;
    int mapping_compatibility_verified;
} OSMGAP3Admission;

typedef enum {
    OSMGA_P3_GATE_READY = 0,
    OSMGA_P3_GATE_PCI_INVENTORY,
    OSMGA_P3_GATE_VRAM_SIZE,
    OSMGA_P3_GATE_VRAM_TYPE,
    OSMGA_P3_GATE_OFFSCREEN_RANGE,
    OSMGA_P3_GATE_MAPPING_COMPATIBILITY
} OSMGAP3Gate;

/*
 * Parse exactly one 128-byte EDID base block.  Returns 1 only if its header
 * and checksum are valid.  A valid EDID block can still lack a usable DTD.
 */
int OSMGAParseBaseEDID(const unsigned char *block, OSMGAEDIDInfo *info);

/* Stable diagnostic text for logs; never use it as a hardware decision. */
const char *OSMGAEDIDReasonString(OSMGAEDIDReason reason);

/* Parse OpenStep's manual Display Mode form: Height, Width, Refresh in Hz. */
int OSMGAParseManualDisplayMode(const char *text, OSMGAMode *mode);

/*
 * Choose a mode without programming hardware.  A non-null manual_mode wins
 * only when it exactly matches the supplied fixed table.  Otherwise, an EDID
 * preferred mode must match resolution and refresh tolerance in that table.
 * A 0 return means retain the caller's known-good profile fallback.
 */
int OSMGASelectDisplayMode(const OSMGAEDIDInfo *info,
                           const OSMGAMode *manual_mode,
                           const OSMGAMode *fixed_modes,
                           unsigned int fixed_mode_count,
                           OSMGAMode *selected_mode,
                           OSMGAModeDecision *decision,
                           OSMGAEDIDReason *fallback_reason);

/*
 * Validate a linear framebuffer footprint without mapping or touching
 * hardware.  Bits per pixel is limited to byte-addressable 8/16/24/32 modes.
 * On success, required_bytes receives pitch_bytes * height.  No output is
 * modified on failure.
 */
int OSMGAModeFitsLinearMemory(const OSMGAMode *mode,
                              unsigned int bits_per_pixel,
                              unsigned long pitch_bytes,
                              unsigned long available_bytes,
                              unsigned long *required_bytes,
                              OSMGAModeMemoryReason *reason);

/*
 * Return 1 only when all preconditions for sidecar P3 work are independently
 * verified.  This function performs no device action and does not authorize
 * a caller to map or write hardware; it merely makes the first failed gate
 * explicit.  `blocked_gate` is always written when it is non-null.
 */
int OSMGACanEnterP3(const OSMGAP3Admission *admission,
                    OSMGAP3Gate *blocked_gate);

#endif
