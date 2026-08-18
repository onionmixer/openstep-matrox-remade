#include <stdio.h>

#include "OpenStepMGAReference.h"

static int failures;

static void
expect(int condition, const char *label)
{
    if (!condition) {
        fprintf(stderr, "OPENSTEP_MGA_REFERENCE_TEST_FAIL=%s\n", label);
        failures++;
    }
}

int
main(void)
{
    unsigned long expected[18];
    unsigned long actual[18];
    unsigned long checksum;
    unsigned long mismatch;
    unsigned short depth[18];
    unsigned long scale_source[6];
    unsigned long scale_destination[20];
    int passed;
    unsigned long texels[4];
    unsigned int index;

    for (index = 0; index < 18; index++) {
        expected[index] = 0xdeadbeefUL;
        actual[index] = 0xdeadbeefUL;
    }
    failures = 0;
    expect(OSMGAReferenceClear32(expected, 4, 3, 6, 0x11223344UL) == 1,
           "clear-valid");
    expect(expected[0] == 0x11223344UL && expected[3] == 0x11223344UL &&
           expected[6] == 0x11223344UL && expected[15] == 0x11223344UL,
           "clear-active-pixels");
    expect(expected[4] == 0xdeadbeefUL && expected[5] == 0xdeadbeefUL &&
           expected[10] == 0xdeadbeefUL && expected[11] == 0xdeadbeefUL,
           "clear-preserves-padding");
    expect(OSMGAReferenceClearRect32(actual, 4, 3, 6, 1, 1, 2, 1,
                                     0x55667788UL) == 1,
           "clear-rect-valid");
    expect(actual[7] == 0x55667788UL && actual[8] == 0x55667788UL &&
           actual[6] == 0xdeadbeefUL && actual[9] == 0xdeadbeefUL,
           "clear-rect-bounded");
    expect(OSMGAReferenceClearRect32(actual, 4, 3, 6, 3, 0, 2, 1, 0) == 0,
           "clear-rect-out-of-range");
    expect(OSMGAReferenceChecksum32(expected, 4, 3, 6, &checksum) == 1,
           "checksum-valid");
    expect(checksum == 0xb32af285UL, "checksum-known-value");

    expect(OSMGAReferenceClear32(actual, 4, 3, 6, 0x11223344UL) == 1,
           "compare-clear");
    expect(OSMGAReferenceCopyRect32(expected, 4, 3, 6, 1, 1,
                                    actual, 4, 3, 6, 1, 1, 2, 1) == 1,
           "copy-rect-valid");
    expect(actual[7] == 0x11223344UL && actual[8] == 0x11223344UL,
           "copy-rect-values");
    expect(OSMGAReferenceCopyRect32(expected, 4, 3, 6, 0, 0,
                                    expected, 4, 3, 6, 0, 0, 1, 1) == 0,
           "copy-rect-self-rejected");

    scale_source[0] = 0x01010101UL;
    scale_source[1] = 0x02020202UL;
    scale_source[2] = 0xdeadbeefUL;
    scale_source[3] = 0x03030303UL;
    scale_source[4] = 0x04040404UL;
    scale_source[5] = 0xdeadbeefUL;
    for (index = 0; index < 20; index++) {
        scale_destination[index] = 0xdeadbeefUL;
    }
    expect(OSMGAReferenceScaleNearest32(scale_source, 2, 2, 3,
                                        scale_destination, 4, 4, 5) == 1,
           "scale-nearest-valid");
    expect(scale_destination[0] == 0x01010101UL &&
           scale_destination[1] == 0x01010101UL &&
           scale_destination[2] == 0x02020202UL &&
           scale_destination[3] == 0x02020202UL &&
           scale_destination[10] == 0x03030303UL &&
           scale_destination[13] == 0x04040404UL,
           "scale-nearest-values");
    expect(scale_destination[4] == 0xdeadbeefUL &&
           scale_destination[9] == 0xdeadbeefUL &&
           scale_destination[14] == 0xdeadbeefUL &&
           scale_destination[19] == 0xdeadbeefUL,
           "scale-nearest-padding-preserved");
    expect(OSMGAReferenceScaleNearest32(scale_source, 2, 2, 3,
                                        scale_source, 2, 2, 3) == 0,
           "scale-nearest-self-rejected");
    expect(OSMGAReferenceScaleNearest32(scale_source, 65535, 1, 65535,
                                        scale_destination, 70000, 1, 70000) == 0,
           "scale-nearest-multiply-overflow-rejected");
    expect(OSMGAReferenceCompare32(expected, actual, 4, 3, 6, &mismatch) == 1,
           "compare-match");
    expect(mismatch == 12UL, "compare-match-count");
    actual[13] = 0x11223345UL;
    expect(OSMGAReferenceCompare32(expected, actual, 4, 3, 6, &mismatch) == 0,
           "compare-difference");
    expect(mismatch == 9UL, "compare-difference-index");

    expect(OSMGAReferenceClear32(0, 4, 3, 6, 0) == 0, "clear-null");
    expect(OSMGAReferenceClear32(expected, 4, 3, 3, 0) == 0,
           "clear-small-stride");
    expect(OSMGAReferenceChecksum32(expected, 4, 3, 6, 0) == 0,
           "checksum-null-output");
    expect(OSMGAReferenceCompare32(expected, actual, 4, 3, 6, 0) == 0,
           "compare-null-output");
    expect(OSMGAReferenceClear32(expected, 1, 65535, 70000, 0) == 0,
           "clear-32bit-offset-overflow");

    expect(OSMGAReferenceClear32(expected, 4, 3, 6, 0) == 1,
           "triangle-clear");
    expect(OSMGAReferenceFillTriangle32(expected, 4, 3, 6,
                                        0, 0, 4, 0, 0, 3,
                                        0xaabbccddUL) == 1,
           "triangle-valid");
    expect(expected[0] == 0xaabbccddUL && expected[1] == 0xaabbccddUL &&
           expected[2] == 0xaabbccddUL && expected[6] == 0xaabbccddUL &&
           expected[7] == 0xaabbccddUL && expected[12] == 0xaabbccddUL,
           "triangle-covered-pixels");
    expect(expected[3] == 0 && expected[8] == 0 && expected[13] == 0 &&
           expected[4] == 0xdeadbeefUL && expected[5] == 0xdeadbeefUL,
           "triangle-uncovered-and-padding");
    expect(OSMGAReferenceClear32(actual, 4, 3, 6, 0) == 1,
           "triangle-reverse-clear");
    expect(OSMGAReferenceFillTriangle32(actual, 4, 3, 6,
                                        0, 0, 0, 3, 4, 0,
                                        0xaabbccddUL) == 1,
           "triangle-reverse-winding");
    expect(OSMGAReferenceCompare32(expected, actual, 4, 3, 6, &mismatch) == 1,
           "triangle-reverse-match");
    expect(OSMGAReferenceFillTriangle32(actual, 4, 3, 6,
                                        0, 0, 1, 1, 2, 2, 1) == 0,
           "triangle-degenerate");
    expect(OSMGAReferenceFillTriangle32(actual, 4096, 3, 4096,
                                        0, 0, 1, 0, 0, 1, 1) == 0,
           "triangle-dimension-limit");
    expect(OSMGAReferenceFillTriangle32(actual, 4, 3, 6,
                                        4096, 0, 1, 0, 0, 1, 1) == 0,
           "triangle-coordinate-limit");

    for (index = 0; index < 18; index++) {
        actual[index] = 0x01020304UL;
        depth[index] = 100;
    }
    passed = -1;
    expect(OSMGAReferenceDepthTestWrite16(actual, depth, 4, 3, 6,
                                          1, 1, 50, 0xa0b0c0d0UL,
                                          OSMGA_REFERENCE_DEPTH_LESS, 1,
                                          &passed) == 1,
           "depth-less-valid");
    expect(passed == 1 && actual[7] == 0xa0b0c0d0UL && depth[7] == 50,
           "depth-less-write");
    expect(OSMGAReferenceDepthTestWrite16(actual, depth, 4, 3, 6,
                                          1, 1, 120, 0xffffffffUL,
                                          OSMGA_REFERENCE_DEPTH_LESS, 1,
                                          &passed) == 1,
           "depth-less-reject-valid");
    expect(passed == 0 && actual[7] == 0xa0b0c0d0UL && depth[7] == 50,
           "depth-less-reject-preserves");
    expect(OSMGAReferenceDepthTestWrite16(actual, depth, 4, 3, 6,
                                          1, 1, 50, 0x33445566UL,
                                          OSMGA_REFERENCE_DEPTH_LEQUAL, 0,
                                          &passed) == 1,
           "depth-lequal-valid");
    expect(passed == 1 && actual[7] == 0x33445566UL && depth[7] == 50,
           "depth-lequal-no-write");
    expect(OSMGAReferenceDepthTestWrite16(actual, depth, 4, 3, 6,
                                          1, 1, 65535, 0x778899aaUL,
                                          OSMGA_REFERENCE_DEPTH_ALWAYS, 1,
                                          &passed) == 1,
           "depth-always-valid");
    expect(passed == 1 && actual[7] == 0x778899aaUL && depth[7] == 65535,
           "depth-always-write");
    expect(OSMGAReferenceDepthTestWrite16(actual, depth, 4, 3, 6,
                                          -1, 1, 0, 0,
                                          OSMGA_REFERENCE_DEPTH_LESS, 1,
                                          &passed) == 0,
           "depth-negative-coordinate");
    expect(OSMGAReferenceDepthTestWrite16(actual, depth, 4, 3, 6,
                                          1, 1, 0, 0,
                                          (OSMGAReferenceDepthFunction)99, 1,
                                          &passed) == 0,
           "depth-invalid-function");
    expect(OSMGAReferenceDepthTestWrite16(actual, depth, 4, 3, 6,
                                          1, 1, 0, 0,
                                          OSMGA_REFERENCE_DEPTH_LESS, 1,
                                          0) == 0,
           "depth-null-passed");

    expect(OSMGAReferenceBlendSrcAlpha32(0x80ff0000UL, 0x400000ffUL) ==
           0xa080007fUL, "blend-half-red-over-blue");
    expect(OSMGAReferenceBlendSrcAlpha32(0x00010203UL, 0x7f112233UL) ==
           0x7f112233UL, "blend-transparent-source");
    expect(OSMGAReferenceBlendSrcAlpha32(0xffa1b2c3UL, 0x11223344UL) ==
           0xffa1b2c3UL, "blend-opaque-source");

    texels[0] = 0xff010203UL;
    texels[1] = 0xff111213UL;
    texels[2] = 0xff212223UL;
    texels[3] = 0xff313233UL;
    expect(OSMGAReferenceSampleTextureNearestClamp32(texels, 2, 2,
                                                      0, 0, &mismatch) == 1,
           "texture-origin-valid");
    expect(mismatch == 0xff010203UL, "texture-origin-value");
    expect(OSMGAReferenceSampleTextureNearestClamp32(texels, 2, 2,
                                                      0x0001f000UL, 0,
                                                      &mismatch) == 1,
           "texture-nearest-floor-valid");
    expect(mismatch == 0xff111213UL, "texture-nearest-floor-value");
    expect(OSMGAReferenceSampleTextureNearestClamp32(texels, 2, 2,
                                                      0x00020000UL,
                                                      0x00030000UL,
                                                      &mismatch) == 1,
           "texture-clamp-valid");
    expect(mismatch == 0xff313233UL, "texture-clamp-value");
    expect(OSMGAReferenceSampleTextureNearestClamp32(0, 2, 2, 0, 0,
                                                      &mismatch) == 0,
           "texture-null-input");
    expect(OSMGAReferenceSampleTextureNearestClamp32(texels, 0, 2, 0, 0,
                                                      &mismatch) == 0,
           "texture-zero-width");
    expect(OSMGAReferenceSampleTextureNearestClamp32(texels, 2, 2, 0, 0,
                                                      0) == 0,
           "texture-null-output");

    if (failures != 0) {
        printf("OPENSTEP_MGA_REFERENCE_TEST_STATUS=fail count=%d\n", failures);
        return 1;
    }
    printf("OPENSTEP_MGA_REFERENCE_TEST_STATUS=pass\n");
    return 0;
}
