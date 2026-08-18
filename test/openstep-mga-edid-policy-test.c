#include <stdio.h>

#include "OpenStepMGAEDID.h"

#ifdef OSMGA_TARGET_NETNAME_BOOTSTRAP
#include <mach/mach.h>
#include <servers/netname.h>

static void
bootstrap_netname_lookup(void)
{
    port_t port;

    port = PORT_NULL;
    (void)netname_look_up(name_server_port, "", "openstepmga-d0-bootstrap",
                          &port);
}
#endif

static int failures;

static void
clear_bytes(unsigned char *destination, unsigned int count)
{
    unsigned int index;

    for (index = 0; index < count; index++) {
        destination[index] = 0;
    }
}

static void
copy_bytes(unsigned char *destination, const unsigned char *source,
           unsigned int count)
{
    unsigned int index;

    for (index = 0; index < count; index++) {
        destination[index] = source[index];
    }
}

static int
same_text(const char *first, const char *second)
{
    while (*first != '\0' && *first == *second) {
        first++;
        second++;
    }
    return *first == *second;
}

static void
expect(int condition, const char *label)
{
    if (!condition) {
        fprintf(stderr, "OPENSTEP_MGA_EDID_TEST_FAIL=%s\n", label);
        failures++;
    }
}

static void
finish_checksum(unsigned char *block)
{
    unsigned int index;
    unsigned int sum;

    block[127] = 0;
    sum = 0;
    for (index = 0; index < OSMGA_EDID_BLOCK_SIZE; index++) {
        sum = (sum + block[index]) & 0xffU;
    }
    block[127] = (unsigned char)((0x100U - sum) & 0xffU);
}

static void
make_base_edid(unsigned char *block)
{
    unsigned int manufacturer;
    unsigned char *descriptor;

    clear_bytes(block, OSMGA_EDID_BLOCK_SIZE);
    block[0] = 0x00;
    block[1] = 0xff;
    block[2] = 0xff;
    block[3] = 0xff;
    block[4] = 0xff;
    block[5] = 0xff;
    block[6] = 0xff;
    block[7] = 0x00;
    manufacturer = (13U << 10) | (7U << 5) | 1U; /* MGA */
    block[8] = (unsigned char)(manufacturer >> 8);
    block[9] = (unsigned char)manufacturer;
    block[10] = 0x34;
    block[11] = 0x12;
    block[12] = 0x78;
    block[13] = 0x56;
    block[14] = 0x34;
    block[15] = 0x12;
    block[18] = 1;
    block[19] = 4;
    block[24] = 0x02;

    /* 1600x1200 @ 60Hz: 162MHz, htotal 2160, vtotal 1250. */
    descriptor = block + 54;
    descriptor[0] = 0x48;
    descriptor[1] = 0x3f;
    descriptor[2] = 0x40;
    descriptor[3] = 0x30;
    descriptor[4] = 0x62;
    descriptor[5] = 0xb0;
    descriptor[6] = 0x32;
    descriptor[7] = 0x40;
    finish_checksum(block);
}

