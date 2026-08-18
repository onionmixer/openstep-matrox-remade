/* Pure-C implementation; see OpenStepMGAEDID.h for the hardware boundary. */

#include "OpenStepMGAEDID.h"

static const unsigned char edid_header[8] = {
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
};

static void
clear_info(OSMGAEDIDInfo *info)
{
    info->valid = 0;
    info->manufacturer[0] = '\0';
    info->manufacturer[1] = '\0';
    info->manufacturer[2] = '\0';
    info->manufacturer[3] = '\0';
    info->version = 0;
    info->revision = 0;
    info->extension_count = 0;
    info->product_code = 0;
    info->serial_number = 0;
    info->has_preferred_mode = 0;
    info->preferred_interlaced = 0;
    info->preferred_mode.width = 0;
    info->preferred_mode.height = 0;
    info->preferred_mode.refresh_millihz = 0;
    info->reason = OSMGA_EDID_REASON_NO_EDID;
}

static int
header_matches(const unsigned char *block)
{
    unsigned int index;

    for (index = 0; index < sizeof(edid_header); index++) {
        if (block[index] != edid_header[index]) {
            return 0;
        }
    }
    return 1;
}

static int
checksum_matches(const unsigned char *block)
{
    unsigned int index;
    unsigned int sum;

    sum = 0;
    for (index = 0; index < OSMGA_EDID_BLOCK_SIZE; index++) {
        sum = (sum + block[index]) & 0xffU;
    }
    return sum == 0;
}

static int
parse_manufacturer(const unsigned char *block, char *manufacturer)
{
    unsigned int encoded;
    unsigned int first;
    unsigned int second;
    unsigned int third;

    encoded = ((unsigned int)block[8] << 8) | block[9];
    first = (encoded >> 10) & 0x1fU;
    second = (encoded >> 5) & 0x1fU;
    third = encoded & 0x1fU;

    manufacturer[0] = (first >= 1 && first <= 26) ? (char)('A' + first - 1) : '?';
    manufacturer[1] = (second >= 1 && second <= 26) ? (char)('A' + second - 1) : '?';
    manufacturer[2] = (third >= 1 && third <= 26) ? (char)('A' + third - 1) : '?';
    manufacturer[3] = '\0';
    return first >= 1 && first <= 26 && second >= 1 && second <= 26 &&
           third >= 1 && third <= 26;
}

static int
parse_detailed_timing(const unsigned char *descriptor, OSMGAMode *mode,
                      int *interlaced)
{
    unsigned long pixel_clock_hz;
    unsigned long horizontal_total;
    unsigned long vertical_total;
    unsigned long timing_total;
    unsigned long refresh_hz;
    unsigned long refresh_remainder;
    unsigned int pixel_clock_10khz;
    unsigned int horizontal_active;
    unsigned int horizontal_blank;
    unsigned int vertical_active;
    unsigned int vertical_blank;

    pixel_clock_10khz = (unsigned int)descriptor[0] |
                         ((unsigned int)descriptor[1] << 8);
    if (pixel_clock_10khz == 0) {
        return 0;
    }

    horizontal_active = (unsigned int)descriptor[2] |
                        (((unsigned int)descriptor[4] & 0xf0U) << 4);
    horizontal_blank = (unsigned int)descriptor[3] |
                       (((unsigned int)descriptor[4] & 0x0fU) << 8);
    vertical_active = (unsigned int)descriptor[5] |
                      (((unsigned int)descriptor[7] & 0xf0U) << 4);
    vertical_blank = (unsigned int)descriptor[6] |
                     (((unsigned int)descriptor[7] & 0x0fU) << 8);
    horizontal_total = (unsigned long)horizontal_active + horizontal_blank;
    vertical_total = (unsigned long)vertical_active + vertical_blank;

    if (horizontal_active == 0 || vertical_active == 0 ||
        horizontal_total == 0 || vertical_total == 0) {
        return 0;
    }

    pixel_clock_hz = (unsigned long)pixel_clock_10khz * 10000UL;
    timing_total = horizontal_total * vertical_total;
    refresh_hz = pixel_clock_hz / timing_total;
    refresh_remainder = pixel_clock_hz % timing_total;
    /* Keep the final millihertz value representable on 32-bit OPENSTEP. */
    if (refresh_hz > 4294967UL) {
        return 0;
    }
    mode->width = (unsigned short)horizontal_active;
    mode->height = (unsigned short)vertical_active;
    /* 100 mHz resolution avoids a 32-bit pixel_clock_hz * 1000 overflow. */
    mode->refresh_millihz = refresh_hz * 1000UL +
                             ((refresh_remainder * 10UL) / timing_total) * 100UL;
    *interlaced = (descriptor[17] & 0x80U) != 0;
    return 1;
}

