#include "OpenStepMGAReference.h"

static int
valid_geometry(unsigned int width, unsigned int height, unsigned int stride_pixels)
{
    if (width == 0 || height == 0 || stride_pixels < width) {
        return 0;
    }
    return (unsigned long)(height - 1U) <=
           OSMGA_REFERENCE_U32_MASK / (unsigned long)stride_pixels;
}

int
OSMGAReferenceClear32(unsigned long *pixels, unsigned int width,
                      unsigned int height, unsigned int stride_pixels,
                      unsigned long pixel)
{
    unsigned int row;
    unsigned int column;
    unsigned long offset;

    if (pixels == 0 || !valid_geometry(width, height, stride_pixels)) {
        return 0;
    }
    pixel &= OSMGA_REFERENCE_U32_MASK;
    for (row = 0; row < height; row++) {
        offset = (unsigned long)row * (unsigned long)stride_pixels;
        for (column = 0; column < width; column++) {
            pixels[offset + column] = pixel;
        }
    }
    return 1;
}

static int
valid_rectangle(unsigned int surface_width, unsigned int surface_height,
                unsigned int x, unsigned int y, unsigned int width,
                unsigned int height)
{
    return width != 0 && height != 0 && x < surface_width && y < surface_height &&
           width <= surface_width - x && height <= surface_height - y;
}

int
OSMGAReferenceClearRect32(unsigned long *pixels,
                          unsigned int surface_width,
                          unsigned int surface_height,
                          unsigned int stride_pixels,
                          unsigned int x, unsigned int y,
                          unsigned int width, unsigned int height,
                          unsigned long pixel)
{
    unsigned int row;
    unsigned int column;
    unsigned long offset;

    if (pixels == 0 || !valid_geometry(surface_width, surface_height,
                                       stride_pixels) ||
        !valid_rectangle(surface_width, surface_height, x, y, width, height)) {
        return 0;
    }
    pixel &= OSMGA_REFERENCE_U32_MASK;
    for (row = 0; row < height; row++) {
        offset = ((unsigned long)y + (unsigned long)row) *
                 (unsigned long)stride_pixels + (unsigned long)x;
        for (column = 0; column < width; column++) {
            pixels[offset + column] = pixel;
        }
    }
    return 1;
}

int
OSMGAReferenceCopyRect32(const unsigned long *source,
                         unsigned int source_width, unsigned int source_height,
                         unsigned int source_stride_pixels,
                         unsigned int source_x, unsigned int source_y,
                         unsigned long *destination,
                         unsigned int destination_width,
                         unsigned int destination_height,
                         unsigned int destination_stride_pixels,
                         unsigned int destination_x,
                         unsigned int destination_y,
                         unsigned int width, unsigned int height)
{
    unsigned int row;
    unsigned int column;
    unsigned long source_offset;
    unsigned long destination_offset;

    if (source == 0 || destination == 0 || source == destination ||
        !valid_geometry(source_width, source_height, source_stride_pixels) ||
        !valid_geometry(destination_width, destination_height,
                        destination_stride_pixels) ||
        !valid_rectangle(source_width, source_height, source_x, source_y,
                         width, height) ||
        !valid_rectangle(destination_width, destination_height,
                         destination_x, destination_y, width, height)) {
        return 0;
    }
    for (row = 0; row < height; row++) {
        source_offset = ((unsigned long)source_y + (unsigned long)row) *
                        (unsigned long)source_stride_pixels +
                        (unsigned long)source_x;
        destination_offset = ((unsigned long)destination_y + (unsigned long)row) *
                             (unsigned long)destination_stride_pixels +
                             (unsigned long)destination_x;
        for (column = 0; column < width; column++) {
            destination[destination_offset + column] =
                source[source_offset + column] & OSMGA_REFERENCE_U32_MASK;
        }
    }
    return 1;
}

int
OSMGAReferenceScaleNearest32(const unsigned long *source,
                             unsigned int source_width,
                             unsigned int source_height,
                             unsigned int source_stride_pixels,
                             unsigned long *destination,
                             unsigned int destination_width,
                             unsigned int destination_height,
                             unsigned int destination_stride_pixels)
{
    unsigned int row;
    unsigned int column;
    unsigned long source_row;
    unsigned long source_column;
    unsigned long source_offset;
    unsigned long destination_offset;

    if (source == 0 || destination == 0 || source == destination ||
        !valid_geometry(source_width, source_height, source_stride_pixels) ||
        !valid_geometry(destination_width, destination_height,
                        destination_stride_pixels) ||
        (destination_width > 1U &&
         (unsigned long)source_width > OSMGA_REFERENCE_U32_MASK /
                                       (unsigned long)(destination_width - 1U)) ||
        (destination_height > 1U &&
         (unsigned long)source_height > OSMGA_REFERENCE_U32_MASK /
                                        (unsigned long)(destination_height - 1U))) {
        return 0;
    }
    for (row = 0; row < destination_height; row++) {
        source_row = (unsigned long)row * (unsigned long)source_height /
                     (unsigned long)destination_height;
        destination_offset = (unsigned long)row *
                             (unsigned long)destination_stride_pixels;
        source_offset = source_row * (unsigned long)source_stride_pixels;
        for (column = 0; column < destination_width; column++) {
            source_column = (unsigned long)column *
                            (unsigned long)source_width /
                            (unsigned long)destination_width;
            destination[destination_offset + (unsigned long)column] =
                source[source_offset + source_column] & OSMGA_REFERENCE_U32_MASK;
        }
    }
    return 1;
}

