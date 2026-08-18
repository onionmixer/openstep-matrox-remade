#include <stdio.h>

#include "OpenStepMGARenderBudget.h"

static int failures;

static void
expect(int condition, const char *name)
{
    if (!condition) {
        printf("OPENSTEP_MGA_RENDER_BUDGET_TEST=fail:%s\n", name);
        failures++;
    }
}

static void
make_8m_request(OSMGARenderBudgetRequest *request)
{
    request->available_bytes = 8UL * 1024UL * 1024UL;
    request->scanout_bytes = 1600UL * 1200UL * 4UL;
    request->reserved_bytes = 0;
    request->render_width = 1600;
    request->render_height = 1200;
    request->color_bits_per_pixel = 32;
    request->depth_bits_per_pixel = 16;
    request->color_buffer_count = 1;
    request->pitch_alignment_bytes = 8UL;
}

static void
make_16m_request(OSMGARenderBudgetRequest *request)
{
    make_8m_request(request);
    request->available_bytes = 16UL * 1024UL * 1024UL;
}

int
main(void)
{
    OSMGARenderBudgetRequest request;
    OSMGARenderBudgetResult result;
    OSMGARenderBudgetReason reason;

    expect(OSMGAValidateRenderBudget(0, &result, &reason) == 0,
           "null-request-rejected");
    expect(reason == OSMGA_RENDER_BUDGET_INVALID_ARGUMENT,
           "null-request-reason");

    make_8m_request(&request);
    expect(OSMGAValidateRenderBudget(&request, &result, &reason) == 0,
           "8m-current-mode-color-depth-rejected");
    expect(reason == OSMGA_RENDER_BUDGET_INSUFFICIENT_MEMORY,
           "8m-current-mode-color-depth-reason");

    make_8m_request(&request);
    request.depth_bits_per_pixel = 0;
    expect(OSMGAValidateRenderBudget(&request, &result, &reason) == 0,
           "8m-current-mode-color-only-rejected");
    expect(reason == OSMGA_RENDER_BUDGET_INSUFFICIENT_MEMORY,
           "8m-current-mode-color-only-reason");

    make_16m_request(&request);
    request.depth_bits_per_pixel = 0;
    expect(OSMGAValidateRenderBudget(&request, &result, &reason) == 1,
           "16m-current-mode-color-only-accepted");
    expect(reason == OSMGA_RENDER_BUDGET_OK,
           "16m-current-mode-color-only-reason");
    expect(result.total_used_bytes == 15360000UL,
           "16m-current-mode-color-only-total");
    expect(result.remaining_bytes == 1417216UL,
           "16m-current-mode-color-only-remaining");

    make_16m_request(&request);
    expect(OSMGAValidateRenderBudget(&request, &result, &reason) == 0,
           "16m-current-mode-color-depth-rejected");
    expect(reason == OSMGA_RENDER_BUDGET_INSUFFICIENT_MEMORY,
           "16m-current-mode-color-depth-reason");

    make_8m_request(&request);
    request.scanout_bytes = 1024UL * 768UL * 4UL;
    request.render_width = 1024;
    request.render_height = 768;
    expect(OSMGAValidateRenderBudget(&request, &result, &reason) == 1,
           "8m-1024-color-depth-accepted");
    expect(reason == OSMGA_RENDER_BUDGET_OK, "8m-1024-review-reason");
    expect(result.color_pitch_bytes == 4096UL, "8m-1024-color-pitch");
    expect(result.color_surface_bytes == 3145728UL,
           "8m-1024-color-bytes");
    expect(result.depth_surface_bytes == 1572864UL,
           "8m-1024-depth-bytes");
    expect(result.total_used_bytes == 7864320UL, "8m-1024-total-bytes");
    expect(result.remaining_bytes == 524288UL, "8m-1024-remaining-bytes");

    request.reserved_bytes = 524288UL;
    expect(OSMGAValidateRenderBudget(&request, &result, &reason) == 1,
           "8m-1024-exact-reserve-accepted");
    expect(result.remaining_bytes == 0, "8m-1024-exact-reserve-remaining");

    request.reserved_bytes = 524289UL;
    expect(OSMGAValidateRenderBudget(&request, &result, &reason) == 0,
           "8m-1024-over-reserve-rejected");
    expect(reason == OSMGA_RENDER_BUDGET_INSUFFICIENT_MEMORY,
           "8m-1024-over-reserve-reason");

    make_16m_request(&request);
    request.scanout_bytes = 1024UL * 768UL * 4UL;
    request.render_width = 1024;
    request.render_height = 768;
    request.color_buffer_count = 2;
    expect(OSMGAValidateRenderBudget(&request, &result, &reason) == 1,
           "16m-1024-double-color-depth-accepted");
    expect(result.total_used_bytes == 11010048UL,
           "16m-1024-double-color-depth-total");
    expect(result.remaining_bytes == 5767168UL,
           "16m-1024-double-color-depth-remaining");

    make_8m_request(&request);
    request.pitch_alignment_bytes = 0;
    expect(OSMGAValidateRenderBudget(&request, &result, &reason) == 0,
           "zero-alignment-rejected");
    expect(reason == OSMGA_RENDER_BUDGET_INVALID_ALIGNMENT,
           "zero-alignment-reason");
    expect(OSMGARenderBudgetReasonString(reason)[0] == 'i', "reason-string");

    if (failures != 0) {
        return 1;
    }
    printf("OPENSTEP_MGA_RENDER_BUDGET_TEST_STATUS=pass\n");
    return 0;
}