int
OSMGAParseBaseEDID(const unsigned char *block, OSMGAEDIDInfo *info)
{
    const unsigned char *preferred_descriptor;

    if (info == 0) {
        return 0;
    }
    clear_info(info);
    if (block == 0) {
        return 0;
    }
    if (!header_matches(block)) {
        info->reason = OSMGA_EDID_REASON_INVALID_HEADER;
        return 0;
    }
    if (!checksum_matches(block)) {
        info->reason = OSMGA_EDID_REASON_INVALID_CHECKSUM;
        return 0;
    }

    info->version = block[18];
    info->revision = block[19];
    info->extension_count = block[126];
    if (info->version != 1) {
        info->reason = OSMGA_EDID_REASON_UNSUPPORTED_VERSION;
        return 0;
    }

    info->valid = 1;
    info->reason = OSMGA_EDID_REASON_NO_PREFERRED_TIMING;
    if (!parse_manufacturer(block, info->manufacturer)) {
        info->valid = 0;
        info->reason = OSMGA_EDID_REASON_INVALID_MANUFACTURER;
        return 0;
    }
    info->product_code = (unsigned short)((unsigned int)block[10] |
                                          ((unsigned int)block[11] << 8));
    info->serial_number = (unsigned long)block[12] |
                          ((unsigned long)block[13] << 8) |
                          ((unsigned long)block[14] << 16) |
                          ((unsigned long)block[15] << 24);

    /* In an EDID base block, only detailed timing descriptor 0 is preferred. */
    if ((block[24] & 0x02U) == 0) {
        return 1;
    }
    preferred_descriptor = block + 54;
    if (parse_detailed_timing(preferred_descriptor, &info->preferred_mode,
                              &info->preferred_interlaced)) {
        info->has_preferred_mode = 1;
        info->reason = info->preferred_interlaced ?
                       OSMGA_EDID_REASON_INTERLACED_PREFERRED :
                       OSMGA_EDID_REASON_NONE;
    }
    return 1;
}

const char *
OSMGAEDIDReasonString(OSMGAEDIDReason reason)
{
    switch (reason) {
    case OSMGA_EDID_REASON_NONE:
        return "none";
    case OSMGA_EDID_REASON_NO_EDID:
        return "no-edid";
    case OSMGA_EDID_REASON_INVALID_HEADER:
        return "invalid-header";
    case OSMGA_EDID_REASON_INVALID_CHECKSUM:
        return "invalid-checksum";
    case OSMGA_EDID_REASON_INVALID_MANUFACTURER:
        return "invalid-manufacturer";
    case OSMGA_EDID_REASON_UNSUPPORTED_VERSION:
        return "unsupported-version";
    case OSMGA_EDID_REASON_INVALID_MANUAL_MODE:
        return "invalid-manual-mode";
    case OSMGA_EDID_REASON_NO_PREFERRED_TIMING:
        return "no-preferred-timing";
    case OSMGA_EDID_REASON_INTERLACED_PREFERRED:
        return "interlaced-preferred";
    case OSMGA_EDID_REASON_UNSUPPORTED_PREFERRED:
        return "unsupported-preferred";
    }
    return "unknown";
}

static const char *
skip_spaces(const char *text)
{
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    return text;
}

static int
consume_text(const char **cursor, const char *expected)
{
    const char *text;

    text = *cursor;
    while (*expected != '\0') {
        if (*text != *expected) {
            return 0;
        }
        text++;
        expected++;
    }
    *cursor = text;
    return 1;
}