static void
test_valid_preferred_mode(void)
{
    unsigned char block[OSMGA_EDID_BLOCK_SIZE];
    OSMGAEDIDInfo info;
    OSMGAMode fixed_modes[2];
    OSMGAMode selected;
    OSMGAModeDecision decision;
    OSMGAEDIDReason reason;

    make_base_edid(block);
    fixed_modes[0].width = 1024;
    fixed_modes[0].height = 768;
    fixed_modes[0].refresh_millihz = 60000UL;
    fixed_modes[1].width = 1600;
    fixed_modes[1].height = 1200;
    fixed_modes[1].refresh_millihz = 59000UL;

    expect(OSMGAParseBaseEDID(block, &info) == 1, "valid-parse");
    expect(same_text(info.manufacturer, "MGA"), "manufacturer");
    expect(info.version == 1 && info.revision == 4, "edid-version");
    expect(info.extension_count == 0, "edid-extension-count");
    expect(same_text(OSMGAEDIDReasonString(info.reason), "none"),
           "valid-reason-text");
    expect(info.product_code == 0x1234, "product-code");
    expect(info.serial_number == 0x12345678UL, "serial-number");
    expect(info.has_preferred_mode, "preferred-present");
    expect(info.preferred_mode.width == 1600 && info.preferred_mode.height == 1200,
           "preferred-resolution");
    expect(info.preferred_mode.refresh_millihz == 60000UL, "preferred-refresh");
    expect(OSMGASelectDisplayMode(&info, 0, fixed_modes, 2, &selected, &decision,
                                  &reason) == 1, "edid-select");
    expect(decision == OSMGA_MODE_DECISION_EDID_PREFERRED, "edid-decision");
    expect(selected.width == 1600 && selected.height == 1200, "edid-selected-mode");
    expect(selected.refresh_millihz == 59000UL, "edid-refresh-tolerance");

    make_base_edid(block);
    block[126] = 1;
    finish_checksum(block);
    expect(OSMGAParseBaseEDID(block, &info) == 1, "extension-count-parse");
    expect(info.extension_count == 1, "extension-count-value");
}

