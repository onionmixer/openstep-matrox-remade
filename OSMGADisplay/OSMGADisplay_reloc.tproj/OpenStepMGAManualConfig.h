/*
 * Manual MGA memory configuration parser.
 *
 * This only validates an operator-supplied configuration-table value.  It
 * does not identify hardware, map memory, or authorize a display lifecycle.
 */

#ifndef OPENSTEP_MGA_MANUAL_CONFIG_H
#define OPENSTEP_MGA_MANUAL_CONFIG_H

typedef enum {
    OSMGA_MANUAL_MEMORY_OK = 0,
    OSMGA_MANUAL_MEMORY_MISSING,
    OSMGA_MANUAL_MEMORY_INVALID,
    OSMGA_MANUAL_MEMORY_UNSUPPORTED
} OSMGAManualMemoryStatus;

/*
 * Accepts original-driver-compatible nonzero values 3 through 63 (MiB) and
 * returns a byte count. Missing, malformed, and out-of-range values never
 * receive a fallback value. A later R2/R3 evidence review must still prove
 * that an accepted value equals the physical board total.
 */
int OSMGAParseManualMemoryMB(const char *value, unsigned int *bytes,
                             OSMGAManualMemoryStatus *status);
const char *OSMGAManualMemoryStatusString(OSMGAManualMemoryStatus status);

#endif /* OPENSTEP_MGA_MANUAL_CONFIG_H */