static int
consume_number(const char **cursor, unsigned long *value)
{
    const char *text;
    unsigned long result;
    unsigned int digit;

    text = *cursor;
    result = 0;
    if (*text < '0' || *text > '9') {
        return 0;
    }
    while (*text >= '0' && *text <= '9') {
        digit = (unsigned int)(*text - '0');
        if (result > (4294967295UL - digit) / 10UL) {
            return 0;
        }
        result = result * 10UL + digit;
        text++;
    }
    *cursor = text;
    *value = result;
    return 1;
}

int
OSMGAParseManualDisplayMode(const char *text, OSMGAMode *mode)
{
    unsigned long height;
    unsigned long width;
    unsigned long refresh_hz;

    if (text == 0 || mode == 0) {
        return 0;
    }
    text = skip_spaces(text);
    if (!consume_text(&text, "Height:")) {
        return 0;
    }
    text = skip_spaces(text);
    if (!consume_number(&text, &height) || height == 0 || height > 65535UL) {
        return 0;
    }
    text = skip_spaces(text);
    if (!consume_text(&text, "Width:")) {
        return 0;
    }
    text = skip_spaces(text);
    if (!consume_number(&text, &width) || width == 0 || width > 65535UL) {
        return 0;
    }
    text = skip_spaces(text);
    if (!consume_text(&text, "Refresh:")) {
        return 0;
    }
    text = skip_spaces(text);
    if (!consume_number(&text, &refresh_hz) || refresh_hz == 0 ||
        refresh_hz > 4294967UL) {
        return 0;
    }
    if (!consume_text(&text, "Hz")) {
        return 0;
    }
    text = skip_spaces(text);
    if (*text != '\0') {
        if (!consume_text(&text, "ColorSpace:")) {
            return 0;
        }
        text = skip_spaces(text);
        if (*text == '\0') {
            return 0;
        }
    }
    mode->width = (unsigned short)width;
    mode->height = (unsigned short)height;
    mode->refresh_millihz = refresh_hz * 1000UL;
    return 1;
}

static unsigned long
refresh_difference(unsigned long first, unsigned long second)
{
    return first >= second ? first - second : second - first;
}

static int
mode_matches(const OSMGAMode *first, const OSMGAMode *second,
             unsigned long refresh_tolerance)
{
    return first->width == second->width && first->height == second->height &&
           refresh_difference(first->refresh_millihz, second->refresh_millihz) <=
           refresh_tolerance;
}

int
OSMGASelectDisplayMode(const OSMGAEDIDInfo *info,
                       const OSMGAMode *manual_mode,
                       const OSMGAMode *fixed_modes,
                       unsigned int fixed_mode_count,
                       OSMGAMode *selected_mode,
                       OSMGAModeDecision *decision,
                       OSMGAEDIDReason *fallback_reason)
{
    unsigned int index;

    if (selected_mode == 0 || decision == 0 || fallback_reason == 0) {
        return 0;
    }
    *decision = OSMGA_MODE_DECISION_FALLBACK;
    if (fixed_modes == 0 || fixed_mode_count == 0) {
        *fallback_reason = manual_mode != 0 ?
                           OSMGA_EDID_REASON_INVALID_MANUAL_MODE :
                           OSMGA_EDID_REASON_UNSUPPORTED_PREFERRED;
        return 0;
    }
    if (manual_mode != 0) {
        for (index = 0; index < fixed_mode_count; index++) {
            if (mode_matches(&fixed_modes[index], manual_mode, 0)) {
                *selected_mode = fixed_modes[index];
                *decision = OSMGA_MODE_DECISION_MANUAL;
                *fallback_reason = OSMGA_EDID_REASON_NONE;
                return 1;
            }
        }
        *fallback_reason = OSMGA_EDID_REASON_INVALID_MANUAL_MODE;
        return 0;
    }
    if (info == 0 || !info->valid) {
        *fallback_reason = info == 0 ? OSMGA_EDID_REASON_NO_EDID : info->reason;
        return 0;
    }
    if (!info->has_preferred_mode) {
        *fallback_reason = OSMGA_EDID_REASON_NO_PREFERRED_TIMING;
        return 0;
    }
    if (info->preferred_interlaced) {
        *fallback_reason = OSMGA_EDID_REASON_INTERLACED_PREFERRED;
        return 0;
    }
    for (index = 0; index < fixed_mode_count; index++) {
        if (mode_matches(&fixed_modes[index], &info->preferred_mode,
                         OSMGA_EDID_REFRESH_TOLERANCE_MILLIHZ)) {
            *selected_mode = fixed_modes[index];
            *decision = OSMGA_MODE_DECISION_EDID_PREFERRED;
            *fallback_reason = OSMGA_EDID_REASON_NONE;
            return 1;
        }
    }
    *fallback_reason = OSMGA_EDID_REASON_UNSUPPORTED_PREFERRED;
    return 0;
}