static unsigned long
fnv_byte(unsigned long hash, unsigned long value)
{
    hash ^= value & 0xffUL;
    return (hash * 16777619UL) & OSMGA_REFERENCE_U32_MASK;
}

int
OSMGAReferenceChecksum32(const unsigned long *pixels, unsigned int width,
                         unsigned int height, unsigned int stride_pixels,
                         unsigned long *checksum)
{
    unsigned int row;
    unsigned int column;
    unsigned long offset;
    unsigned long pixel;
    unsigned long hash;

    if (pixels == 0 || checksum == 0 ||
        !valid_geometry(width, height, stride_pixels)) {
        return 0;
    }
    hash = 2166136261UL;
    for (row = 0; row < height; row++) {
        offset = (unsigned long)row * (unsigned long)stride_pixels;
        for (column = 0; column < width; column++) {
            pixel = pixels[offset + column] & OSMGA_REFERENCE_U32_MASK;
            hash = fnv_byte(hash, pixel);
            hash = fnv_byte(hash, pixel >> 8);
            hash = fnv_byte(hash, pixel >> 16);
            hash = fnv_byte(hash, pixel >> 24);
        }
    }
    *checksum = hash;
    return 1;
}

int
OSMGAReferenceCompare32(const unsigned long *expected,
                        const unsigned long *actual,
                        unsigned int width, unsigned int height,
                        unsigned int stride_pixels,
                        unsigned long *mismatch_index)
{
    unsigned int row;
    unsigned int column;
    unsigned long offset;
    unsigned long index;

    if (expected == 0 || actual == 0 || mismatch_index == 0 ||
        !valid_geometry(width, height, stride_pixels)) {
        return 0;
    }
    index = 0;
    for (row = 0; row < height; row++) {
        offset = (unsigned long)row * (unsigned long)stride_pixels;
        for (column = 0; column < width; column++) {
            if ((expected[offset + column] & OSMGA_REFERENCE_U32_MASK) !=
                (actual[offset + column] & OSMGA_REFERENCE_U32_MASK)) {
                *mismatch_index = index;
                return 0;
            }
            index++;
        }
    }
    *mismatch_index = index;
    return 1;
}

static long
edge_value(long ax, long ay, long bx, long by, long px, long py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static int
valid_coordinate(int value)
{
    return value >= -OSMGA_REFERENCE_COORDINATE_LIMIT &&
           value <= OSMGA_REFERENCE_COORDINATE_LIMIT;
}

static void
swap_point(int *first_x, int *first_y, int *second_x, int *second_y)
{
    int value;

    value = *first_x;
    *first_x = *second_x;
    *second_x = value;
    value = *first_y;
    *first_y = *second_y;
    *second_y = value;
}

int
OSMGAReferenceFillTriangle32(unsigned long *pixels, unsigned int width,
                             unsigned int height, unsigned int stride_pixels,
                             int x0, int y0, int x1, int y1, int x2, int y2,
                             unsigned long pixel)
{
    int left;
    int top;
    int right;
    int bottom;
    int x;
    int y;
    long area;
    long e0;
    long e1;
    long e2;
    long offset;
    long ax0;
    long ay0;
    long ax1;
    long ay1;
    long ax2;
    long ay2;
    long sample_x;
    long sample_y;

    if (pixels == 0 || !valid_geometry(width, height, stride_pixels) ||
        width > OSMGA_REFERENCE_COORDINATE_LIMIT ||
        height > OSMGA_REFERENCE_COORDINATE_LIMIT ||
        !valid_coordinate(x0) || !valid_coordinate(y0) ||
        !valid_coordinate(x1) || !valid_coordinate(y1) ||
        !valid_coordinate(x2) || !valid_coordinate(y2)) {
        return 0;
    }
    area = edge_value((long)x0, (long)y0, (long)x1, (long)y1,
                      (long)x2, (long)y2);
    if (area == 0) {
        return 0;
    }
    if (area < 0) {
        swap_point(&x1, &y1, &x2, &y2);
    }
    left = x0;
    if (x1 < left) left = x1;
    if (x2 < left) left = x2;
    top = y0;
    if (y1 < top) top = y1;
    if (y2 < top) top = y2;
    right = x0;
    if (x1 > right) right = x1;
    if (x2 > right) right = x2;
    bottom = y0;
    if (y1 > bottom) bottom = y1;
    if (y2 > bottom) bottom = y2;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right >= (int)width) right = (int)width - 1;
    if (bottom >= (int)height) bottom = (int)height - 1;
    if (left > right || top > bottom) {
        return 1;
    }

    ax0 = (long)x0 * 2L;
    ay0 = (long)y0 * 2L;
    ax1 = (long)x1 * 2L;
    ay1 = (long)y1 * 2L;
    ax2 = (long)x2 * 2L;
    ay2 = (long)y2 * 2L;
    pixel &= OSMGA_REFERENCE_U32_MASK;
    for (y = top; y <= bottom; y++) {
        sample_y = (long)y * 2L + 1L;
        offset = (long)y * (long)stride_pixels;
        for (x = left; x <= right; x++) {
            sample_x = (long)x * 2L + 1L;
            e0 = edge_value(ax0, ay0, ax1, ay1, sample_x, sample_y);
            e1 = edge_value(ax1, ay1, ax2, ay2, sample_x, sample_y);
            e2 = edge_value(ax2, ay2, ax0, ay0, sample_x, sample_y);
            if (e0 >= 0 && e1 >= 0 && e2 >= 0) {
                pixels[offset + x] = pixel;
            }
        }
    }
    return 1;
}

