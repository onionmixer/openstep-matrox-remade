/*
 * OpenStepMGARenderBudget.h - pure-C render-memory budget arithmetic.
 *
 * All byte totals are caller-supplied reviewed quantities.  This module has
 * no target-system interfaces and does not infer a physical memory size.
 */

#ifndef OPENSTEP_MGA_RENDER_BUDGET_H
#define OPENSTEP_MGA_RENDER_BUDGET_H

typedef struct {
    unsigned long available_bytes;
    unsigned long scanout_bytes;
    unsigned long reserved_bytes;
    unsigned short render_width;
    unsigned short render_height;
    unsigned int color_bits_per_pixel;
    unsigned int depth_bits_per_pixel;
    unsigned int color_buffer_count;
    unsigned long pitch_alignment_bytes;
} OSMGARenderBudgetRequest;

typedef struct {
    unsigned long color_pitch_bytes;
    unsigned long color_surface_bytes;
    unsigned long depth_pitch_bytes;
    unsigned long depth_surface_bytes;
    unsigned long total_used_bytes;
    unsigned long remaining_bytes;
} OSMGARenderBudgetResult;

typedef enum {
    OSMGA_RENDER_BUDGET_OK = 0,
    OSMGA_RENDER_BUDGET_INVALID_ARGUMENT,
    OSMGA_RENDER_BUDGET_INVALID_LAYOUT,
    OSMGA_RENDER_BUDGET_UNSUPPORTED_FORMAT,
    OSMGA_RENDER_BUDGET_INVALID_ALIGNMENT,
    OSMGA_RENDER_BUDGET_OVERFLOW,
    OSMGA_RENDER_BUDGET_INSUFFICIENT_MEMORY
} OSMGARenderBudgetReason;

/*
 * Account for a known scanout allocation, explicit reserved bytes, one or
 * more color surfaces, and an optional depth surface.  Passing only proves
 * arithmetic against the supplied values; it does not reserve or map memory.
 */
int OSMGAValidateRenderBudget(const OSMGARenderBudgetRequest *request,
                              OSMGARenderBudgetResult *result,
                              OSMGARenderBudgetReason *reason);

const char *OSMGARenderBudgetReasonString(OSMGARenderBudgetReason reason);

#endif /* OPENSTEP_MGA_RENDER_BUDGET_H */
