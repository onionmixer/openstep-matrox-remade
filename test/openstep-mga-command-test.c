#include <stdio.h>

#include "OpenStepMGACommand.h"

static int failures;

static void
expect(int condition, const char *label)
{
    if (!condition) {
        fprintf(stderr, "OPENSTEP_MGA_COMMAND_TEST_FAIL=%s\n", label);
        failures++;
    }
}

int
main(void)
{
    OSMGASurfaceGeometry surface;
    OSMGACommandValidation validation;
    unsigned long required;

    failures = 0;
    surface.width = 1600;
    surface.height = 1200;
    surface.stride_pixels = 1600;
    surface.allocation_bytes = 7680000UL;
    required = 0;
    expect(OSMGAValidateSurface32(&surface, &required, &validation) == 1,
           "surface-valid");
    expect(required == 7680000UL && validation == OSMGA_COMMAND_VALID,
           "surface-required-size");
    expect(OSMGAValidateClear32(&surface, 0, 0, 1600, 1200, &validation) == 1,
           "clear-full-surface");
    expect(OSMGAValidateClear32(&surface, 1599, 1199, 1, 1, &validation) == 1,
           "clear-last-pixel");
    expect(OSMGAValidateClear32(&surface, 1599, 0, 2, 1, &validation) == 0,
           "clear-over-width");
    expect(validation == OSMGA_COMMAND_RECT_OUT_OF_RANGE,
           "clear-over-width-reason");
    expect(OSMGAValidateTriangle32(&surface, 0, 0, 1599, 0, 0, 1199,
                                   &validation) == 1,
           "triangle-valid");
    expect(OSMGAValidateTriangle32(&surface, 0, 0, 1, 1, 2, 2,
                                   &validation) == 0,
           "triangle-degenerate");
    expect(validation == OSMGA_COMMAND_TRIANGLE_DEGENERATE,
           "triangle-degenerate-reason");
    expect(OSMGAValidateTriangle32(&surface, -1, 0, 1, 0, 0, 1,
                                   &validation) == 0,
           "triangle-out-of-range");
    expect(validation == OSMGA_COMMAND_TRIANGLE_OUT_OF_RANGE,
           "triangle-out-of-range-reason");

    surface.allocation_bytes = 7679999UL;
    expect(OSMGAValidateSurface32(&surface, &required, &validation) == 0,
           "surface-insufficient");
    expect(validation == OSMGA_COMMAND_SURFACE_TOO_SMALL,
           "surface-insufficient-reason");
    surface.stride_pixels = 1599;
    expect(OSMGAValidateSurface32(&surface, &required, &validation) == 0,
           "surface-small-stride");
    expect(validation == OSMGA_COMMAND_INVALID_SURFACE,
           "surface-small-stride-reason");
    expect(OSMGAValidateSurface32(0, &required, &validation) == 0,
           "surface-null-input");
    expect(OSMGAValidateSurface32(&surface, 0, &validation) == 0,
           "surface-null-required");
    expect(OSMGAValidateClear32(&surface, 0, 0, 1, 1, 0) == 0,
           "clear-null-validation");

    if (failures != 0) {
        printf("OPENSTEP_MGA_COMMAND_TEST_STATUS=fail count=%d\n", failures);
        return 1;
    }
    printf("OPENSTEP_MGA_COMMAND_TEST_STATUS=pass\n");
    return 0;
}
