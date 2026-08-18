/* Pure-C render-memory budget arithmetic; no target-system interfaces. */

#include <limits.h>

#include "OpenStepMGARenderBudget.h"

static int
is_surface_format(unsigned int bits_per_pixel)
{
    return bits_per_pixel == 8U || bits_per_pixel == 16U ||
           bits_per_pixel == 24U || bits_per_pixel == 32U;
}

static int
add_checked(unsigned long left, unsigned long right, unsigned long *result)
{
    if (left > ULONG_MAX - right) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int
multiply_checked(unsigned long left, unsigned long right,
                 unsigned long *result)
{
    if (left != 0 && right > ULONG_MAX / left) {
        return 0;
    }
    *result = left * right;
    return 1;
}

static int
surface_bytes(unsigned short width, unsigned short height,
              unsigned int bits_per_pixel, unsigned long alignment,
              unsigned long *pitch, unsigned long *bytes)
{
    unsigned long raw_pitch;
    unsigned long rounded_input;
    unsigned long pixel_bytes;

    pixel_bytes = (unsigned long)(bits_per_pixel / 8U);
    if (!multiply_checked((unsigned long)width, pixel_bytes, &raw_pitch)) {
        return 0;
    }
    if (raw_pitch > ULONG_MAX - (alignment - 1UL)) {
        return 0;
    }
    rounded_input = raw_pitch + alignment - 1UL;
    *pitch = (rounded_input / alignment) * alignment;
    return multiply_checked(*pitch, (unsigned long)height, bytes);
}

int
OSMGAValidateRenderBudget(const OSMGARenderBudgetRequest *request,
                          OSMGARenderBudgetResult *result,
                          OSMGARenderBudgetReason *reason)
{
    unsigned long color_pitch;
    unsigned long color_surface;
    unsigned long color_total;
    unsigned long depth_pitch;
    unsigned long depth_surface;
    unsigned long total;

    if (reason == 0) {
        return 0;
    }
    if (request == 0) {
        *reason = OSMGA_RENDER_BUDGET_INVALID_ARGUMENT;
        return 0;
    }
    if (request->available_bytes == 0 || request->scanout_bytes == 0 ||
        request->render_width == 0 || request->render_height == 0 ||
        request->color_buffer_count == 0) {
        *reason = OSMGA_RENDER_BUDGET_INVALID_LAYOUT;
        return 0;
    }
    if (!is_surface_format(request->color_bits_per_pixel) ||
        (request->depth_bits_per_pixel != 0 &&
         !is_surface_format(request->depth_bits_per_pixel))) {
        *reason = OSMGA_RENDER_BUDGET_UNSUPPORTED_FORMAT;
        return 0;
    }
    if (request->pitch_alignment_bytes == 0) {
        *reason = OSMGA_RENDER_BUDGET_INVALID_ALIGNMENT;
        return 0;
    }
    if (!surface_bytes(request->render_width, request->render_height,
                       request->color_bits_per_pixel,
                       request->pitch_alignment_bytes, &color_pitch,
                       &color_surface) ||
        !multiply_checked(color_surface,
                          (unsigned long)request->color_buffer_count,
                          &color_total)) {
        *reason = OSMGA_RENDER_BUDGET_OVERFLOW;
        return 0;
    }
    depth_pitch = 0;
    depth_surface = 0;
    if (request->depth_bits_per_pixel != 0 &&
        !surface_bytes(request->render_width, request->render_height,
                       request->depth_bits_per_pixel,
                       request->pitch_alignment_bytes, &depth_pitch,
                       &depth_surface)) {
        *reason = OSMGA_RENDER_BUDGET_OVERFLOW;
        return 0;
    }
    if (!add_checked(request->scanout_bytes, request->reserved_bytes, &total) ||
        !add_checked(total, color_total, &total) ||
        !add_checked(total, depth_surface, &total)) {
        *reason = OSMGA_RENDER_BUDGET_OVERFLOW;
        return 0;
    }
    if (total > request->available_bytes) {
        *reason = OSMGA_RENDER_BUDGET_INSUFFICIENT_MEMORY;
        return 0;
    }
    if (result != 0) {
        result->color_pitch_bytes = color_pitch;
        result->color_surface_bytes = color_surface;
        result->depth_pitch_bytes = depth_pitch;
        result->depth_surface_bytes = depth_surface;
        result->total_used_bytes = total;
        result->remaining_bytes = request->available_bytes - total;
    }
    *reason = OSMGA_RENDER_BUDGET_OK;
    return 1;
}

const char *
OSMGARenderBudgetReasonString(OSMGARenderBudgetReason reason)
{
    switch (reason) {
    case OSMGA_RENDER_BUDGET_OK:
        return "ok";
    case OSMGA_RENDER_BUDGET_INVALID_ARGUMENT:
        return "invalid-argument";
    case OSMGA_RENDER_BUDGET_INVALID_LAYOUT:
        return "invalid-layout";
    case OSMGA_RENDER_BUDGET_UNSUPPORTED_FORMAT:
        return "unsupported-format";
    case OSMGA_RENDER_BUDGET_INVALID_ALIGNMENT:
        return "invalid-alignment";
    case OSMGA_RENDER_BUDGET_OVERFLOW:
        return "overflow";
    case OSMGA_RENDER_BUDGET_INSUFFICIENT_MEMORY:
        return "insufficient-memory";
    default:
        return "unknown";
    }
}