static void
test_manual_override_and_fallbacks(void)
{
    unsigned char block[OSMGA_EDID_BLOCK_SIZE];
    OSMGAEDIDInfo info;
    OSMGAMode fixed_mode;
    OSMGAMode manual_fixed_mode;
    OSMGAMode manual_mode;
    OSMGAMode selected;
    OSMGAMode retained;
    OSMGAModeDecision decision;
    OSMGAEDIDReason reason;

    make_base_edid(block);
    fixed_mode.width = 1024;
    fixed_mode.height = 768;
    fixed_mode.refresh_millihz = 60000UL;
    manual_mode.width = 1280;
    manual_mode.height = 1024;
    manual_mode.refresh_millihz = 60000UL;
    manual_fixed_mode = manual_mode;
    retained.width = 1600;
    retained.height = 1200;
    retained.refresh_millihz = 60000UL;
    selected = retained;
    expect(OSMGASelectDisplayMode(0, 0, &fixed_mode, 1, &selected, &decision,
                                  &reason) == 0, "no-edid-fallback");
    expect(reason == OSMGA_EDID_REASON_NO_EDID, "no-edid-reason");
    expect(same_text(OSMGAEDIDReasonString(reason), "no-edid"),
           "no-edid-reason-text");
    expect(selected.width == retained.width && selected.height == retained.height &&
           selected.refresh_millihz == retained.refresh_millihz,
           "fallback-retains-known-good-mode");
    expect(OSMGAParseBaseEDID(block, &info) == 1, "fallback-parse");
    expect(OSMGASelectDisplayMode(&info, &manual_mode, &manual_fixed_mode, 1, &selected,
                                  &decision, &reason) == 1, "manual-select");
    expect(decision == OSMGA_MODE_DECISION_MANUAL, "manual-decision");
    expect(selected.width == 1280 && selected.height == 1024, "manual-mode");
    expect(OSMGASelectDisplayMode(&info, &manual_mode, &fixed_mode, 1, &selected,
                                  &decision, &reason) == 0,
           "manual-unsupported-fallback");
    expect(reason == OSMGA_EDID_REASON_INVALID_MANUAL_MODE,
           "manual-unsupported-reason");
    expect(OSMGASelectDisplayMode(&info, 0, &fixed_mode, 1, &selected, &decision,
                                  &reason) == 0, "unsupported-fallback");
    expect(reason == OSMGA_EDID_REASON_UNSUPPORTED_PREFERRED,
           "unsupported-reason");

    block[0] = 0x01;
    expect(OSMGAParseBaseEDID(block, &info) == 0, "invalid-header");
    expect(info.reason == OSMGA_EDID_REASON_INVALID_HEADER, "invalid-header-reason");
    expect(same_text(OSMGAEDIDReasonString(info.reason), "invalid-header"),
           "invalid-header-reason-text");

    expect(OSMGASelectDisplayMode(&info, &manual_mode, &manual_fixed_mode, 1, &selected,
                                  &decision, &reason) == 1,
           "manual-overrides-invalid-edid");

    make_base_edid(block);
    block[20] ^= 0x01;
    expect(OSMGAParseBaseEDID(block, &info) == 0, "invalid-checksum");
    expect(info.reason == OSMGA_EDID_REASON_INVALID_CHECKSUM,
           "invalid-checksum-reason");

    make_base_edid(block);
    block[54 + 17] = 0x80;
    finish_checksum(block);
    expect(OSMGAParseBaseEDID(block, &info) == 1, "interlaced-parse");
    expect(OSMGASelectDisplayMode(&info, 0, &fixed_mode, 1, &selected, &decision,
                                  &reason) == 0, "interlaced-fallback");
    expect(reason == OSMGA_EDID_REASON_INTERLACED_PREFERRED,
           "interlaced-reason");

    make_base_edid(block);
    copy_bytes(block + 72, block + 54, 18);
    clear_bytes(block + 54, 18);
    finish_checksum(block);
    expect(OSMGAParseBaseEDID(block, &info) == 1, "no-preferred-parse");
    expect(!info.has_preferred_mode, "no-preferred-absent");
    expect(OSMGASelectDisplayMode(&info, 0, &fixed_mode, 1, &selected, &decision,
                                  &reason) == 0, "no-preferred-fallback");
    expect(reason == OSMGA_EDID_REASON_NO_PREFERRED_TIMING,
           "no-preferred-reason");

    make_base_edid(block);
    block[24] = 0;
    finish_checksum(block);
    expect(OSMGAParseBaseEDID(block, &info) == 1, "no-feature-preferred-parse");
    expect(!info.has_preferred_mode, "no-feature-preferred-absent");
    expect(OSMGASelectDisplayMode(&info, 0, &fixed_mode, 1, &selected, &decision,
                                  &reason) == 0, "no-feature-preferred-fallback");
    expect(reason == OSMGA_EDID_REASON_NO_PREFERRED_TIMING,
           "no-feature-preferred-reason");

    make_base_edid(block);
    block[18] = 2;
    finish_checksum(block);
    expect(OSMGAParseBaseEDID(block, &info) == 0, "unsupported-version");
    expect(info.reason == OSMGA_EDID_REASON_UNSUPPORTED_VERSION,
           "unsupported-version-reason");
    expect(same_text(OSMGAEDIDReasonString(info.reason), "unsupported-version"),
           "unsupported-version-reason-text");

    make_base_edid(block);
    block[8] = 0;
    block[9] = 0;
    finish_checksum(block);
    expect(OSMGAParseBaseEDID(block, &info) == 0, "invalid-manufacturer");
    expect(info.reason == OSMGA_EDID_REASON_INVALID_MANUFACTURER,
           "invalid-manufacturer-reason");
    expect(same_text(OSMGAEDIDReasonString(info.reason), "invalid-manufacturer"),
           "invalid-manufacturer-reason-text");
}