int
OSMGAModeFitsLinearMemory(const OSMGAMode *mode, unsigned int bits_per_pixel,
                          unsigned long pitch_bytes, unsigned long available_bytes,
                          unsigned long *required_bytes,
                          OSMGAModeMemoryReason *reason)
{
    unsigned long bytes_per_pixel;
    unsigned long minimum_pitch;
    unsigned long total_bytes;

    if (reason == 0 || required_bytes == 0) {
        return 0;
    }
    if (mode == 0 || mode->width == 0 || mode->height == 0 ||
        pitch_bytes == 0 || available_bytes == 0) {
        *reason = OSMGA_MODE_MEMORY_INVALID_ARGUMENT;
        return 0;
    }
    if (bits_per_pixel != 8 && bits_per_pixel != 16 &&
        bits_per_pixel != 24 && bits_per_pixel != 32) {
        *reason = OSMGA_MODE_MEMORY_UNSUPPORTED_FORMAT;
        return 0;
    }
    bytes_per_pixel = (unsigned long)(bits_per_pixel / 8);
    if ((unsigned long)mode->width > 4294967295UL / bytes_per_pixel) {
        *reason = OSMGA_MODE_MEMORY_OVERFLOW;
        return 0;
    }
    minimum_pitch = (unsigned long)mode->width * bytes_per_pixel;
    if (pitch_bytes < minimum_pitch) {
        *reason = OSMGA_MODE_MEMORY_PITCH_TOO_SMALL;
        return 0;
    }
    if ((unsigned long)mode->height > 4294967295UL / pitch_bytes) {
        *reason = OSMGA_MODE_MEMORY_OVERFLOW;
        return 0;
    }
    total_bytes = pitch_bytes * (unsigned long)mode->height;
    if (total_bytes > available_bytes) {
        *reason = OSMGA_MODE_MEMORY_INSUFFICIENT;
        return 0;
    }
    *required_bytes = total_bytes;
    *reason = OSMGA_MODE_MEMORY_OK;
    return 1;
}

int
OSMGACanEnterP3(const OSMGAP3Admission *admission, OSMGAP3Gate *blocked_gate)
{
    if (blocked_gate == 0) {
        return 0;
    }
    if (admission == 0 || !admission->pci_inventory_verified) {
        *blocked_gate = OSMGA_P3_GATE_PCI_INVENTORY;
        return 0;
    }
    if (!admission->physical_vram_size_verified) {
        *blocked_gate = OSMGA_P3_GATE_VRAM_SIZE;
        return 0;
    }
    if (!admission->physical_vram_type_verified) {
        *blocked_gate = OSMGA_P3_GATE_VRAM_TYPE;
        return 0;
    }
    if (!admission->existing_owner_offscreen_range_verified) {
        *blocked_gate = OSMGA_P3_GATE_OFFSCREEN_RANGE;
        return 0;
    }
    if (!admission->mapping_compatibility_verified) {
        *blocked_gate = OSMGA_P3_GATE_MAPPING_COMPATIBILITY;
        return 0;
    }
    *blocked_gate = OSMGA_P3_GATE_READY;
    return 1;
}