int
OSMGAReferenceDepthTestWrite16(unsigned long *pixels, unsigned short *depth,
                               unsigned int width, unsigned int height,
                               unsigned int stride_pixels, int x, int y,
                               unsigned short incoming_depth,
                               unsigned long pixel,
                               OSMGAReferenceDepthFunction function,
                               int write_depth, int *passed)
{
    unsigned long offset;
    unsigned short stored_depth;
    int result;

    if (pixels == 0 || depth == 0 || passed == 0 ||
        !valid_geometry(width, height, stride_pixels) || x < 0 || y < 0 ||
        x >= (int)width || y >= (int)height ||
        function < OSMGA_REFERENCE_DEPTH_NEVER ||
        function > OSMGA_REFERENCE_DEPTH_ALWAYS) {
        return 0;
    }
    offset = (unsigned long)y * (unsigned long)stride_pixels + (unsigned long)x;
    stored_depth = depth[offset];
    result = 0;
    switch (function) {
    case OSMGA_REFERENCE_DEPTH_NEVER:
        break;
    case OSMGA_REFERENCE_DEPTH_LESS:
        result = incoming_depth < stored_depth;
        break;
    case OSMGA_REFERENCE_DEPTH_LEQUAL:
        result = incoming_depth <= stored_depth;
        break;
    case OSMGA_REFERENCE_DEPTH_ALWAYS:
        result = 1;
        break;
    }
    *passed = result;
    if (result) {
        pixels[offset] = pixel & OSMGA_REFERENCE_U32_MASK;
        if (write_depth) {
            depth[offset] = incoming_depth;
        }
    }
    return 1;
}

static unsigned long
blend_channel(unsigned long source, unsigned long destination,
              unsigned long source_alpha)
{
    return (source * source_alpha +
            destination * (255UL - source_alpha) + 127UL) / 255UL;
}

unsigned long
OSMGAReferenceBlendSrcAlpha32(unsigned long source, unsigned long destination)
{
    unsigned long source_alpha;
    unsigned long destination_alpha;
    unsigned long output_alpha;
    unsigned long red;
    unsigned long green;
    unsigned long blue;

    source &= OSMGA_REFERENCE_U32_MASK;
    destination &= OSMGA_REFERENCE_U32_MASK;
    source_alpha = (source >> 24) & 0xffUL;
    destination_alpha = (destination >> 24) & 0xffUL;
    output_alpha = source_alpha +
                   (destination_alpha * (255UL - source_alpha) + 127UL) / 255UL;
    red = blend_channel((source >> 16) & 0xffUL,
                        (destination >> 16) & 0xffUL, source_alpha);
    green = blend_channel((source >> 8) & 0xffUL,
                          (destination >> 8) & 0xffUL, source_alpha);
    blue = blend_channel(source & 0xffUL, destination & 0xffUL, source_alpha);
    return ((output_alpha & 0xffUL) << 24) | ((red & 0xffUL) << 16) |
           ((green & 0xffUL) << 8) | (blue & 0xffUL);
}

int
OSMGAReferenceSampleTextureNearestClamp32(const unsigned long *texels,
                                          unsigned int width, unsigned int height,
                                          unsigned long u_16_16,
                                          unsigned long v_16_16,
                                          unsigned long *pixel)
{
    unsigned long u;
    unsigned long v;
    unsigned long offset;

    if (texels == 0 || pixel == 0 || width == 0 || height == 0 ||
        (unsigned long)height > OSMGA_REFERENCE_U32_MASK / (unsigned long)width) {
        return 0;
    }
    u = u_16_16 >> 16;
    v = v_16_16 >> 16;
    if (u >= (unsigned long)width) {
        u = (unsigned long)width - 1UL;
    }
    if (v >= (unsigned long)height) {
        v = (unsigned long)height - 1UL;
    }
    offset = v * (unsigned long)width + u;
    *pixel = texels[offset] & OSMGA_REFERENCE_U32_MASK;
    return 1;
}