static void
test_manual_display_mode_parser(void)
{
    OSMGAMode mode;

    expect(OSMGAParseManualDisplayMode(
           "Height: 1200 Width: 1600 Refresh: 60Hz ColorSpace: RGB:888/32",
           &mode) == 1, "manual-string-parse");
    expect(mode.width == 1600 && mode.height == 1200,
           "manual-string-resolution");
    expect(mode.refresh_millihz == 60000UL, "manual-string-refresh");
    expect(OSMGAParseManualDisplayMode(
           "\tHeight:\t768 Width:1024 Refresh:75Hz", &mode) == 1,
           "manual-string-whitespace");
    expect(mode.width == 1024 && mode.height == 768 &&
           mode.refresh_millihz == 75000UL, "manual-string-whitespace-value");
    expect(OSMGAParseManualDisplayMode("Width: 1600 Height: 1200 Refresh: 60Hz",
                                       &mode) == 0, "manual-string-order");
    expect(OSMGAParseManualDisplayMode("Height: 1200 Width: 1600 Refresh: 0Hz",
                                       &mode) == 0, "manual-string-zero-refresh");
    expect(OSMGAParseManualDisplayMode("Height: 1200 Width: 1600 Refresh: 60",
                                       &mode) == 0, "manual-string-unit");
    expect(OSMGAParseManualDisplayMode(
           "Height: 1200 Width: 1600 Refresh: 60Hz unexpected", &mode) == 0,
           "manual-string-trailing-text");
    expect(OSMGAParseManualDisplayMode(
           "Height: 1200 Width: 1600 Refresh: 60Hz ColorSpace:", &mode) == 0,
           "manual-string-empty-colorspace");
    expect(OSMGAParseManualDisplayMode(
           "Height: 70000 Width: 1600 Refresh: 60Hz", &mode) == 0,
           "manual-string-height-range");
    expect(OSMGAParseManualDisplayMode(
           "Height: 1200 Width: 4294967296 Refresh: 60Hz", &mode) == 0,
           "manual-string-number-overflow");
    expect(OSMGAParseManualDisplayMode(
           "Height: 1200 Width: 1600 Refresh: 4294968Hz", &mode) == 0,
           "manual-string-refresh-overflow");
}

static void
test_api_boundaries(void)
{
    OSMGAEDIDInfo info;
    OSMGAMode mode;
    OSMGAModeDecision decision;
    OSMGAEDIDReason reason;

    expect(OSMGAParseBaseEDID(0, &info) == 0, "null-edid");
    expect(info.reason == OSMGA_EDID_REASON_NO_EDID, "null-edid-reason");
    expect(OSMGAParseBaseEDID(0, 0) == 0, "null-edid-info");
    expect(OSMGAParseManualDisplayMode(0, &mode) == 0, "null-manual-text");
    expect(OSMGAParseManualDisplayMode("Height: 1 Width: 1 Refresh: 1Hz", 0) == 0,
           "null-manual-mode");
    expect(OSMGASelectDisplayMode(0, 0, 0, 0, 0, &decision, &reason) == 0,
           "null-selected-mode");
    expect(same_text(OSMGAEDIDReasonString((OSMGAEDIDReason)999), "unknown"),
           "unknown-reason-text");
}

static void
test_linear_memory_footprint(void)
{
    OSMGAMode mode;
    unsigned long required;
    OSMGAModeMemoryReason reason;

    mode.width = 1600;
    mode.height = 1200;
    mode.refresh_millihz = 60000UL;
    required = 0;
    expect(OSMGAModeFitsLinearMemory(&mode, 32, 6400UL, 16UL * 1024UL * 1024UL,
                                     &required, &reason) == 1,
           "linear-memory-1600x1200x32");
    expect(required == 7680000UL && reason == OSMGA_MODE_MEMORY_OK,
           "linear-memory-required-bytes");

    required = 123UL;
    expect(OSMGAModeFitsLinearMemory(&mode, 32, 6399UL, 16UL * 1024UL * 1024UL,
                                     &required, &reason) == 0,
           "linear-memory-pitch-small");
    expect(reason == OSMGA_MODE_MEMORY_PITCH_TOO_SMALL && required == 123UL,
           "linear-memory-pitch-output-retained");

    expect(OSMGAModeFitsLinearMemory(&mode, 32, 6400UL, 7679999UL,
                                     &required, &reason) == 0,
           "linear-memory-insufficient");
    expect(reason == OSMGA_MODE_MEMORY_INSUFFICIENT,
           "linear-memory-insufficient-reason");

    expect(OSMGAModeFitsLinearMemory(&mode, 15, 6400UL, 16UL * 1024UL * 1024UL,
                                     &required, &reason) == 0,
           "linear-memory-format");
    expect(reason == OSMGA_MODE_MEMORY_UNSUPPORTED_FORMAT,
           "linear-memory-format-reason");

    mode.width = 65535;
    mode.height = 65535;
    expect(OSMGAModeFitsLinearMemory(&mode, 32, 262140UL, 4294967295UL,
                                     &required, &reason) == 0,
           "linear-memory-overflow");
    expect(reason == OSMGA_MODE_MEMORY_OVERFLOW,
           "linear-memory-overflow-reason");
    expect(OSMGAModeFitsLinearMemory(0, 32, 6400UL, 1UL, &required, &reason) == 0,
           "linear-memory-null-mode");
    expect(reason == OSMGA_MODE_MEMORY_INVALID_ARGUMENT,
           "linear-memory-null-mode-reason");
    expect(OSMGAModeFitsLinearMemory(&mode, 32, 262140UL, 1UL, 0, &reason) == 0,
           "linear-memory-null-required");
    expect(OSMGAModeFitsLinearMemory(&mode, 32, 262140UL, 1UL, &required, 0) == 0,
           "linear-memory-null-reason");
}

static void
test_p3_admission_policy(void)
{
    OSMGAP3Admission admission;
    OSMGAP3Gate gate;

    clear_bytes((unsigned char *)&admission, sizeof(admission));
    gate = OSMGA_P3_GATE_READY;
    expect(OSMGACanEnterP3(0, &gate) == 0, "p3-admission-null");
    expect(gate == OSMGA_P3_GATE_PCI_INVENTORY, "p3-admission-null-gate");
    expect(OSMGACanEnterP3(&admission, 0) == 0, "p3-admission-null-output");

    admission.pci_inventory_verified = 1;
    expect(OSMGACanEnterP3(&admission, &gate) == 0, "p3-admission-vram-size");
    expect(gate == OSMGA_P3_GATE_VRAM_SIZE, "p3-admission-vram-size-gate");
    admission.physical_vram_size_verified = 1;
    expect(OSMGACanEnterP3(&admission, &gate) == 0, "p3-admission-vram-type");
    expect(gate == OSMGA_P3_GATE_VRAM_TYPE, "p3-admission-vram-type-gate");
    admission.physical_vram_type_verified = 1;
    expect(OSMGACanEnterP3(&admission, &gate) == 0,
           "p3-admission-offscreen-range");
    expect(gate == OSMGA_P3_GATE_OFFSCREEN_RANGE,
           "p3-admission-offscreen-range-gate");
    admission.existing_owner_offscreen_range_verified = 1;
    expect(OSMGACanEnterP3(&admission, &gate) == 0,
           "p3-admission-mapping-compatibility");
    expect(gate == OSMGA_P3_GATE_MAPPING_COMPATIBILITY,
           "p3-admission-mapping-compatibility-gate");
    admission.mapping_compatibility_verified = 1;
    expect(OSMGACanEnterP3(&admission, &gate) == 1, "p3-admission-ready");
    expect(gate == OSMGA_P3_GATE_READY, "p3-admission-ready-gate");
}

int
main(void)
{
#ifdef OSMGA_TARGET_NETNAME_BOOTSTRAP
    bootstrap_netname_lookup();
#endif
    failures = 0;
    test_valid_preferred_mode();
    test_manual_override_and_fallbacks();
    test_manual_display_mode_parser();
    test_api_boundaries();
    test_linear_memory_footprint();
    test_p3_admission_policy();
    if (failures != 0) {
        printf("OPENSTEP_MGA_EDID_TEST_STATUS=fail count=%d\n", failures);
        return 1;
    }
    printf("OPENSTEP_MGA_EDID_TEST_STATUS=pass\n");
    return 0;
}
